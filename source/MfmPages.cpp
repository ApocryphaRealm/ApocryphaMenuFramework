#include "MfmPages.h"

#include "AmfIcons.h"
#include "HelpBar.h"
#include "McmStyle.h"
#include "MfmCore.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mfmpages
{
	namespace
	{
		// The folder MFM reads, relative to the game folder (the working directory), so Mod Organizer's virtual file
		// system serves it exactly as it serves MFM.
		constexpr const wchar_t* kRoot = L"Data/SKSE/Plugins/ccld_ModFunctionMenu";

		// What is known about one entry, keyed by its path from the menu root.
		struct State
		{
			std::string rel;
			std::string label;
			std::filesystem::path path;
			mfm::Function f;
			std::string readError;   // the file could not be read as an entry
			std::string missing;     // what GetModuleHandleW / GetProcAddress did not find ("" = resolved)
			bool resolved = false;
			int runs = 0;
			std::string lastOutcome;
		};

		struct Modal
		{
			bool pending = false;
			std::string title;
			std::string text;
		};

		std::mutex g_lock;   // guards everything below (the render thread draws, DevBench lists)
		bool g_loaded = false;
		bool g_registered = false;
		std::string g_entry;   // the registry entry: "Mod Function Menu" in the language at start-up
		std::string g_page;
		mfm::Node g_mod, g_config;
		std::vector<State> g_states;
		std::vector<std::string> g_problems;
		Modal g_modal;
		// CloseMenuAndResetPath: every folder folded, nested ones too - the folders' open state lives under this number in
		// ImGui's ID stack, so a new number is a tree that has never been opened (the page's root, as MFM's reset shows it)
		int g_treeGeneration = 0;
		std::chrono::steady_clock::time_point g_lastRetry{};

		State* FindState(const std::string& a_rel)
		{
			auto it = std::find_if(g_states.begin(), g_states.end(), [&](const State& s) { return s.rel == a_rel; });
			return it == g_states.end() ? nullptr : &*it;
		}

		// Read (or re-read) the entry's file and resolve it. Logs a change of state. Called with g_lock held.
		void Refresh(State& a_s, bool a_reread)
		{
			const bool wasResolved = a_s.resolved;
			const std::string wasMissing = a_s.missing, wasError = a_s.readError;
			if (a_reread)
			{
				mfm::Function f;
				std::string err;
				if (mfm::ReadFunction(a_s.path, f, err))
				{
					a_s.f = std::move(f);
					a_s.readError.clear();
					logger::debug("mfm: {} - dll = \"{}\", api = \"{}\", type = \"{}\" ({}), preAction = \"{}\" ({}), postAction = \"{}\" ({})", a_s.rel,
								  a_s.f.dll, a_s.f.api, a_s.f.typeText, mfm::TypeName(a_s.f.type), a_s.f.preText, mfm::ActionName(a_s.f.pre),
								  a_s.f.postText, mfm::ActionName(a_s.f.post));
				}
				else
				{
					a_s.readError = err;
				}
			}
			if (!a_s.readError.empty())
			{
				a_s.resolved = false;
				a_s.missing.clear();
				if (a_s.readError != wasError) { logger::warn("mfm: {} - the file cannot be read as an entry: {}", a_s.rel, a_s.readError); }
				return;
			}
			std::string missing;
			a_s.resolved = mfm::Resolve(a_s.f, missing) != nullptr;
			a_s.missing = missing;
			if (a_s.resolved && !wasResolved) { logger::info("mfm: {} - {} ! {} found", a_s.rel, a_s.f.dll, a_s.f.api); }
			if (!a_s.resolved && (wasResolved || wasMissing != a_s.missing))
			{
				logger::warn("mfm: {} - function not found: {} (the button is disabled until it is loaded)", a_s.rel, a_s.missing);
			}
		}

		void RetryUnresolved()
		{
			const auto now = std::chrono::steady_clock::now();
			if (now - g_lastRetry < std::chrono::seconds(2)) { return; }
			g_lastRetry = now;
			for (State& s : g_states)
			{
				if (!s.resolved && s.readError.empty()) { Refresh(s, false); }
			}
		}

		void CloseMenu(mfm::Action a_a, const char* a_when, const std::string& a_rel)
		{
			if (a_a == mfm::Action::kNone) { return; }
			logger::info("mfm: {} - {} {}", a_rel, a_when, mfm::ActionName(a_a));
			renderer::SetMenuVisible(false, false);
			if (a_a == mfm::Action::kCloseMenuAndResetPath)
			{
				std::scoped_lock l(g_lock);
				++g_treeGeneration;
			}
		}

		struct RunResult
		{
			bool found = false;
			bool ran = false;
			bool ok = false;
			std::string type;
			std::string message;
			std::string error;
		};

		// Press one entry: read its file again, resolve, preAction, the guarded call, the message box, postAction.
		// Render thread only. g_lock is NOT held across the call (a function may take its time or call back into AMF).
		RunResult Run(const std::string& a_rel, const char* a_source)
		{
			RunResult r;
			mfm::Function f;
			void* fn = nullptr;
			std::string label;
			{
				std::scoped_lock l(g_lock);
				State* s = FindState(a_rel);
				if (!s) { r.error = "no such entry (do=list names them)"; return r; }
				r.found = true;
				label = s->label;
				Refresh(*s, true);   // MFM reads the entry's file again on every press
				if (!s->readError.empty()) { r.error = "the file cannot be read as an entry: " + s->readError; s->lastOutcome = r.error; return r; }
				if (!s->resolved) { r.error = "function not found: " + s->missing; s->lastOutcome = r.error; return r; }
				f = s->f;
				fn = mfm::Resolve(f, r.error);
				r.type = mfm::TypeName(f.type);
				if (!fn) { r.error = "function not found: " + r.error; s->lastOutcome = r.error; return r; }
			}
			CloseMenu(f.pre, "preAction", a_rel);
			const auto t0 = std::chrono::steady_clock::now();
			const mfm::CallResult c = mfm::Call(fn, f.type);
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
			r.ran = true;
			r.ok = c.ok;
			r.message = c.message;
			r.error = c.error;
			if (c.ok)
			{
				logger::info("mfm: {} - {} ! {} ({}) ran in {} ms ({}){}", a_rel, f.dll, f.api, r.type, ms, a_source,
							 f.type == mfm::Type::kVoid ? std::string() : ", returned " + std::to_string(c.message.size()) + " char(s)");
			}
			else
			{
				logger::error("mfm: {} - {} ! {} ({}) failed: {} ({})", a_rel, f.dll, f.api, r.type, c.error, a_source);
			}
			{
				std::scoped_lock l(g_lock);
				if (State* s = FindState(a_rel))
				{
					++s->runs;
					s->lastOutcome = c.ok ? "ran" : c.error;
				}
				if (!c.ok)
				{
					g_modal = { true, label, mcmstyle::Fmt(strings::TR("AMF_MfmFailed", "The function failed: %s"), c.error.c_str()) };
				}
				else if (f.type != mfm::Type::kVoid)
				{
					g_modal = { true, label, c.message };
				}
			}
			CloseMenu(f.post, "postAction", a_rel);
			return r;
		}

		// One folder's contents. Called with g_lock held; a press is recorded in a_pressed and run after the lock is let go.
		void DrawNode(const mfm::Node& a_n, int a_depth, std::string& a_pressed)
		{
			for (const mfm::Node& c : a_n.children)
			{
				ImGui::PushID(c.rel.c_str());
				if (c.folder)
				{
					if (mfm::CountFiles(c) == 0) { ImGui::PopID(); continue; }   // a folder with no entries in it draws nothing
					const bool open = a_depth == 0 ? ImGui::CollapsingHeader(c.name.c_str())
												   : ImGui::TreeNodeEx(c.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding);
					if (open)
					{
						if (a_depth == 0) { ImGui::Indent(); }
						DrawNode(c, a_depth + 1, a_pressed);
						if (a_depth == 0) { ImGui::Unindent(); }
						else { ImGui::TreePop(); }
					}
					ImGui::PopID();
					continue;
				}
				const State* s = FindState(c.rel);
				const bool runnable = s && s->resolved;
				const std::string label = std::string(icons::kAction) + "  " + c.name;
				// an entry that cannot run is inert AND looks it: ImGui's disabled fade alone left the label reading as white in the
				// 2026-10-12 test captures, so its label also takes the theme's disabled text colour (every theme has one)
				if (!runnable)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
					ImGui::BeginDisabled();
				}
				if (ImGui::Button(label.c_str())) { a_pressed = c.rel; }
				if (!runnable)
				{
					ImGui::EndDisabled();
					ImGui::PopStyleColor();
				}
				if (s && s->readError.empty())
				{
					// the function it calls, as the entry names it (the mod's own data, shown as written)
					helpbar::OfferForLastItem(s->f.dll + " ! " + s->f.api + " (" + mfm::TypeName(s->f.type) + ")");
				}
				if (s && !runnable)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::PushTextWrapPos(0.0f);
					const std::string line = s->readError.empty()
						? mcmstyle::Fmt(strings::TR("AMF_MfmNotFound", "Function not found: %s"), s->missing.c_str())
						: mcmstyle::Fmt(strings::TR("AMF_MfmBadFile", "This entry's file could not be read: %s"), s->readError.c_str());
					ImGui::TextUnformatted((std::string(icons::kWarning) + "  " + line).c_str());
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}
				ImGui::PopID();
			}
		}

		void DrawModal()
		{
			Modal m;
			{
				std::scoped_lock l(g_lock);
				m = g_modal;
			}
			if (!m.pending) { return; }
			if (!ImGui::IsPopupOpen("##mfmmessage")) { ImGui::OpenPopup("##mfmmessage"); }
			if (ImGui::BeginPopupModal("##mfmmessage", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
			{
				const float font = ImGui::GetFontSize();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HeaderTextColor());
				ImGui::TextUnformatted(m.title.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
				// a long reply (up to 16 KB) scrolls inside the box instead of growing it off the screen
				const ImVec2 size(font * 32.0f, std::min(font * 18.0f, ImGui::CalcTextSize(m.text.c_str(), nullptr, false, font * 31.0f).y + font));
				if (ImGui::BeginChild("##mfmtext", size, false))
				{
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(m.text.empty() ? " " : m.text.c_str());
					ImGui::PopTextWrapPos();
				}
				ImGui::EndChild();
				ImGui::Spacing();
				if (ImGui::Button(strings::TR("AMF_MfmOk", "OK"), ImVec2(font * 6.0f, 0.0f)))
				{
					std::scoped_lock l(g_lock);
					g_modal.pending = false;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndPopup();
			}
		}

		void DrawPage()
		{
			std::string pressed;
			{
				std::scoped_lock l(g_lock);
				helpbar::Want("mfm");
				RetryUnresolved();
				mcmstyle::PageScope scope;
				ImGui::PushID(g_treeGeneration);
				for (const mfm::Node* g : { &g_mod, &g_config })
				{
					if (mfm::CountFiles(*g) == 0) { continue; }
					mcmstyle::Heading(g == &g_mod ? strings::TR("AMF_MfmGroupMod", "Mod") : strings::TR("AMF_MfmGroupConfig", "Config"));
					DrawNode(*g, 0, pressed);
					ImGui::Spacing();
				}
				ImGui::PopID();
			}
			if (!pressed.empty()) { Run(pressed, "pressed"); }
			DrawModal();
		}

		void Collect(const mfm::Node& a_n)
		{
			if (!a_n.folder)
			{
				State s;
				s.rel = a_n.rel;
				s.label = a_n.name;
				s.path = a_n.path;
				g_states.push_back(std::move(s));
				return;
			}
			for (const mfm::Node& c : a_n.children) { Collect(c); }
		}

		std::string J(const std::string& a_s) { return nlohmann::json(a_s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

		// Mod Function Menu's own DLL is loaded as well (the owner, 2026-10-12: "ideally amf would make mfm unneccacery").
		// Only reported - its files are never touched and it is never held back; both menus then open on F1.
		bool MfmLoaded() { return ::GetModuleHandleW(L"ccld_ModFunctionMenu.dll") != nullptr; }
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (g_loaded) { return; }   // once per session, as MFM builds its tree once
		g_loaded = true;
		if (MfmLoaded())
		{
			// tested 2026-10-12: with MFM 1.5.0 loaded a real F1 opened ONLY MFM. It hooks the same input call AMF does (ID 67315+0x7B
			// on SE - its log's "ProcessInputQueue" address minus the image base), later than AMF, so its hook sees each key first and
			// does not pass its own key on
			logger::info("mfm: Mod Function Menu is installed as well, and it takes the key first: on F1, the default key of both, only "
						 "Mod Function Menu opens and AMF does not. Uninstall it - AMF runs its menus - or give it another key in "
						 "ccld_ModFunctionMenu.toml.");
		}
		const std::filesystem::path root(kRoot);
		g_mod = mfm::BuildGroup(root, "Mod", g_problems);
		g_config = mfm::BuildGroup(root, "Config", g_problems);
		for (const std::string& p : g_problems) { logger::warn("mfm: {} - folder not read", p); }
		Collect(g_mod);
		Collect(g_config);
		std::size_t resolved = 0;
		for (State& s : g_states)
		{
			Refresh(s, true);
			resolved += s.resolved ? 1 : 0;
		}
		std::error_code ec;
		logger::info("mfm: {} entr(ies) in {} (Mod {}, Config {}), {} resolved{}", g_states.size(), mfm::Narrow(root.wstring()),
					 mfm::CountFiles(g_mod), mfm::CountFiles(g_config), resolved,
					 std::filesystem::is_directory(root, ec) ? "" : " - the folder is not there (no mod ships Mod Function Menu entries)");
		if (g_states.empty()) { return; }
		g_entry = strings::TR("AMF_MfmMenu", "Mod Function Menu");
		g_page = g_entry;
		g_registered = registry::RegisterFn(g_entry.c_str(), g_page.c_str(), []() { DrawPage(); }, true);
		if (g_registered) { registry::SetPageVisible(g_entry.c_str(), g_page.c_str(), settings::Get().mfmMenus); }
		logger::info("mfm: menu \"{}\" {}{}", g_entry, g_registered ? "registered" : "NOT registered (the name is taken)",
					 g_registered && !settings::Get().mfmMenus ? ", hidden (Settings > Converted menus > MFM is off)" : "");
	}

	void ApplyVisibility()
	{
		std::scoped_lock l(g_lock);
		const bool show = settings::Get().mfmMenus;
		if (g_registered) { registry::SetPageVisible(g_entry.c_str(), g_page.c_str(), show); }
		logger::info("mfm: menu {} ({} entr(ies))", show ? "shown" : "hidden", g_states.size());
	}

	void DrawSettingsList()
	{
		std::scoped_lock l(g_lock);
		std::size_t resolved = 0;
		for (const State& s : g_states) { resolved += s.resolved ? 1 : 0; }
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		if (g_states.empty())
		{
			ImGui::TextUnformatted(strings::TR("AMF_MfmNone", "No Mod Function Menu entries were found (Data\\SKSE\\Plugins\\ccld_ModFunctionMenu)."));
		}
		else
		{
			ImGui::Text(strings::TR("AMF_MfmCount", "%d entries found (Mod %d, Config %d); %d of them can run."), static_cast<int>(g_states.size()),
						static_cast<int>(mfm::CountFiles(g_mod)), static_cast<int>(mfm::CountFiles(g_config)), static_cast<int>(resolved));
		}
		ImGui::TextUnformatted(strings::TR("AMF_MfmNotNeeded", "Mod Function Menu itself is not needed: AMF runs these menus. Mods that "
			"list it as a requirement only need their own files."));
		if (MfmLoaded())
		{
			ImGui::TextUnformatted(strings::TR("AMF_MfmAlsoInstalled", "Mod Function Menu is installed as well, and it takes the key first: on F1, "
				"the default key of both, only Mod Function Menu opens and AMF does not. Uninstall it - AMF runs its menus - or give it "
				"another key in ccld_ModFunctionMenu.toml."));
		}
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		if (what == "list")
		{
			std::scoped_lock l(g_lock);
			std::string out;
			for (const State& s : g_states)
			{
				out += (out.empty() ? "" : ",") + std::string("{\"path\":") + J(s.rel) + ",\"label\":" + J(s.label) + ",\"dll\":" + J(s.f.dll) +
					   ",\"api\":" + J(s.f.api) + ",\"type\":\"" + mfm::TypeName(s.f.type) + "\",\"preAction\":\"" + mfm::ActionName(s.f.pre) +
					   "\",\"postAction\":\"" + mfm::ActionName(s.f.post) + "\",\"resolved\":" + (s.resolved ? "true" : "false") +
					   ",\"missing\":" + J(s.missing) + ",\"readError\":" + J(s.readError) + ",\"runs\":" + std::to_string(s.runs) +
					   ",\"lastOutcome\":" + J(s.lastOutcome) + "}";
			}
			return std::string("{\"ok\":true,\"op\":\"mfm\",\"do\":\"list\",\"enabled\":") + (settings::Get().mfmMenus ? "true" : "false") +
				   ",\"registered\":" + (g_registered ? "true" : "false") +
				   ",\"mfmLoaded\":" + (MfmLoaded() ? "true" : "false") + ",\"menu\":" + J(g_entry) + ",\"count\":" + std::to_string(g_states.size()) +
				   ",\"entries\":[" + out + "]}";
		}
		if (what == "run")
		{
			std::string path = str("path");
			std::replace(path.begin(), path.end(), '\\', '/');
			while (!path.empty() && path.front() == '/') { path.erase(path.begin()); }
			{
				std::scoped_lock l(g_lock);
				if (!FindState(path) && FindState(path + ".toml")) { path += ".toml"; }
			}
			// the call runs on the render thread, never on DevBench's listener thread; the result comes back through a
			// shared block, so a job that runs after the wait has given up writes into nothing that is gone
			auto result = std::make_shared<RunResult>();
			auto done = std::make_shared<std::atomic<bool>>(false);
			const bool ran = renderer::RunOnRenderThreadWait([path, result, done]() {
				*result = Run(path, "DevBench");
				done->store(true);
			}, 5000);
			if (!ran || !done->load())
			{
				return "{\"ok\":false,\"op\":\"mfm\",\"do\":\"run\",\"path\":" + J(path) +
					   ",\"error\":\"no frame took the call within 5 s (is the game drawing?) - it runs at the next frame\"}";
			}
			const RunResult r = *result;
			return std::string("{\"ok\":") + (r.ok ? "true" : "false") + ",\"op\":\"mfm\",\"do\":\"run\",\"path\":" + J(path) + ",\"found\":" +
				   (r.found ? "true" : "false") + ",\"ran\":" + (r.ran ? "true" : "false") + ",\"type\":" + J(r.type) + ",\"message\":" + J(r.message) +
				   (r.error.empty() ? std::string() : ",\"error\":" + J(r.error)) + "}";
		}
		return "{\"ok\":false,\"op\":\"mfm\",\"error\":\"do = list | run {path}\"}";
	}
}
