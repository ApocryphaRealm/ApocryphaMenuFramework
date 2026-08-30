#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "MainMenuDriver.h"
#include "Renderer.h"
#include "Watchdog.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include "utils/Logger.h"

#include <string>

namespace devbenchtool
{
	namespace
	{
		// Minimal extractor for a top-level JSON string field: finds "key":"value" and returns
		// value (handling backslash escapes). Enough for this tool's small, controlled argument
		// shape - no dependency on a JSON library.
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
				if (a_json[pos] == '\\' && pos + 1 < a_json.size())
				{
					++pos;
				}
				out += a_json[pos];
				++pos;
			}
			return out;
		}

		// Runs on devbench's LISTENER thread. Every renderer:: call used here is thread-safe
		// (atomic visibility, mutex-guarded selection, RunConsoleCommand marshals to the main
		// thread, registry::Snapshot is thread-safe), so no extra marshalling is needed.
		void MenuTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			std::string result;
			if (op == "open")
			{
				renderer::SetMenuVisible(true);
				result = "{\"ok\":true,\"op\":\"open\"}";
			}
			else if (op == "close")
			{
				renderer::SetMenuVisible(false);
				result = "{\"ok\":true,\"op\":\"close\"}";
			}
			else if (op == "select")
			{
				const std::string node = JsonStr(args, "node");
				renderer::SetSelectedNode(node);
				result = "{\"ok\":true,\"op\":\"select\",\"node\":\"" + node + "\"}";
			}
			else if (op == "activate")
			{
				renderer::ActivateSelectedNode();
				result = std::string("{\"ok\":true,\"op\":\"activate\",\"selected\":\"") +
						 renderer::GetSelectedNode() + "\"}";
			}
			else if (op == "state" || op.empty())
			{
				result = renderer::GetMenuStateJson();
			}
			else
			{
				result = "{\"ok\":false,\"error\":\"unknown op '" + op + "'\"}";
			}

			a_write(a_sink, result.c_str());
		}

		// Process control - deliberately separate from the menu tool. Runs on devbench's LISTENER
		// thread, which keeps answering while the game's main thread is wedged, so "kill" works on a
		// hung game where taskkill/Task Manager do not (see Watchdog.h).
		void ProcessTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			if (op == "kill")
			{
				// Answer BEFORE terminating, so the caller gets a reply rather than a dropped socket.
				a_write(a_sink, "{\"ok\":true,\"op\":\"kill\",\"note\":\"terminating now\"}");
				watchdog::KillNow("amf.process kill requested over DevBench");
			}

			if (op == "capture")
			{
				// Saves the current frame WITH the framework overlay to
				// Data\\SKSE\\Plugins\\ApocryphaMenuFramework\\captures\\<name>.png (name from args, default a timestamp).
				std::string name = JsonStr(args, "name");
				if (name.empty()) { name = "capture-" + std::to_string(static_cast<long long>(std::time(nullptr))); }
				for (char& c : name) { if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) { c = '_'; } }
				std::filesystem::path dir = std::filesystem::path("Data") / "SKSE" / "Plugins" / "ApocryphaMenuFramework" / "captures";
				std::error_code ec; std::filesystem::create_directories(dir, ec);
				const std::filesystem::path file = std::filesystem::absolute(dir / (name + ".png"), ec);
				const std::string err = renderer::CaptureBlocking(file.wstring(), 3000);
				std::string esc; for (char c : file.string()) { if (c == '\\' || c == '"') { esc += '\\'; } esc += c; }
				std::string reply = std::string("{\"ok\":") + (err.empty() ? "true" : "false") + ",\"op\":\"capture\",\"path\":\"" + esc + "\"";
				if (!err.empty()) { reply += ",\"error\":\"" + err + "\""; }
				reply += "}";
				a_write(a_sink, reply.c_str());
				return;
			}

			const std::string status = watchdog::StatusJson();
			if (op == "status" || op.empty())
			{
				a_write(a_sink, status.c_str());
				return;
			}
			const std::string err = "{\"ok\":false,\"error\":\"unknown op\",\"status\":" + status + "}";
			a_write(a_sink, err.c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}

		auto* dev = DevBenchAPI::GetDevBenchInterface001();
		if (!dev)
		{
			if (a_lastAttempt)
			{
				logger::info("DevBench not detected; the amf.menu driving tool is unavailable this "
							 "session (the menu still works normally)");
			}
			else
			{
				logger::debug("DevBench not detected yet; will retry at the next message");
			}
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Drive and inspect the Apocrypha Menu Framework window for testing. "
			"op: open|close|select|activate|state. For select, node is a path: game-settings, stats, "
			"quest, general, system/save, system/load, system/savequit, system/quit, or mod:<index>. "
			"activate runs the selected node's action (Save/Quit). state returns visibility, the "
			"selected node and every registered mod + its pages.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (dev->RegisterTool("amf.menu", descriptor, &MenuTool, nullptr))
		{
			logger::info("Registered \"amf.menu\" driving tool with DevBench (build {})", dev->GetBuildNumber());
		}
		else
		{
			logger::warn("DevBench reported \"amf.menu\" replaced an existing tool of the same name");
		}

		constexpr const char* procDescriptor =
			"{"
			"\"description\":\"Process control for the running game. op: status (frame counter, seconds "
			"since the last rendered frame, whether the hang watchdog considers it hung) | kill (force-exit "
			"the game immediately - works even when the main thread is HUNG and taskkill cannot help, "
			"because it terminates from inside the process).\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("amf.process", procDescriptor, &ProcessTool, nullptr))
		{
			logger::info("Registered \"amf.process\" (status/kill) with DevBench");
		}

		// Rule 64's start-menu extension: the vanilla Main Menu driver (amf.mainmenu).
		mainmenudriver::Register(dev);

		registered = true;
	}
}
