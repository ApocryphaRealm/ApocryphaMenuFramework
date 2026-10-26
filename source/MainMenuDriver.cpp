#include "MainMenuDriver.h"

#include "DevBench/DevBenchAPI.h"
#include "utils/Logger.h"
#include "utils/NullGuard.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <tuple>

namespace mainmenudriver
{
	namespace
	{
		// Same minimal top-level string extractor as DevBenchTool.cpp - small controlled args.
		std::string JsonStr(const std::string& a_json, const char* a_key)
		{
			const std::string key = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(key);
			if (pos == std::string::npos)
			{
				return "";
			}
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string::npos)
			{
				return "";
			}
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t'))
			{
				++pos;
			}
			if (pos >= a_json.size() || a_json[pos] != '"')
			{
				return "";
			}
			++pos;
			std::string out;
			while (pos < a_json.size() && a_json[pos] != '"')
			{
				if (a_json[pos] == '\x5C' && pos + 1 < a_json.size())
				{
					++pos;
				}
				out += a_json[pos];
				++pos;
			}
			return out;
		}

		std::string JsonEscape(const std::string& a_v)
		{
			std::string o;
			for (char c : a_v)
			{
				if (c == '"' || c == '\x5C')
				{
					o += '\x5C';
				}
				if (c == '\n' || c == '\r' || c == '\t')
				{
					o += ' ';
					continue;
				}
				o += c;
			}
			return o;
		}

		// The thread SKSE's UI tasks run on, noted by the first one that runs (default id: not seen yet).
		std::atomic<std::thread::id> g_uiThread{};

		// Runs a_fn on the UI THREAD (SKSE AddUITask: inside the UI's own message processing, the thread the menu movies
		// advance on) and waits up to 2 s for its result string, so a read op answers synchronously from DevBench's
		// listener thread. 2.3.0 (gate rows scaleform-calls-only-on-the-ui-thread / scaleform-helpers-only-on-the-ui-thread,
		// after Dragon's Eye Minimap 1.7.5's crashes): this used AddTask, whose queue on SE 1.5.97 runs as its own job
		// beside the UI job, so getvar's GetVariable on the Main Menu movie could race the movie's own Advance.
		// It cannot deadlock: DevBench calls tools on its listener thread (DevBenchAPI.h), never the UI thread; should a
		// caller ever BE the UI thread (it matches the id the UI tasks noted), the job runs inline instead of waiting on
		// itself; and every wait is bounded (2 s, then a "timed out" answer). The shared state outlives a timed-out wait,
		// so a late task writes into it safely.
		std::string RunOnUIThreadBlocking(const char* a_what, std::function<std::string()> a_fn)
		{
			AMF_REQUIRE(a_fn, "{\"ok\":false,\"error\":\"no job to run\"}");
			if (g_uiThread.load() == std::this_thread::get_id())
			{
				logger::debug("amf.mainmenu {}: called on the UI thread - run inline", a_what ? a_what : "?");
				return a_fn();
			}
			auto state = std::make_shared<std::tuple<std::mutex, std::condition_variable, std::string, bool>>();

			auto* tasks = SKSE::GetTaskInterface();  // 2.3.0: null before SKSE::Init has queried it
			AMF_REQUIRE(tasks, "{\"ok\":false,\"error\":\"SKSE task interface unavailable\"}");
			const std::string what = a_what ? a_what : "?";
			tasks->AddUITask([state, what, fn = std::move(a_fn)]() {
				g_uiThread.store(std::this_thread::get_id());
				logger::info("amf.mainmenu {}: running on the UI task", what);
				std::string result = fn ? fn() : std::string();   // 2.3.0: an empty job answers "" instead of throwing on the UI thread
				{
					std::scoped_lock l(std::get<0>(*state));
					std::get<2>(*state) = std::move(result);
					std::get<3>(*state) = true;
				}
				std::get<1>(*state).notify_all();
			});

			std::unique_lock l(std::get<0>(*state));
			if (!std::get<1>(*state).wait_for(l, std::chrono::milliseconds(2000), [&] { return std::get<3>(*state); }))
			{
				logger::warn("amf.mainmenu {}: the UI task did not answer within 2 s", what);
				return "{\"ok\":false,\"error\":\"UI task timed out (2s) - engine busy or paused\"}";
			}
			return std::get<2>(*state);
		}

		void PostUserEvent(std::string a_text)
		{
			auto* tasks = SKSE::GetTaskInterface();  // 2.3.0: null before SKSE::Init has queried it
			AMF_REQUIRE_VOID(tasks);
			tasks->AddTask([text = std::move(a_text)]() {
				auto* queue = RE::UIMessageQueue::GetSingleton();
				auto* strings = RE::InterfaceStrings::GetSingleton();
				if (!queue || !strings)
				{
					logger::warn("amf.mainmenu userevent: UIMessageQueue/InterfaceStrings unavailable");
					return;
				}
				auto* data = static_cast<RE::BSUIMessageData*>(queue->CreateUIMessageData(strings->bsUIMessageData));
				if (!data)
				{
					logger::warn("amf.mainmenu userevent: CreateUIMessageData returned null");
					return;
				}
				data->fixedStr = text.c_str();
				queue->AddMessage(RE::MainMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kUserEvent, data);
				logger::info("amf.mainmenu: posted kUserEvent \"{}\" to Main Menu", text);
			});
		}

		// UNSAFE-BY-EXPERIMENT, REMOVED (2026-08-28): calling the game's registered GameDelegate
		// handler directly - fxDelegate->Callback(movie, "StartNewGame", nullptr, 0) - CRASHED the
		// game instantly (crash-2026-08-28-19-37-57: access violation reading 0x08 inside
		// SkyrimSE.exe+0ED6C6D, i.e. the handler dereferencing the FxDelegateArgs we never supplied).
		// The handlers are not callable with fabricated arguments from outside the UI's own context.
		//
		// The SAFE equivalent is to drive the menu's OWN ActionScript, exactly as a button press does,
		// and let the game build the delegate context itself. Decompiled StartMenu.as (FFDec) shows
		// the terminal action of the New Game flow is `this.FadeOutAndCall("StartNewGame")`, so:
		//   invoke path="_root.MenuHolder.Menu_mc.FadeOutAndCall" arg="StartNewGame"
		// Likewise "ContinueLastSavedGame", and QuitToDesktop via GameDelegate. That is what the
		// `invoke` op below does, now with an optional string argument.

		void InvokeMoviePath(std::string a_path, std::string a_arg)
		{
			auto* tasks = SKSE::GetTaskInterface();  // 2.3.0: null before SKSE::Init has queried it
			AMF_REQUIRE_VOID(tasks);
			// 2.3.0: on the UI task (AddUITask), not AddTask - Invoke on the menu movie must not race its Advance
			tasks->AddUITask([path = std::move(a_path), arg = std::move(a_arg)]() {
				g_uiThread.store(std::this_thread::get_id());
				auto* ui = RE::UI::GetSingleton();
				auto menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
				if (!menu || !menu->uiMovie)
				{
					logger::warn("amf.mainmenu invoke: Main Menu or its movie is not available");
					return;
				}
				RE::GFxValue result;
				bool ok = false;
				if (arg.empty())
				{
					ok = menu->uiMovie->Invoke(path.c_str(), &result, nullptr, 0);
				}
				else
				{
					RE::GFxValue a;
					a.SetString(arg.c_str());
					ok = menu->uiMovie->Invoke(path.c_str(), &result, &a, 1);
				}
				logger::info("amf.mainmenu: Invoke(\"{}\"{}) -> {}", path,
							 arg.empty() ? "" : (", \"" + arg + "\""), ok ? "ok" : "FAILED");
			});
		}

		void MainMenuTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			AMF_REQUIRE_VOID(a_write);   // DevBench always passes its writer; without one there is no one to answer
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			std::string result;
			if (op == "userevent")
			{
				const std::string text = JsonStr(args, "text");
				if (text.empty())
				{
					result = "{\"ok\":false,\"error\":\"userevent needs text\"}";
				}
				else
				{
					PostUserEvent(text);
					result = "{\"ok\":true,\"op\":\"userevent\",\"queued\":true,\"text\":\"" + JsonEscape(text) +
							 "\",\"note\":\"observe lifecycle (newGame/preLoadGame) and the AMF log for the outcome\"}";
				}
			}
			else if (op == "invoke")
			{
				const std::string path = JsonStr(args, "path");
				if (path.empty())
				{
					result = "{\"ok\":false,\"error\":\"invoke needs path\"}";
				}
				else
				{
					const std::string arg = JsonStr(args, "arg");
					InvokeMoviePath(path, arg);
					result = "{\"ok\":true,\"op\":\"invoke\",\"queued\":true,\"path\":\"" + JsonEscape(path) +
							 "\",\"arg\":\"" + JsonEscape(arg) + "\"}";
				}
			}
			else if (op == "delegate")
			{
				const std::string name = JsonStr(args, "name");
				if (name.empty())
				{
					result = "{\"ok\":false,\"error\":\"delegate needs name (e.g. StartNewGame, ContinueLastSavedGame, QuitToDesktop)\"}";
				}
				else
				{
					// Route through the menu's own ActionScript (safe) rather than calling the
					// game's handler directly (that crashed - see the note above).
					InvokeMoviePath("_root.MenuHolder.Menu_mc.FadeOutAndCall", name);
					result = "{\"ok\":true,\"op\":\"delegate\",\"queued\":true,\"name\":\"" + JsonEscape(name) +
							 "\",\"via\":\"FadeOutAndCall\",\"note\":\"watch lifecycle (newGame/preLoadGame) for the outcome\"}";
				}
			}
			else if (op == "getvar")
			{
				const std::string path = JsonStr(args, "path");
				if (path.empty())
				{
					result = "{\"ok\":false,\"error\":\"getvar needs path\"}";
				}
				else
				{
					result = RunOnUIThreadBlocking("getvar", [path]() -> std::string {
						auto* ui = RE::UI::GetSingleton();
						auto menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
						if (!menu || !menu->uiMovie)
						{
							return "{\"ok\":false,\"error\":\"Main Menu or its movie is not available\"}";
						}
						RE::GFxValue v;
						if (!menu->uiMovie->GetVariable(&v, path.c_str()))
						{
							return "{\"ok\":false,\"error\":\"GetVariable failed for path\"}";
						}
						std::string rep;
						if (v.IsString())
						{
							rep = "\"" + JsonEscape(v.GetString() ? v.GetString() : "") + "\"";
						}
						else if (v.IsNumber())
						{
							rep = std::to_string(v.GetNumber());
						}
						else if (v.IsBool())
						{
							rep = v.GetBool() ? "true" : "false";
						}
						else
						{
							rep = "\"<type " + std::to_string(static_cast<int>(v.GetType())) + ">\"";
						}
						return "{\"ok\":true,\"value\":" + rep + "}";
					});
				}
			}
			else if (op == "state" || op.empty())
			{
				result = RunOnUIThreadBlocking("state", []() -> std::string {
					auto* ui = RE::UI::GetSingleton();
					const bool open = ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
					auto menu = ui ? ui->GetMenu(RE::MainMenu::MENU_NAME) : nullptr;
					const bool movie = menu && menu->uiMovie;
					return std::string("{\"ok\":true,\"mainMenuOpen\":") + (open ? "true" : "false") +
						   ",\"moviePresent\":" + (movie ? "true" : "false") + "}";
				});
			}
			else
			{
				result = "{\"ok\":false,\"error\":\"unknown op '" + JsonEscape(op) + "'\"}";
			}

			a_write(a_sink, result.c_str());
		}
	}

	void Register(DevBenchAPI::IDevBenchInterface001* a_devBench)
	{
		if (!a_devBench)
		{
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Drive and inspect the GAME's vanilla Main Menu headlessly for testing. "
			"op: state | delegate (name: a GameDelegate callback - StartNewGame, "
			"ContinueLastSavedGame, QuitToDesktop, NEW, CONTINUE, HELP - the exact C++ handler the "
			"menu's buttons reach) | invoke (path: an ActionScript method on the menu movie) | "
			"getvar (path: an ActionScript variable) | userevent (text: post a kUserEvent). "
			"delegate/invoke/userevent queue to the main thread; watch lifecycle/log for the outcome.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},\"path\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (a_devBench->RegisterTool("amf.mainmenu", descriptor, &MainMenuTool, nullptr))
		{
			logger::info("Registered \"amf.mainmenu\" main-menu driver with DevBench");
		}
		else
		{
			logger::warn("DevBench reported \"amf.mainmenu\" replaced an existing tool of the same name");
		}
	}
}
