#include "PCH.h"

#include "FlickHost.h"

#include "FlickImGuiMap.h"
#include "HelpBar.h"
#include "PreciseSlider.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

// See FlickHost.h. Plan: 4. plans\amf-flick\PLAN.md (sections 3-6). This file is the first cut (the plan's phases 1-2):
// every one of the 259 slots answers - none is ever null (logic library 49: a null slot is a jump to address zero) -
// the registration, display, IO, styling, layout, text, ID, interaction and widget slots do what FLICK's do on AMF's
// ImGui in AMF's look, and the rest (images, input feed, bindings, free windows, game control, INI helpers) answer a
// safe default and are logged once each, so "why does this FLICK mod's X do nothing" is answered by the log.

namespace flick
{
	namespace
	{
		namespace map = imgui_map;

		std::atomic<bool> g_host{ true };
		bool g_realFlick = false;

		// ---- the mods' pages ----------------------------------------------------------------------------------------
		struct ToolRec
		{
			Tool*       tool = nullptr;
			std::string plugin, name, group, entry;
			bool        listed = true;
			std::string dll;
		};

		// The DLL a mod's object lives in: the module holding its vtable.
		std::string DllOf(const void* a_object)
		{
			HMODULE mod = nullptr;
			const void* vtable = a_object ? *reinterpret_cast<void* const*>(a_object) : nullptr;
			if (!vtable || !::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
							   reinterpret_cast<LPCWSTR>(vtable), &mod) || !mod)
			{
				return {};
			}
			wchar_t path[MAX_PATH]{};
			const auto len = ::GetModuleFileNameW(mod, path, MAX_PATH);
			std::string name = std::filesystem::path(std::wstring(path, len)).filename().string();
			std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return name;
		}

		// the per-mod choice (FlickHost.h)
		constexpr const char* kLeftToFlickPath = "Data/SKSE/Plugins/ApocryphaMenuFramework/FlickLeftToFlick.txt";
		std::mutex g_choiceLock;
		std::set<std::string> g_leftAtStart;   // as read with Configure: what the alias answers by this session
		std::set<std::string> g_leftNow;       // as saved now
		std::map<std::string, bool> g_consumers;   // dll -> answered with AMF's table
		std::map<std::string, std::string> g_names;   // dll -> shown name, kept with the choice

		void SaveLeftLocked()
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(kLeftToFlickPath).parent_path(), ec);
			std::ofstream out(kLeftToFlickPath, std::ios::binary | std::ios::trunc);
			out << "# FLICK mods left to the real FLICK (FUCK.dll) - one DLL file name per line. Set in the menu: Settings >\r\n"
				   "# Converted menus > FLICK. Applies from the next game start.\r\n";
			for (const auto& dll : g_leftNow)
			{
				const auto it = g_names.find(dll);
				out << dll << (it != g_names.end() && !it->second.empty() ? " = " + it->second : std::string{}) << "\r\n";
			}
		}
		std::mutex g_lock;
		std::vector<ToolRec> g_tools;     // registration order; never shrinks (a mod's object lives as long as the game)
		std::vector<Window*> g_windows;   // registered, not drawn yet (free windows are the plan's phase 4)

		int   g_inDraw = 0;               // render thread: inside a mod's Draw that AMF called
		// What the open FLICK page drew last frame, for the DevBench flick op (rule 64: a driving tool reads the page instead
		// of guessing at it - the 2026-10-08 run had no working screen capture): each widget's kind, label, rect, and whether
		// it holds the keyboard / controller highlight or is hovered; a header also says whether it is open.
		struct ItemRec { const char* kind; std::string label; ImVec2 min, max; bool focused, hovered, value; };
		std::vector<ItemRec> g_items;         // render thread, this frame
		std::mutex g_itemLock;
		std::vector<ItemRec> g_itemsShown;    // last complete frame, read by StatusJson
		void Note(const char* a_kind, const char* a_label, bool a_value = false)
		{
			if (g_inDraw <= 0) { return; }
			g_items.push_back({ a_kind, a_label ? a_label : "", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::IsItemFocused(),
								ImGui::IsItemHovered(), a_value });
		}
		Tool* g_active = nullptr;         // render thread: the FLICK page open now (OnOpen called, OnClose not yet)
		Tool* g_drawnThisFrame = nullptr;

		// ---- the calls not done yet, each logged once ----------------------------------------------------------------
		std::mutex g_noteLock;
		std::set<std::string> g_notes;
		void NoteOnce(const char* a_what, const char* a_why)
		{
			std::lock_guard lock(g_noteLock);
			if (g_notes.insert(a_what).second)
			{
				logger::info("flick: a FLICK mod called {} - {}", a_what, a_why);
			}
		}

		template <class R>
		R Zero()
		{
			if constexpr (std::is_void_v<R>) { return; }
			else { return R{}; }
		}

		// ---- the mods' own text: Interface/Translations/<plugin>_<language>.txt (UTF-16 LE, "$KEY<tab>text") ------------
		std::mutex g_trLock;
		std::unordered_map<std::string, std::string> g_tr;   // "$KEY" -> text; node-based, so c_str() stays put
		std::set<std::string> g_trLoaded;

		std::string Utf16ToUtf8(const std::wstring& a_w)
		{
			if (a_w.empty()) { return {}; }
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		bool ReadTranslationFile(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) { return false; }
			std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			std::string text;
			if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFF && static_cast<unsigned char>(raw[1]) == 0xFE)
			{
				std::wstring w(reinterpret_cast<const wchar_t*>(raw.data() + 2), (raw.size() - 2) / 2);
				text = Utf16ToUtf8(w);
			}
			else
			{
				text = raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0 ? raw.substr(3) : raw;
			}
			std::size_t pos = 0, count = 0;
			while (pos < text.size())
			{
				std::size_t end = text.find('\n', pos);
				if (end == std::string::npos) { end = text.size(); }
				std::string line = text.substr(pos, end - pos);
				pos = end + 1;
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				const auto tab = line.find('\t');
				if (line.empty() || line[0] != '$' || tab == std::string::npos) { continue; }
				g_tr[line.substr(0, tab)] = line.substr(tab + 1);
				++count;
			}
			logger::info("flick: {} lines of text from {}", count, a_path.string());
			return true;
		}

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		// "$KEY" (and "$KEY##id") through the mods' text; anything else as it is.
		const char* Tr(const char* a_text)
		{
			if (!a_text || a_text[0] != '$') { return a_text; }
			std::lock_guard lock(g_trLock);
			std::string key(a_text);
			std::string id;
			if (const auto hash = key.find("##"); hash != std::string::npos) { id = key.substr(hash); key.resize(hash); }
			const auto it = g_tr.find(key);
			if (it == g_tr.end()) { return a_text; }
			if (id.empty()) { return it->second.c_str(); }
			// a translated label keeps its "##id": stored once per distinct label, stable for the session
			static std::unordered_map<std::string, std::string> s_withId;
			auto& slot = s_withId[a_text];
			if (slot.empty()) { slot = it->second + id; }
			return slot.c_str();
		}

		// ---- the font tokens (FLICK's header reads ImFont::LegacySize - ImGui 1.92's field - from what GetFont returns) --
		struct FontToken
		{
			void*         lastBaked = nullptr;
			void*         ownerAtlas = nullptr;
			int           flags = 0;
			float         rasterizerDensity = 1.0f;
			std::uint32_t fontId = 0;
			float         legacySize = 16.0f;   // ImFont::LegacySize in 1.92
			char          rest[96]{};
		};
		static_assert(offsetof(FontToken, legacySize) == 28, "LegacySize sits at byte 28 of ImGui 1.92's ImFont");
		FontToken g_fontRegular, g_fontLarge;
		int g_fontPushes = 0;

		// ---- value translation, 1.92 -> 1.90.8 --------------------------------------------------------------------------
		int Col(int a_col) { return map::Index(a_col, map::kCol); }
		int Var(int a_var) { return map::Index(a_var, map::kStyleVar); }
		ImGuiKey Key(int a_key) { const int k = map::Index(a_key, map::kKey); return k < 0 ? ImGuiKey_None : static_cast<ImGuiKey>(k); }
		template <std::size_t N>
		int Fl(int a_flags, const map::FlagBit (&a_map)[N]) { return static_cast<int>(map::Flags(static_cast<std::uint32_t>(a_flags), a_map)); }

		// 1.90.8's style variables that are an ImVec2 (the rest are float) - a push of the wrong kind is refused by ImGui
		// and would unbalance the next pop.
		bool VarIsVec(int a_var)
		{
			switch (a_var)
			{
			case ImGuiStyleVar_WindowPadding: case ImGuiStyleVar_WindowMinSize: case ImGuiStyleVar_WindowTitleAlign:
			case ImGuiStyleVar_FramePadding: case ImGuiStyleVar_ItemSpacing: case ImGuiStyleVar_ItemInnerSpacing:
			case ImGuiStyleVar_CellPadding: case ImGuiStyleVar_TableAngledHeadersTextAlign: case ImGuiStyleVar_ButtonTextAlign:
			case ImGuiStyleVar_SelectableTextAlign: case ImGuiStyleVar_SeparatorTextAlign: case ImGuiStyleVar_SeparatorTextPadding:
				return true;
			default:
				return false;
			}
		}

		ImU32 U32(const ImVec4& a_c) { return ImGui::ColorConvertFloat4ToU32(a_c); }

		// ==== the slots ================================================================================================
		// Registration
		void RegisterTool(Tool* a_tool)
		{
			if (!a_tool) { return; }
			ToolRec rec;
			rec.tool = a_tool;
			const char* plugin = a_tool->PluginName();
			const char* name = a_tool->Name();
			const char* group = a_tool->Group();
			rec.plugin = plugin ? plugin : "";
			rec.name = name ? name : "";
			rec.group = group ? group : "";
			rec.listed = a_tool->ShowInSidebar();
			rec.dll = DllOf(a_tool);
			// entry = the group when the mod gave one, else the tool's name; " (FLICK)" on the end, as converted MCM menus
			// carry " (MCM)" - the source shown in the list (the owner, 2026-10-08: "(FLICK)")
			rec.entry = (rec.group.empty() ? rec.name : rec.group) + " (FLICK)";
			logger::info("flick: RegisterTool plugin={} ({}) name=\"{}\" group=\"{}\" sidebar={} -> entry \"{}\"", rec.plugin, rec.dll, rec.name,
						 rec.group, rec.listed, rec.entry);
			{
				std::lock_guard lock(g_lock);
				for (const auto& t : g_tools)
				{
					if (t.tool == a_tool) { logger::warn("flick: the same tool registered twice - ignored"); return; }
				}
				g_tools.push_back(rec);
			}
			if (!rec.listed) { return; }
			Tool* tool = a_tool;
			const std::string helpPage = "FLICK|" + rec.plugin + "|" + rec.name;
			registry::RegisterFn(rec.entry.c_str(), rec.name.empty() ? "Settings" : rec.name.c_str(), [tool, helpPage]() {
				// FLICK's "tool selected": the page that becomes the shown one gets OnOpen, the one it replaces OnClose
				g_drawnThisFrame = tool;
				if (g_active != tool)
				{
					if (g_active) { g_active->OnClose(); }
					g_active = tool;
					tool->OnOpen();
				}
				helpbar::Want(helpPage);   // a HelpMarker's text goes to the bar under the pane, as on converted MCM pages
				ImGui::PushID(tool);
				++g_inDraw;
				tool->Draw();
				--g_inDraw;
				ImGui::PopID();
			});
		}
		void RegisterWindow(Window* a_window)
		{
			if (!a_window) { return; }
			const char* id = a_window->Id();
			const char* plugin = a_window->PluginName();
			logger::info("flick: RegisterWindow plugin={} id={} - held, not drawn yet (free windows come in a later build)",
						 plugin ? plugin : "?", id ? id : "?");
			std::lock_guard lock(g_lock);
			g_windows.push_back(a_window);
		}
		void UnregisterWindow(Window* a_window)
		{
			std::lock_guard lock(g_lock);
			std::erase(g_windows, a_window);
		}

		// Display
		float GetResolutionScale() { const float h = ImGui::GetIO().DisplaySize.y; return h > 0.0f ? h / 1080.0f : 1.0f; }
		float GetUserScale() { return std::max(0.5f, settings::Get().textScale); }
		float GetGlobalScale() { return GetResolutionScale() * GetUserScale(); }
		void GetDisplaySize(float* a_w, float* a_h)
		{
			const ImVec2 s = ImGui::GetIO().DisplaySize;
			if (a_w) { *a_w = s.x; }
			if (a_h) { *a_h = s.y; }
		}
		void TranslateScaleformToScreen(float a_x, float a_y, float* a_ox, float* a_oy)
		{
			// the game's menus draw on a 1280x720 stage fitted to the screen's height, centred
			const ImVec2 s = ImGui::GetIO().DisplaySize;
			const float k = s.y / 720.0f;
			const float left = (s.x - 1280.0f * k) * 0.5f;
			if (a_ox) { *a_ox = left + a_x * k; }
			if (a_oy) { *a_oy = a_y * k; }
		}
		void* GetFont(int a_font)
		{
			g_fontRegular.legacySize = ImGui::GetFontSize();
			g_fontLarge.legacySize = ImGui::GetFontSize() * 1.4f;
			return a_font == 1 ? &g_fontLarge : &g_fontRegular;
		}
		void PushFont(void*, float)
		{
			// one face in AMF (the player's font); the push is kept so the pop stays balanced
			ImGui::PushFont(ImGui::GetFont());
			++g_fontPushes;
		}
		void PopFont()
		{
			if (g_fontPushes > 0) { ImGui::PopFont(); --g_fontPushes; }
		}
		void SuspendRendering(bool) { NoteOnce("SuspendRendering", "FLICK's own rendering does not exist under AMF; nothing to suspend"); }
		void SetMenuOpen(bool a_open)
		{
			logger::info("flick: SetMenuOpen({}) -> AMF's menu", a_open);
			renderer::SetMenuVisible(a_open);
		}
		bool IsMenuOpen() { return renderer::IsMainWindowVisible(); }

		// IO
		float GetDeltaTime() { return ImGui::GetIO().DeltaTime; }
		double GetTime() { return ImGui::GetTime(); }
		void GetMouseDelta(float* a_x, float* a_y) { const ImVec2 d = ImGui::GetIO().MouseDelta; if (a_x) { *a_x = d.x; } if (a_y) { *a_y = d.y; } }
		void GetMousePos(float* a_x, float* a_y) { const ImVec2 d = ImGui::GetIO().MousePos; if (a_x) { *a_x = d.x; } if (a_y) { *a_y = d.y; } }
		float GetMouseWheel() { return ImGui::GetIO().MouseWheel; }

		// Styling - every push lands as some push, so the mod's pops always balance
		void PushStyleColor(int a_col, const ImVec4& a_c)
		{
			const int c = Col(a_col);
			ImGui::PushStyleColor(c >= 0 ? c : ImGuiCol_Text, c >= 0 ? a_c : ImGui::GetStyleColorVec4(ImGuiCol_Text));
		}
		void PopStyleColor(int a_n) { ImGui::PopStyleColor(a_n); }
		void PushStyleVar(int a_var, float a_v)
		{
			const int v = Var(a_var);
			if (v >= 0 && !VarIsVec(v)) { ImGui::PushStyleVar(v, a_v); }
			else { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha); }
		}
		void PushStyleVarVec(int a_var, const ImVec2& a_v)
		{
			const int v = Var(a_var);
			if (v >= 0 && VarIsVec(v)) { ImGui::PushStyleVar(v, a_v); }
			else { ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha); }
		}
		void PopStyleVar(int a_n) { ImGui::PopStyleVar(a_n); }
		float GetStyleVar(int a_var)
		{
			const int v = Var(a_var);
			if (v < 0 || VarIsVec(v)) { return 0.0f; }
			const ImGuiStyle& s = ImGui::GetStyle();
			switch (v)
			{
			case ImGuiStyleVar_Alpha: return s.Alpha;
			case ImGuiStyleVar_DisabledAlpha: return s.DisabledAlpha;
			case ImGuiStyleVar_WindowRounding: return s.WindowRounding;
			case ImGuiStyleVar_WindowBorderSize: return s.WindowBorderSize;
			case ImGuiStyleVar_ChildRounding: return s.ChildRounding;
			case ImGuiStyleVar_ChildBorderSize: return s.ChildBorderSize;
			case ImGuiStyleVar_PopupRounding: return s.PopupRounding;
			case ImGuiStyleVar_PopupBorderSize: return s.PopupBorderSize;
			case ImGuiStyleVar_FrameRounding: return s.FrameRounding;
			case ImGuiStyleVar_FrameBorderSize: return s.FrameBorderSize;
			case ImGuiStyleVar_IndentSpacing: return s.IndentSpacing;
			case ImGuiStyleVar_ScrollbarSize: return s.ScrollbarSize;
			case ImGuiStyleVar_ScrollbarRounding: return s.ScrollbarRounding;
			case ImGuiStyleVar_GrabMinSize: return s.GrabMinSize;
			case ImGuiStyleVar_GrabRounding: return s.GrabRounding;
			case ImGuiStyleVar_TabRounding: return s.TabRounding;
			case ImGuiStyleVar_TabBorderSize: return s.TabBorderSize;
			case ImGuiStyleVar_TabBarBorderSize: return s.TabBarBorderSize;
			case ImGuiStyleVar_SeparatorTextBorderSize: return s.SeparatorTextBorderSize;
			default: return 0.0f;
			}
		}
		void GetStyleVarVec(int a_var, float* a_x, float* a_y)
		{
			const int v = Var(a_var);
			ImVec2 r(0.0f, 0.0f);
			const ImGuiStyle& s = ImGui::GetStyle();
			switch (v)
			{
			case ImGuiStyleVar_WindowPadding: r = s.WindowPadding; break;
			case ImGuiStyleVar_WindowMinSize: r = s.WindowMinSize; break;
			case ImGuiStyleVar_WindowTitleAlign: r = s.WindowTitleAlign; break;
			case ImGuiStyleVar_FramePadding: r = s.FramePadding; break;
			case ImGuiStyleVar_ItemSpacing: r = s.ItemSpacing; break;
			case ImGuiStyleVar_ItemInnerSpacing: r = s.ItemInnerSpacing; break;
			case ImGuiStyleVar_CellPadding: r = s.CellPadding; break;
			case ImGuiStyleVar_ButtonTextAlign: r = s.ButtonTextAlign; break;
			case ImGuiStyleVar_SelectableTextAlign: r = s.SelectableTextAlign; break;
			case ImGuiStyleVar_SeparatorTextAlign: r = s.SeparatorTextAlign; break;
			case ImGuiStyleVar_SeparatorTextPadding: r = s.SeparatorTextPadding; break;
			default: break;
			}
			if (a_x) { *a_x = r.x; }
			if (a_y) { *a_y = r.y; }
		}
		void GetStyleColorVec4(int a_col, float* a_r, float* a_g, float* a_b, float* a_a)
		{
			const int c = Col(a_col);
			const ImVec4 v = c >= 0 ? ImGui::GetStyleColorVec4(c) : ImVec4(1, 1, 1, 1);
			if (a_r) { *a_r = v.x; } if (a_g) { *a_g = v.y; } if (a_b) { *a_b = v.z; } if (a_a) { *a_a = v.w; }
		}
		void SetWindowFontScale(float a_s) { ImGui::SetWindowFontScale(a_s); }

		// Layout
		void SetCursorPosX(float a_x) { ImGui::SetCursorPosX(a_x); }
		void SetCursorPosY(float a_y) { ImGui::SetCursorPosY(a_y); }
		void GetCursorPos(float* a_x, float* a_y) { const ImVec2 p = ImGui::GetCursorPos(); if (a_x) { *a_x = p.x; } if (a_y) { *a_y = p.y; } }
		void SetCursorPos(float a_x, float a_y) { ImGui::SetCursorPos(ImVec2(a_x, a_y)); }
		void GetCursorScreenPos(float* a_x, float* a_y) { const ImVec2 p = ImGui::GetCursorScreenPos(); if (a_x) { *a_x = p.x; } if (a_y) { *a_y = p.y; } }
		void SetCursorScreenPos(float a_x, float a_y) { ImGui::SetCursorScreenPos(ImVec2(a_x, a_y)); }
		void AlignTextToFramePadding() { ImGui::AlignTextToFramePadding(); }
		void GetContentRegionAvail(float* a_x, float* a_y) { const ImVec2 p = ImGui::GetContentRegionAvail(); if (a_x) { *a_x = p.x; } if (a_y) { *a_y = p.y; } }
		float CalcItemWidth() { return ImGui::CalcItemWidth(); }
		void CalcTextSize(const char* a_t, const char* a_end, bool a_hide, float a_wrap, float* a_x, float* a_y)
		{
			const ImVec2 s = ImGui::CalcTextSize(a_t ? Tr(a_t) : "", a_t && a_t[0] == '$' ? nullptr : a_end, a_hide, a_wrap);
			if (a_x) { *a_x = s.x; }
			if (a_y) { *a_y = s.y; }
		}
		void GetItemRectMin(float* a_x, float* a_y) { const ImVec2 p = ImGui::GetItemRectMin(); if (a_x) { *a_x = p.x; } if (a_y) { *a_y = p.y; } }
		void GetItemRectMax(float* a_x, float* a_y) { const ImVec2 p = ImGui::GetItemRectMax(); if (a_x) { *a_x = p.x; } if (a_y) { *a_y = p.y; } }
		void SetNextItemWidth(float a_w) { ImGui::SetNextItemWidth(a_w); }
		void SetNextItemOpen(bool a_open, int a_cond) { ImGui::SetNextItemOpen(a_open, Fl(a_cond, map::kCond)); }
		void Dummy(float a_w, float a_h) { ImGui::Dummy(ImVec2(a_w, a_h)); }
		void Spacing() { ImGui::Spacing(); }
		void Separator() { ImGui::Separator(); }
		void SeparatorThick()
		{
			ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, std::max(2.0f, ImGui::GetStyle().SeparatorTextBorderSize * 2.0f));
			ImGui::SeparatorText("");
			ImGui::PopStyleVar();
		}
		void SeparatorText(const char* a_t) { ImGui::SeparatorText(Tr(a_t ? a_t : "")); }
		float GetColumnWidth(int a_i) { return ImGui::GetColumnWidth(a_i); }

		// Metrics
		float GetTextLineHeight() { return ImGui::GetTextLineHeight(); }
		float GetTextLineHeightWithSpacing() { return ImGui::GetTextLineHeightWithSpacing(); }
		float GetFrameHeight() { return ImGui::GetFrameHeight(); }
		float GetFrameHeightWithSpacing() { return ImGui::GetFrameHeightWithSpacing(); }

		// Utils
		void LoadTranslation(const char* a_plugin)
		{
			if (!a_plugin || !*a_plugin) { return; }
			std::lock_guard lock(g_trLock);
			if (!g_trLoaded.insert(a_plugin).second) { return; }
			// the mod's own text in the GAME's language (FLICK's behaviour - third-party text keeps its own language, rule 66's
			// exemption), English when the mod has no file for it
			std::string lang = Lower(strings::GameLanguageSetting());
			if (lang.empty()) { lang = "english"; }
			const auto dir = std::filesystem::path("Data/Interface/Translations");
			if (!ReadTranslationFile(dir / std::format("{}_{}.txt", a_plugin, lang)) && lang != "english")
			{
				ReadTranslationFile(dir / std::format("{}_english.txt", a_plugin));
			}
		}
		const char* GetTranslation(const char* a_key) { return a_key ? Tr(a_key) : ""; }
		void SanitizePath(char* a_out, const char* a_in, std::size_t a_size)
		{
			if (!a_out || a_size == 0) { return; }
			std::size_t i = 0;
			for (; a_in && a_in[i] && i + 1 < a_size; ++i)
			{
				const char c = a_in[i];
				a_out[i] = std::strchr("<>:\"/\\|?*", c) ? '_' : c;
			}
			a_out[i] = '\0';
		}
		void GetPluginConfigPath(const char* a_plugin, char* a_out, std::size_t a_size)
		{
			if (!a_out || a_size == 0) { return; }
			// the same place FLICK keeps a mod's settings, so a player's existing settings carry over
			const std::string p = std::format("Data\\FUCKs\\{}\\", a_plugin ? a_plugin : "");
			strncpy_s(a_out, a_size, p.c_str(), _TRUNCATE);
		}
		void LoadPluginINI(const char*, void*, void (*)(void*, void*)) { NoteOnce("LoadPluginINI", "the INI helpers come in a later build - the mod keeps its defaults"); }
		void SavePluginINI(const char*, void*, void (*)(void*, void*)) { NoteOnce("SavePluginINI", "the INI helpers come in a later build"); }
		void LoadPluginINIDefaults(const char*, void*, void (*)(void*, void*)) { NoteOnce("LoadPluginINIDefaults", "the INI helpers come in a later build"); }
		void LoadPluginKeybinds(const char*, void*, void (*)(void*, void*)) { NoteOnce("LoadPluginKeybinds", "the keybind helpers come in a later build"); }
		void SavePluginKeybinds(const char*, void*, void (*)(void*, void*)) { NoteOnce("SavePluginKeybinds", "the keybind helpers come in a later build"); }
		void LoadPluginKeybindsDefaults(const char*, void*, void (*)(void*, void*)) { NoteOnce("LoadPluginKeybindsDefaults", "the keybind helpers come in a later build"); }
		void PushItemFlag(int a_flag, bool a_on)
		{
			// FLICK's ItemFlags bits are 1.90.8's NoTabStop / ButtonRepeat / Disabled / NoNav, bit for bit
			ImGui::PushItemFlag(static_cast<ImGuiItemFlags>(a_flag & 0xF), a_on);
		}
		void PopItemFlag() { ImGui::PopItemFlag(); }
		void HelpMarker(const char* a_desc)
		{
			// AMF's look: the help of the item before it goes to the help bar (or its popup), as on converted MCM pages
			if (a_desc && *a_desc) { helpbar::OfferForLastItem(Tr(a_desc)); }
		}
		void PushID_Str(const char* a_id) { ImGui::PushID(a_id); }
		void PushID_Int(int a_id) { ImGui::PushID(a_id); }
		void PushID_Ptr(const void* a_id) { ImGui::PushID(a_id); }
		void PopID() { ImGui::PopID(); }

		// Menu events (held; fired in a later build)
		struct Listener { void* user; void (*fn)(const char*, bool, void*); };
		std::vector<Listener> g_menuListeners;
		void AddMenuListener(void* a_user, void (*a_fn)(const char*, bool, void*))
		{
			std::lock_guard lock(g_lock);
			g_menuListeners.push_back({ a_user, a_fn });
			NoteOnce("AddMenuListener", "held; menu events reach FLICK mods in a later build");
		}
		void RemoveMenuListener(void* a_user)
		{
			std::lock_guard lock(g_lock);
			std::erase_if(g_menuListeners, [a_user](const Listener& l) { return l.user == a_user; });
		}

		// Input - the takeover's default (plan section 6): a FLICK mod's keys do nothing during play; the input feed and
		// bindings come in a later build, so these answer "nothing pressed"
		bool IsInputPressed(const void*, std::uint32_t) { return false; }
		bool IsInputDown(std::uint32_t) { return false; }
		float GetAnalogInput(std::uint32_t) { return 0.0f; }
		bool IsModifierPressed(int a_mod)
		{
			const ImGuiIO& io = ImGui::GetIO();
			return a_mod == 0 ? io.KeyShift : a_mod == 1 ? io.KeyCtrl : a_mod == 2 ? io.KeyAlt : false;
		}
		int GetInputDevice() { return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) ? 1 : 0; }
		const char* GetKeyName(std::uint32_t) { NoteOnce("GetKeyName", "key names come in a later build"); return ""; }
		bool IsGamepadKey(std::uint32_t a_key) { return a_key >= 266; }   // SKSE's InputMap: the pad starts at kMacro_GamepadOffset
		bool IsBinding() { return false; }
		void AbortBinding() {}
		bool ProcessManagedHotkey(const void*, Hotkey*) { return false; }
		bool IsManagedHotkeyDown(Hotkey*) { return false; }

		// Interaction
		bool IsPopupOpen(const char* a_id, int a_flags) { return ImGui::IsPopupOpen(a_id, Fl(a_flags, map::kPopupFlags)); }
		bool IsItemHovered(int a_flags) { return ImGui::IsItemHovered(Fl(a_flags, map::kHoveredFlags)); }
		bool IsItemClicked(int a_button) { return ImGui::IsItemClicked(a_button); }
		bool IsItemActive() { return ImGui::IsItemActive(); }
		bool IsItemFocused() { return ImGui::IsItemFocused(); }
		bool IsItemDeactivated() { return ImGui::IsItemDeactivated(); }
		bool IsItemDeactivatedAfterEdit() { return ImGui::IsItemDeactivatedAfterEdit(); }
		bool IsAnyItemActive() { return ImGui::IsAnyItemActive(); }
		bool IsAnyItemHovered() { return ImGui::IsAnyItemHovered(); }
		bool IsWindowFocused(int a_flags) { return ImGui::IsWindowFocused(Fl(a_flags, map::kFocusedFlags)); }
		bool IsWindowHovered(int a_flags) { return ImGui::IsWindowHovered(Fl(a_flags, map::kHoveredFlags)); }
		bool IsMouseDown(int a_b) { return a_b >= 0 && a_b < 5 && ImGui::IsMouseDown(a_b); }
		bool IsMouseClicked(int a_b, bool a_repeat) { return a_b >= 0 && a_b < 5 && ImGui::IsMouseClicked(a_b, a_repeat); }
		bool IsMouseReleased(int a_b) { return a_b >= 0 && a_b < 5 && ImGui::IsMouseReleased(a_b); }
		bool IsKeyDown(int a_key) { const ImGuiKey k = Key(a_key); return k != ImGuiKey_None && ImGui::IsKeyDown(k); }
		bool IsKeyPressed(int a_key, bool a_repeat) { const ImGuiKey k = Key(a_key); return k != ImGuiKey_None && ImGui::IsKeyPressed(k, a_repeat); }
		void SetKeyboardFocusHere(int a_offset) { ImGui::SetKeyboardFocusHere(a_offset); }
		void SetItemDefaultFocus() { ImGui::SetItemDefaultFocus(); }

		bool BeginDragDropSource(int a_flags) { return ImGui::BeginDragDropSource(Fl(a_flags, map::kDragDropFlags)); }
		bool SetDragDropPayload(const char* a_type, const void* a_data, std::size_t a_size, int a_cond) { return ImGui::SetDragDropPayload(a_type, a_data, a_size, Fl(a_cond, map::kCond)); }
		void EndDragDropSource() { ImGui::EndDragDropSource(); }
		bool BeginDragDropTarget() { return ImGui::BeginDragDropTarget(); }
		const ImGuiPayload* AcceptDragDropPayload(const char* a_type, int a_flags) { return ImGui::AcceptDragDropPayload(a_type, Fl(a_flags, map::kDragDropFlags)); }
		void EndDragDropTarget() { ImGui::EndDragDropTarget(); }

		// Drawing - the current window's, the background and the foreground ("screen") draw lists
		void DrawRect(const ImVec2& a, const ImVec2& b, const ImVec4& c, float r, float t) { ImGui::GetWindowDrawList()->AddRect(a, b, U32(c), r, 0, t); }
		void DrawRectFilled(const ImVec2& a, const ImVec2& b, const ImVec4& c, float r) { ImGui::GetWindowDrawList()->AddRectFilled(a, b, U32(c), r); }
		void DrawLine(const ImVec2& a, const ImVec2& b, const ImVec4& c, float t) { ImGui::GetWindowDrawList()->AddLine(a, b, U32(c), t); }
		void DrawBackgroundLine(float x0, float y0, float x1, float y1, unsigned int c, float t) { ImGui::GetBackgroundDrawList()->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), c, t); }
		void DrawBackgroundRect(const ImVec2& a, const ImVec2& b, ImU32 c, float t) { ImGui::GetBackgroundDrawList()->AddRect(a, b, c, 0.0f, 0, t); }
		void DrawScreenRect(const ImVec2& a, const ImVec2& b, ImU32 c, float r, float t) { ImGui::GetForegroundDrawList()->AddRect(a, b, c, r, 0, t); }
		void DrawScreenRectFilled(const ImVec2& a, const ImVec2& b, ImU32 c, float r) { ImGui::GetForegroundDrawList()->AddRectFilled(a, b, c, r); }
		void DrawScreenLine(float x0, float y0, float x1, float y1, ImU32 c, float t) { ImGui::GetForegroundDrawList()->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), c, t); }

		// Windows - a raw window only inside a draw AMF is running (the takeover, plan section 6)
		std::vector<bool> g_begun;
		void SetNextWindowPos(float x, float y, int cond, float px, float py) { ImGui::SetNextWindowPos(ImVec2(x, y), Fl(cond, map::kCond), ImVec2(px, py)); }
		void SetNextWindowSize(float w, float h, int cond) { ImGui::SetNextWindowSize(ImVec2(w, h), Fl(cond, map::kCond)); }
		void GetWindowPos(float* x, float* y) { const ImVec2 p = ImGui::GetWindowPos(); if (x) { *x = p.x; } if (y) { *y = p.y; } }
		void GetWindowSize(float* x, float* y) { const ImVec2 p = ImGui::GetWindowSize(); if (x) { *x = p.x; } if (y) { *y = p.y; } }
		void SetWindowPos(float x, float y, int cond) { ImGui::SetWindowPos(ImVec2(x, y), Fl(cond, map::kCond)); }
		void SetWindowSize(float w, float h, int cond) { ImGui::SetWindowSize(ImVec2(w, h), Fl(cond, map::kCond)); }
		bool BeginWindow(const char* a_name, bool* a_open, int a_flags)
		{
			if (g_inDraw <= 0)
			{
				NoteOnce("BeginWindow outside a page", "a FLICK window opens only from a page AMF is drawing");
				g_begun.push_back(false);
				return false;
			}
			g_begun.push_back(true);
			return ImGui::Begin(Tr(a_name ? a_name : "##flick"), a_open, Fl(a_flags, map::kWindowFlags));
		}
		void EndWindow()
		{
			if (g_begun.empty()) { return; }
			const bool begun = g_begun.back();
			g_begun.pop_back();
			if (begun) { ImGui::End(); }
		}
		void ExtendWindowPastBorder() {}
		void BeginChild(const char* a_id, float a_w, float a_h, bool a_border, int a_flags)
		{
			ImGui::BeginChild(a_id ? a_id : "##flickchild", ImVec2(a_w, a_h), a_border ? ImGuiChildFlags_Border : ImGuiChildFlags_None,
							  Fl(a_flags, map::kWindowFlags));
		}
		void EndChild() { ImGui::EndChild(); }
		bool TreeNode(const char* a_label) { return ImGui::TreeNode(Tr(a_label)); }
		void TreePop() { ImGui::TreePop(); }
		bool BeginPopupContextItem(const char* a_id, int a_flags) { return ImGui::BeginPopupContextItem(a_id, Fl(a_flags, map::kPopupFlags)); }
		void EndPopup() { ImGui::EndPopup(); }

		// Widgets - AMF's look: on/off as AMF's switch (rule 32), sliders precise (rule 68), dropdowns tight
		bool Button(const char* a_label) { const bool r = ImGui::Button(Tr(a_label)); Note("button", a_label, r); return r; }
		bool InvisibleButton(const char* a_id, const ImVec2& a_size, int a_flags) { return ImGui::InvisibleButton(a_id, a_size, Fl(a_flags, map::kButtonFlags)); }
		bool Checkbox(const char* a_label, bool* a_v, bool, bool)
		{
			if (!a_v) { return false; }
			// one group, so the next HelpMarker / IsItemHovered sees the switch and its label as one item - AMF's switch
			// submits its label last, and help offered "for the last item" answered only over the words (2026-10-08 test)
			ImGui::BeginGroup();
			const bool r = widgets::Toggle(Tr(a_label), a_v);
			ImGui::EndGroup();
			Note("switch", a_label, *a_v);
			return r;
		}
		bool ToggleButton(const char* a_label, bool* a_v, bool, bool) { return Checkbox(a_label, a_v, true, true); }
		bool Hotkey(const char* a_label, std::uint32_t, std::int32_t, std::int32_t, bool, bool, bool)
		{
			NoteOnce("Hotkey", "key display comes in a later build");
			ImGui::TextUnformatted(Tr(a_label));
			return false;
		}
		bool InputText(const char* a_label, char* a_buf, std::size_t a_size, int a_flags) { return a_buf && ImGui::InputText(Tr(a_label), a_buf, a_size, Fl(a_flags, map::kInputTextFlags)); }
		bool ColorEdit3(const char* a_label, float* a_c, int a_flags) { return a_c && ImGui::ColorEdit3(Tr(a_label), a_c, Fl(a_flags, map::kColorEditFlags)); }
		bool ColorEdit4(const char* a_label, float* a_c, int a_flags) { return a_c && ImGui::ColorEdit4(Tr(a_label), a_c, Fl(a_flags, map::kColorEditFlags)); }
		bool SliderFloat(const char* a_label, float* a_v, float a_min, float a_max, const char* a_fmt)
		{
			if (!a_v) { return false; }
			const bool r = precise::SliderFloat(Tr(a_label), a_v, a_min, a_max, a_fmt ? a_fmt : "%.3f");
			Note("slider", (std::string(a_label ? a_label : "") + "=" + std::format("{:.3f}", *a_v)).c_str());
			return r;
		}
		bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_fmt)
		{
			if (!a_v) { return false; }
			const bool r = precise::SliderInt(Tr(a_label), a_v, a_min, a_max, a_fmt ? a_fmt : "%d");
			Note("slider", (std::string(a_label ? a_label : "") + "=" + std::to_string(*a_v)).c_str());
			return r;
		}
		bool DragInt(const char* a_label, int* a_v, float a_speed, int a_min, int a_max, const char* a_fmt) { return a_v && ImGui::DragInt(Tr(a_label), a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%d"); }
		bool DragFloat(const char* a_label, float* a_v, float a_speed, float a_min, float a_max, const char* a_fmt) { return a_v && ImGui::DragFloat(Tr(a_label), a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%.3f"); }
		bool DragFloat2(const char* a_label, float* a_v, float a_speed, float a_min, float a_max, const char* a_fmt) { return a_v && ImGui::DragFloat2(Tr(a_label), a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%.3f"); }
		bool DragFloat3(const char* a_label, float* a_v, float a_speed, float a_min, float a_max, const char* a_fmt) { return a_v && ImGui::DragFloat3(Tr(a_label), a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%.3f"); }
		bool DragFloat4(const char* a_label, float* a_v, float a_speed, float a_min, float a_max, const char* a_fmt) { return a_v && ImGui::DragFloat4(Tr(a_label), a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%.3f"); }
		bool Combo(const char* a_label, int* a_cur, const char* const* a_items, int a_count) { return a_cur && a_items && theme::ComboTight(Tr(a_label), a_cur, a_items, a_count); }
		bool ComboWithFilter(const char* a_label, int* a_cur, const char* const* a_items, int a_count, int) { return Combo(a_label, a_cur, a_items, a_count); }
		bool ComboForm(const char* a_label, std::uint32_t*, std::uint8_t) { NoteOnce("ComboForm", "form lists come in a later build"); ImGui::TextUnformatted(Tr(a_label)); return false; }
		bool ComboFormStr(const char* a_label, char*, std::size_t, std::uint8_t) { NoteOnce("ComboFormStr", "form lists come in a later build"); ImGui::TextUnformatted(Tr(a_label)); return false; }
		bool Selectable(const char* a_label, bool a_sel, int a_flags, const ImVec2& a_size) { return ImGui::Selectable(Tr(a_label), a_sel, Fl(a_flags, map::kSelectableFlags), a_size); }
		ImGuiTableSortSpecs* GetTableSortSpecs() { return ImGui::TableGetSortSpecs(); }
		void Header(const char* a_t) { ImGui::SeparatorText(Tr(a_t ? a_t : "")); }
		void LeftLabel(const char* a_t) { ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(Tr(a_t ? a_t : "")); ImGui::SameLine(); }
		void TextColored(const ImVec4& a_c, const char* a_t) { ImGui::PushStyleColor(ImGuiCol_Text, a_c); ImGui::TextUnformatted(Tr(a_t ? a_t : "")); ImGui::PopStyleColor(); }
		void TextColoredWrapped(const ImVec4& a_c, const char* a_t) { ImGui::PushStyleColor(ImGuiCol_Text, a_c); ImGui::TextWrapped("%s", Tr(a_t ? a_t : "")); ImGui::PopStyleColor(); }
		void TextDisabled(const char* a_t) { ImGui::TextDisabled("%s", Tr(a_t ? a_t : "")); }
		void CenteredText(const char* a_t, bool)
		{
			const char* t = Tr(a_t ? a_t : "");
			const float w = ImGui::CalcTextSize(t).x;
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, (ImGui::GetContentRegionAvail().x - w) * 0.5f));
			ImGui::TextUnformatted(t);
		}
		void CenteredTextWithArrows(const char* a_label, const char* a_text, bool* a_left, bool* a_right, bool*)
		{
			// the arrows as two small buttons either side of the text
			if (a_left) { *a_left = false; }
			if (a_right) { *a_right = false; }
			ImGui::PushID(a_label ? a_label : "##arrows");
			if (ImGui::ArrowButton("##l", ImGuiDir_Left) && a_left) { *a_left = true; }
			ImGui::SameLine();
			ImGui::TextUnformatted(Tr(a_text ? a_text : ""));
			ImGui::SameLine();
			if (ImGui::ArrowButton("##r", ImGuiDir_Right) && a_right) { *a_right = true; }
			ImGui::PopID();
		}
		bool ButtonIconWithLabel(const char* a_label, void*, float, float, bool, bool) { return ImGui::Button(Tr(a_label)); }
		void Stepper(const char* a_label, const char* a_text, bool* a_left, bool* a_right) { CenteredTextWithArrows(a_label, a_text, a_left, a_right, nullptr); }

		bool BeginTabBar(const char* a_id, int a_flags) { return ImGui::BeginTabBar(a_id, Fl(a_flags, map::kTabBarFlags)); }
		void EndTabBar() { ImGui::EndTabBar(); }
		bool BeginTabItem(const char* a_label, int a_flags) { return ImGui::BeginTabItem(Tr(a_label), nullptr, Fl(a_flags, map::kTabItemFlags)); }
		void EndTabItem() { ImGui::EndTabItem(); }
		bool BeginTable(const char* a_id, int a_cols, int a_flags, const ImVec2& a_outer, float a_inner) { return a_cols > 0 && ImGui::BeginTable(a_id, a_cols, Fl(a_flags, map::kTableFlags), a_outer, a_inner); }
		void EndTable() { ImGui::EndTable(); }
		void TableSetupColumn(const char* a_label, int a_flags, float a_w, std::uint32_t a_id) { ImGui::TableSetupColumn(Tr(a_label), Fl(a_flags, map::kTableColumnFlags), a_w, a_id); }
		void TableNextRow(int a_flags, float a_h) { ImGui::TableNextRow(Fl(a_flags, map::kTableRowFlags), a_h); }
		bool TableNextColumn() { return ImGui::TableNextColumn(); }
		void TableHeadersRow() { ImGui::TableHeadersRow(); }
		void TableSetBgColor(int a_target, ImU32 a_c, int a_col) { if (a_target >= 1 && a_target <= 3) { ImGui::TableSetBgColor(a_target, a_c, a_col); } }
		void Columns(int a_n, const char* a_id, bool a_border) { ImGui::Columns(a_n, a_id, a_border); }
		void NextColumn() { ImGui::NextColumn(); }
		void SameLine(float a_off, float a_sp) { ImGui::SameLine(a_off, a_sp); }
		bool CollapsingHeader(const char* a_label, int a_flags)
		{
			const bool open = ImGui::CollapsingHeader(Tr(a_label), Fl(a_flags, map::kTreeNodeFlags));
			Note("header", a_label, open);
			return open;
		}
		void BeginGroup() { ImGui::BeginGroup(); }
		void EndGroup() { ImGui::EndGroup(); }
		void BeginDisabled(bool a_d) { ImGui::BeginDisabled(a_d); }
		void EndDisabled() { ImGui::EndDisabled(); }
		bool IsWidgetFocused(const char* a_id) { return a_id && ImGui::GetFocusID() == ImGui::GetID(a_id); }
		void SetTooltip(const char* a_t) { ImGui::SetTooltip("%s", Tr(a_t ? a_t : "")); }
		void Indent(float a_w) { ImGui::Indent(a_w); }
		void Unindent(float a_w) { ImGui::Unindent(a_w); }
		void Text(const char* a_t) { ImGui::TextUnformatted(Tr(a_t ? a_t : "")); }
		void TextWrapped(const char* a_t) { ImGui::TextWrapped("%s", Tr(a_t ? a_t : "")); }
		void TextUnformatted(const char* a_t, const char* a_end) { ImGui::TextUnformatted(a_t ? a_t : "", a_end); }

		// v2
		void SeparatorVertical() { ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical); }
		void PushItemWidth(float a_w) { ImGui::PushItemWidth(a_w); }
		void PopItemWidth() { ImGui::PopItemWidth(); }
		bool BeginTooltip() { return ImGui::BeginTooltip(); }
		void EndTooltip() { ImGui::EndTooltip(); }
		void SetScrollHereY(float a_r) { ImGui::SetScrollHereY(a_r); }
		bool InputTextMultiline(const char* a_label, char* a_buf, std::size_t a_size, const ImVec2& a_box, int a_flags)
		{
			return a_buf && ImGui::InputTextMultiline(Tr(a_label), a_buf, a_size, a_box, Fl(a_flags, map::kInputTextFlags));
		}

		// v3
		void SetHotkeyEnabled(bool) { NoteOnce("SetHotkeyEnabled", "there is no FLICK key under AMF; AMF's own key is untouched"); }
		void SetWindowFocus() { ImGui::SetWindowFocus(); }
		void CloseCurrentPopup() { ImGui::CloseCurrentPopup(); }
		void OpenPopup(const char* a_id, int a_flags) { ImGui::OpenPopup(a_id, Fl(a_flags, map::kPopupFlags)); }
		bool BeginPopup(const char* a_id, int) { return ImGui::BeginPopup(a_id); }
		bool BeginPopupModal(const char* a_id, bool* a_open, int) { return ImGui::BeginPopupModal(Tr(a_id), a_open); }
		bool IsWindowAppearing() { return ImGui::IsWindowAppearing(); }
		void PushTextWrapPos(float a_x) { ImGui::PushTextWrapPos(a_x); }
		void PopTextWrapPos() { ImGui::PopTextWrapPos(); }
		void SetNavCursorVisible(bool a_v) { GImGui->NavDisableHighlight = !a_v; }
		void DrawCircle(const ImVec2& c, float r, const ImVec4& col, int seg, float t) { ImGui::GetWindowDrawList()->AddCircle(c, r, U32(col), seg, t); }
		void DrawCircleFilled(const ImVec2& c, float r, const ImVec4& col, int seg) { ImGui::GetWindowDrawList()->AddCircleFilled(c, r, U32(col), seg); }
		void DrawScreenCircle(const ImVec2& c, float r, ImU32 col, int seg, float t) { ImGui::GetForegroundDrawList()->AddCircle(c, r, col, seg, t); }
		void DrawScreenCircleFilled(const ImVec2& c, float r, ImU32 col, int seg) { ImGui::GetForegroundDrawList()->AddCircleFilled(c, r, col, seg); }
		void DrawQuad(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, const ImVec4& col, float t) { ImGui::GetWindowDrawList()->AddQuad(a, b, c, d, U32(col), t); }
		void DrawQuadFilled(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, const ImVec4& col) { ImGui::GetWindowDrawList()->AddQuadFilled(a, b, c, d, U32(col)); }
		void DrawScreenQuad(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, ImU32 col, float t) { ImGui::GetForegroundDrawList()->AddQuad(a, b, c, d, col, t); }
		void DrawScreenQuadFilled(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec2& d, ImU32 col) { ImGui::GetForegroundDrawList()->AddQuadFilled(a, b, c, d, col); }
		void DrawTriangle(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec4& col, float t) { ImGui::GetWindowDrawList()->AddTriangle(a, b, c, U32(col), t); }
		void DrawTriangleFilled(const ImVec2& a, const ImVec2& b, const ImVec2& c, const ImVec4& col) { ImGui::GetWindowDrawList()->AddTriangleFilled(a, b, c, U32(col)); }
		void DrawScreenTriangle(const ImVec2& a, const ImVec2& b, const ImVec2& c, ImU32 col, float t) { ImGui::GetForegroundDrawList()->AddTriangle(a, b, c, col, t); }
		void DrawScreenTriangleFilled(const ImVec2& a, const ImVec2& b, const ImVec2& c, ImU32 col) { ImGui::GetForegroundDrawList()->AddTriangleFilled(a, b, c, col); }
		bool TreeNodeEx(const char* a_label, int a_flags) { return ImGui::TreeNodeEx(Tr(a_label), Fl(a_flags, map::kTreeNodeFlags)); }

		// v4
		void TableSetupScrollFreeze(int a_c, int a_r) { ImGui::TableSetupScrollFreeze(a_c, a_r); }
		void TableSetColumnIndex(int a_i) { ImGui::TableSetColumnIndex(a_i); }
		int TableGetColumnIndex() { return ImGui::TableGetColumnIndex(); }
		int TableGetRowIndex() { return ImGui::TableGetRowIndex(); }
		int TableGetColumnCount() { return ImGui::TableGetColumnCount(); }
		float GetScrollX() { return ImGui::GetScrollX(); }
		float GetScrollY() { return ImGui::GetScrollY(); }
		float GetScrollMaxX() { return ImGui::GetScrollMaxX(); }
		float GetScrollMaxY() { return ImGui::GetScrollMaxY(); }
		void SetScrollX(float a_v) { ImGui::SetScrollX(a_v); }
		void SetScrollY(float a_v) { ImGui::SetScrollY(a_v); }
		bool SliderAngle(const char* a_label, float* a_rad, float a_min, float a_max, const char* a_fmt) { return a_rad && ImGui::SliderAngle(Tr(a_label), a_rad, a_min, a_max, a_fmt ? a_fmt : "%.0f deg"); }
		bool VSliderFloat(const char* a_label, const ImVec2& a_size, float* a_v, float a_min, float a_max, const char* a_fmt) { return a_v && ImGui::VSliderFloat(Tr(a_label), a_size, a_v, a_min, a_max, a_fmt ? a_fmt : "%.3f"); }
		bool IsPluginWindowOpen(const char*, const char*) { return false; }

		// v5: AMF's sliders never multiply a nudge on the bumpers (rule 68), so there is nothing to switch off
		void PushGamepadTweakFastDisabled() {}
		void PopGamepadTweakFastDisabled() {}
		void PushGamepadTweakSlowDisabled() {}
		void PopGamepadTweakSlowDisabled() {}

		// ---- every other slot: a safe default, logged once (generated from the slot list) ----------------------------
#define FLICK_SLOT(ret, name, params) \
		ret Stub_##name params { NoteOnce(#name, "not answered in this build - a safe default is returned"); return Zero<ret>(); }
#include "FlickSlots.inc"
#undef FLICK_SLOT

		// ---- the table ----------------------------------------------------------------------------------------------
#pragma pack(push, 1)
		struct Interface
		{
			unsigned int version;
#define FLICK_SLOT(ret, name, params) ret(*name) params;
#include "FlickSlots.inc"
#undef FLICK_SLOT
		};
#pragma pack(pop)
		static_assert(sizeof(Interface) == 4 + FLICK_SLOT_COUNT * sizeof(void*), "FLICK's table: a 4-byte version, then one pointer per slot");
		static_assert(offsetof(Interface, RegisterTool) == 4, "the first slot follows the version with no padding (pack 1)");

		Interface MakeTable()
		{
			Interface t{};
			t.version = FLICK_API_VERSION_ANSWERED;
			// every slot starts at its logged default, so none is ever null...
#define FLICK_SLOT(ret, name, params) t.name = &Stub_##name;
#include "FlickSlots.inc"
#undef FLICK_SLOT
			// ...and the ones AMF does are answered for real
#define FLICK_DO(name) t.name = &name;
			FLICK_DO(RegisterTool) FLICK_DO(RegisterWindow) FLICK_DO(UnregisterWindow)
			FLICK_DO(GetResolutionScale) FLICK_DO(GetGlobalScale) FLICK_DO(GetUserScale) FLICK_DO(GetDisplaySize)
			FLICK_DO(TranslateScaleformToScreen) FLICK_DO(GetFont) FLICK_DO(PushFont) FLICK_DO(PopFont)
			FLICK_DO(SuspendRendering) FLICK_DO(SetMenuOpen) FLICK_DO(IsMenuOpen)
			FLICK_DO(GetDeltaTime) FLICK_DO(GetTime) FLICK_DO(GetMouseDelta) FLICK_DO(GetMousePos) FLICK_DO(GetMouseWheel)
			FLICK_DO(PushStyleColor) FLICK_DO(PopStyleColor) FLICK_DO(PushStyleVar) FLICK_DO(PushStyleVarVec) FLICK_DO(PopStyleVar)
			FLICK_DO(GetStyleVar) FLICK_DO(GetStyleVarVec) FLICK_DO(GetStyleColorVec4) FLICK_DO(SetWindowFontScale)
			FLICK_DO(SetCursorPosX) FLICK_DO(SetCursorPosY) FLICK_DO(GetCursorPos) FLICK_DO(SetCursorPos) FLICK_DO(GetCursorScreenPos)
			FLICK_DO(SetCursorScreenPos) FLICK_DO(AlignTextToFramePadding) FLICK_DO(GetContentRegionAvail) FLICK_DO(CalcItemWidth)
			FLICK_DO(CalcTextSize) FLICK_DO(GetItemRectMin) FLICK_DO(GetItemRectMax) FLICK_DO(SetNextItemWidth) FLICK_DO(SetNextItemOpen)
			FLICK_DO(Dummy) FLICK_DO(Spacing) FLICK_DO(Separator) FLICK_DO(SeparatorThick) FLICK_DO(SeparatorText) FLICK_DO(GetColumnWidth)
			FLICK_DO(GetTextLineHeight) FLICK_DO(GetTextLineHeightWithSpacing) FLICK_DO(GetFrameHeight) FLICK_DO(GetFrameHeightWithSpacing)
			FLICK_DO(LoadTranslation) FLICK_DO(GetTranslation) FLICK_DO(SanitizePath) FLICK_DO(GetPluginConfigPath)
			FLICK_DO(LoadPluginINI) FLICK_DO(SavePluginINI) FLICK_DO(LoadPluginINIDefaults)
			FLICK_DO(LoadPluginKeybinds) FLICK_DO(SavePluginKeybinds) FLICK_DO(LoadPluginKeybindsDefaults)
			FLICK_DO(PushItemFlag) FLICK_DO(PopItemFlag) FLICK_DO(HelpMarker) FLICK_DO(PushID_Str) FLICK_DO(PushID_Int) FLICK_DO(PushID_Ptr) FLICK_DO(PopID)
			FLICK_DO(AddMenuListener) FLICK_DO(RemoveMenuListener)
			FLICK_DO(IsInputPressed) FLICK_DO(IsInputDown) FLICK_DO(GetAnalogInput) FLICK_DO(IsModifierPressed) FLICK_DO(GetInputDevice)
			FLICK_DO(GetKeyName) FLICK_DO(IsGamepadKey) FLICK_DO(IsBinding) FLICK_DO(AbortBinding)
			FLICK_DO(ProcessManagedHotkey) FLICK_DO(IsManagedHotkeyDown)
			FLICK_DO(IsPopupOpen) FLICK_DO(IsItemHovered) FLICK_DO(IsItemClicked) FLICK_DO(IsItemActive) FLICK_DO(IsItemFocused)
			FLICK_DO(IsItemDeactivated) FLICK_DO(IsItemDeactivatedAfterEdit) FLICK_DO(IsAnyItemActive) FLICK_DO(IsAnyItemHovered)
			FLICK_DO(IsWindowFocused) FLICK_DO(IsWindowHovered) FLICK_DO(IsMouseDown) FLICK_DO(IsMouseClicked) FLICK_DO(IsMouseReleased)
			FLICK_DO(IsKeyDown) FLICK_DO(IsKeyPressed) FLICK_DO(SetKeyboardFocusHere) FLICK_DO(SetItemDefaultFocus)
			FLICK_DO(BeginDragDropSource) FLICK_DO(SetDragDropPayload) FLICK_DO(EndDragDropSource) FLICK_DO(BeginDragDropTarget)
			FLICK_DO(AcceptDragDropPayload) FLICK_DO(EndDragDropTarget)
			FLICK_DO(DrawRect) FLICK_DO(DrawRectFilled) FLICK_DO(DrawLine) FLICK_DO(DrawBackgroundLine) FLICK_DO(DrawBackgroundRect)
			FLICK_DO(DrawScreenRect) FLICK_DO(DrawScreenRectFilled) FLICK_DO(DrawScreenLine)
			FLICK_DO(SetNextWindowPos) FLICK_DO(SetNextWindowSize) FLICK_DO(GetWindowPos) FLICK_DO(GetWindowSize) FLICK_DO(SetWindowPos)
			FLICK_DO(SetWindowSize) FLICK_DO(BeginWindow) FLICK_DO(EndWindow) FLICK_DO(ExtendWindowPastBorder) FLICK_DO(BeginChild)
			FLICK_DO(EndChild) FLICK_DO(TreeNode) FLICK_DO(TreePop) FLICK_DO(BeginPopupContextItem) FLICK_DO(EndPopup)
			FLICK_DO(Button) FLICK_DO(InvisibleButton) FLICK_DO(Checkbox) FLICK_DO(Hotkey) FLICK_DO(ToggleButton) FLICK_DO(InputText)
			FLICK_DO(ColorEdit3) FLICK_DO(ColorEdit4) FLICK_DO(SliderFloat) FLICK_DO(SliderInt) FLICK_DO(DragInt) FLICK_DO(DragFloat)
			FLICK_DO(DragFloat2) FLICK_DO(DragFloat3) FLICK_DO(DragFloat4) FLICK_DO(Combo) FLICK_DO(ComboWithFilter) FLICK_DO(ComboForm)
			FLICK_DO(ComboFormStr) FLICK_DO(Selectable) FLICK_DO(GetTableSortSpecs) FLICK_DO(Header) FLICK_DO(LeftLabel)
			FLICK_DO(TextColored) FLICK_DO(TextColoredWrapped) FLICK_DO(TextDisabled) FLICK_DO(CenteredText) FLICK_DO(CenteredTextWithArrows)
			FLICK_DO(ButtonIconWithLabel) FLICK_DO(Stepper)
			FLICK_DO(BeginTabBar) FLICK_DO(EndTabBar) FLICK_DO(BeginTabItem) FLICK_DO(EndTabItem)
			FLICK_DO(BeginTable) FLICK_DO(EndTable) FLICK_DO(TableSetupColumn) FLICK_DO(TableNextRow) FLICK_DO(TableNextColumn)
			FLICK_DO(TableHeadersRow) FLICK_DO(TableSetBgColor) FLICK_DO(Columns) FLICK_DO(NextColumn) FLICK_DO(SameLine)
			FLICK_DO(CollapsingHeader) FLICK_DO(BeginGroup) FLICK_DO(EndGroup) FLICK_DO(BeginDisabled) FLICK_DO(EndDisabled)
			FLICK_DO(IsWidgetFocused) FLICK_DO(SetTooltip) FLICK_DO(Indent) FLICK_DO(Unindent) FLICK_DO(Text) FLICK_DO(TextWrapped)
			FLICK_DO(TextUnformatted)
			FLICK_DO(SeparatorVertical) FLICK_DO(PushItemWidth) FLICK_DO(PopItemWidth) FLICK_DO(BeginTooltip) FLICK_DO(EndTooltip)
			FLICK_DO(SetScrollHereY) FLICK_DO(InputTextMultiline)
			FLICK_DO(SetHotkeyEnabled) FLICK_DO(SetWindowFocus) FLICK_DO(CloseCurrentPopup) FLICK_DO(OpenPopup) FLICK_DO(BeginPopup)
			FLICK_DO(BeginPopupModal) FLICK_DO(IsWindowAppearing) FLICK_DO(PushTextWrapPos) FLICK_DO(PopTextWrapPos)
			FLICK_DO(SetNavCursorVisible) FLICK_DO(DrawCircle) FLICK_DO(DrawCircleFilled) FLICK_DO(DrawScreenCircle)
			FLICK_DO(DrawScreenCircleFilled) FLICK_DO(DrawQuad) FLICK_DO(DrawQuadFilled) FLICK_DO(DrawScreenQuad)
			FLICK_DO(DrawScreenQuadFilled) FLICK_DO(DrawTriangle) FLICK_DO(DrawTriangleFilled) FLICK_DO(DrawScreenTriangle)
			FLICK_DO(DrawScreenTriangleFilled) FLICK_DO(TreeNodeEx)
			FLICK_DO(TableSetupScrollFreeze) FLICK_DO(TableSetColumnIndex) FLICK_DO(TableGetColumnIndex) FLICK_DO(TableGetRowIndex)
			FLICK_DO(TableGetColumnCount) FLICK_DO(GetScrollX) FLICK_DO(GetScrollY) FLICK_DO(GetScrollMaxX) FLICK_DO(GetScrollMaxY)
			FLICK_DO(SetScrollX) FLICK_DO(SetScrollY) FLICK_DO(SliderAngle) FLICK_DO(VSliderFloat) FLICK_DO(IsPluginWindowOpen)
			FLICK_DO(PushGamepadTweakFastDisabled) FLICK_DO(PopGamepadTweakFastDisabled) FLICK_DO(PushGamepadTweakSlowDisabled)
			FLICK_DO(PopGamepadTweakSlowDisabled)
#undef FLICK_DO
			// the null-slot check (logic library 49): walked once at load; a null would be logged loudly
			const auto* slots = reinterpret_cast<const unsigned char*>(&t) + 4;
			for (int i = 0; i < FLICK_SLOT_COUNT; ++i)
			{
				void* p = nullptr;
				std::memcpy(&p, slots + i * sizeof(void*), sizeof(void*));
				if (!p) { logger::critical("flick: table slot {} is EMPTY - a FLICK mod calling it would crash", i); }
			}
			return t;
		}

		Interface& Table()
		{
			static Interface s_table = MakeTable();
			return s_table;
		}
		std::atomic<std::size_t> g_requests{ 0 };
	}

	void Configure(bool a_host)
	{
		g_host.store(a_host, std::memory_order_release);
		std::error_code ec;
		g_realFlick = std::filesystem::exists("Data/SKSE/Plugins/FUCK.dll", ec);
		logger::info("flick: host {} ({} slots, API version {}){}", a_host ? "ON" : "off", FLICK_SLOT_COUNT, FLICK_API_VERSION_ANSWERED,
					 g_realFlick ? " - the real FLICK (FUCK.dll) is installed too: AMF holds every FLICK mod not left to it (Settings > Converted menus > FLICK)" : "");
		(void)Table();   // build the table now (it is also built on first request)
		{
			std::scoped_lock lock(g_choiceLock);
			std::ifstream in(kLeftToFlickPath, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) { line.pop_back(); }
				const auto start = line.find_first_not_of(" \t");
				if (start == std::string::npos || line[start] == '#') { continue; }
				std::string dll = line.substr(start);
				std::string name;
				if (const auto eq = dll.find('='); eq != std::string::npos)
				{
					name = dll.substr(eq + 1);
					dll.resize(eq);
					while (!dll.empty() && (dll.back() == ' ' || dll.back() == '\t')) { dll.pop_back(); }
					const auto n = name.find_first_not_of(" \t");
					name = n == std::string::npos ? std::string{} : name.substr(n);
				}
				std::transform(dll.begin(), dll.end(), dll.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				g_leftAtStart.insert(dll);
				if (!name.empty()) { g_names[dll] = name; }
			}
			g_leftNow = g_leftAtStart;
			for (const auto& dll : g_leftAtStart) { logger::info("flick: {} is left to the real FLICK (the player's choice){}", dll, g_realFlick ? "" : " - FLICK is not installed, so its page is shown nowhere"); }
		}
	}

	bool LeftToFlick(const std::string& a_dllLower)
	{
		std::scoped_lock lock(g_choiceLock);
		return g_leftAtStart.contains(a_dllLower);
	}

	void SetLeftToFlick(const std::string& a_dllLower, bool a_on, const std::string& a_shownName)
	{
		std::scoped_lock lock(g_choiceLock);
		if (!a_shownName.empty() && a_shownName != a_dllLower) { g_names[a_dllLower] = a_shownName; }
		if (a_on) { g_leftNow.insert(a_dllLower); }
		else { g_leftNow.erase(a_dllLower); }
		SaveLeftLocked();
		logger::info("flick: {} -> {} (applies from the next game start)", a_dllLower, a_on ? "the real FLICK's own window" : "this menu");
	}

	bool LeftToFlickChanged()
	{
		std::scoped_lock lock(g_choiceLock);
		return g_leftNow != g_leftAtStart;
	}

	void NoteConsumer(const std::string& a_dllLower, bool a_toAmf)
	{
		std::scoped_lock lock(g_choiceLock);
		const auto [it, added] = g_consumers.emplace(a_dllLower, a_toAmf);
		if (added)
		{
			logger::info("flick: {} asked for FLICK - {}", a_dllLower, a_toAmf ? "answered with this menu's table" :
				g_realFlick ? "left to the real FLICK" : "left to FLICK, which is not installed: it gets nothing");
		}
	}

	std::vector<Consumer> Consumers()
	{
		std::set<std::string> dlls;
		std::map<std::string, std::string> shown;
		{
			std::lock_guard lock(g_lock);
			for (const auto& t : g_tools)
			{
				if (t.dll.empty()) { continue; }
				dlls.insert(t.dll);
				if (!shown.contains(t.dll)) { shown[t.dll] = t.group.empty() ? t.name : t.group; }
			}
		}
		std::scoped_lock lock(g_choiceLock);
		for (const auto& [dll, toAmf] : g_consumers) { dlls.insert(dll); }
		for (const auto& dll : g_leftNow) { dlls.insert(dll); }
		for (const auto& dll : g_leftAtStart) { dlls.insert(dll); }
		std::vector<Consumer> out;
		for (const auto& dll : dlls)
		{
			const auto it = g_consumers.find(dll);
			std::string name = shown.contains(dll) ? shown[dll] : std::string{};
			if (name.empty()) { if (const auto n = g_names.find(dll); n != g_names.end()) { name = n->second; } }
			out.push_back({ dll, name, it != g_consumers.end() ? it->second : !g_leftAtStart.contains(dll), g_leftNow.contains(dll) });
		}
		return out;
	}
	bool Enabled() { return g_host.load(std::memory_order_acquire); }
	bool RealFlickInstalled() { return g_realFlick; }

	// THE ONE-ENTRY RULE (plan section 8; the owner's Q6 answer, 2026-10-08): a mod that registers its own page here (SKSE Menu
	// Framework's API) AND a FLICK page has one entry - its own. The FLICK copy is hidden, matched by name: "<entry> (FLICK)"
	// beside "<entry>". Checked every couple of seconds, because a mod's own page arrives later than its FLICK one.
	void HideFlickCopies()
	{
		static double s_last = -10.0;
		const double now = ImGui::GetTime();
		if (now - s_last < 2.0) { return; }
		s_last = now;
		std::vector<std::pair<std::string, std::string>> hide;   // (entry, page)
		{
			const auto entries = registry::Snapshot();
			std::set<std::string> own;
			for (const auto& e : entries) { own.insert(e.modName); }
			std::lock_guard lock(g_lock);
			for (auto& t : g_tools)
			{
				if (!t.listed) { continue; }
				const std::string base = t.entry.substr(0, t.entry.size() - std::string(" (FLICK)").size());
				if (own.contains(base))
				{
					t.listed = false;
					hide.emplace_back(t.entry, t.name.empty() ? "Settings" : t.name);
					logger::info("flick: \"{}\" also has its own page here - its FLICK copy \"{}\" is not listed (one entry per mod)", base, t.entry);
				}
			}
		}
		for (const auto& [entry, page] : hide) { registry::SetPageVisible(entry.c_str(), page.c_str(), false); }
	}

	void EndFrame()
	{
		HideFlickCopies();
		// FLICK's "tool deselected": the page open last frame and not drawn this one - the player left it or closed the menu
		if (g_active && g_drawnThisFrame != g_active)
		{
			g_active->OnClose();
			g_active = nullptr;
		}
		g_drawnThisFrame = nullptr;
		if (!g_items.empty() || g_active == nullptr)
		{
			std::lock_guard lock(g_itemLock);
			g_itemsShown.swap(g_items);
		}
		g_items.clear();
		// a mod's Draw that left a BeginWindow without its EndWindow: forget it rather than carry it into the next frame
		g_begun.clear();
	}

	std::size_t ToolCount()
	{
		std::lock_guard lock(g_lock);
		return g_tools.size();
	}
	ToolInfo ToolAt(std::size_t a_index)
	{
		std::lock_guard lock(g_lock);
		if (a_index >= g_tools.size()) { return {}; }
		const auto& t = g_tools[a_index];
		return { t.plugin, t.name, t.group, t.entry, t.listed, t.dll };
	}

	std::string StatusJson()
	{
		auto esc = [](const std::string& a_s) {
			std::string o;
			for (const char c : a_s) { if (c == '"' || c == '\\') { o += '\\'; } o += c; }
			return o;
		};
		std::string tools, notes;
		std::size_t windows = 0;
		{
			std::lock_guard lock(g_lock);
			for (const auto& t : g_tools)
			{
				tools += std::format(R"({}{{"plugin":"{}","name":"{}","group":"{}","entry":"{}","listed":{}}})", tools.empty() ? "" : ",",
									 esc(t.plugin), esc(t.name), esc(t.group), esc(t.entry), t.listed ? "true" : "false");
			}
			windows = g_windows.size();
		}
		{
			std::lock_guard lock(g_noteLock);
			for (const auto& n : g_notes) { notes += std::format(R"({}"{}")", notes.empty() ? "" : ",", esc(n)); }
		}
		std::string items;
		{
			std::lock_guard lock(g_itemLock);
			for (const auto& i : g_itemsShown)
			{
				items += std::format(R"({}{{"kind":"{}","label":"{}","rect":[{:.0f},{:.0f},{:.0f},{:.0f}],"focused":{},"hovered":{},"value":{}}})",
									 items.empty() ? "" : ",", i.kind, esc(i.label), i.min.x, i.min.y, i.max.x, i.max.y,
									 i.focused ? "true" : "false", i.hovered ? "true" : "false", i.value ? "true" : "false");
			}
		}
		return std::format(R"({{"host":{},"version":{},"slots":{},"requests":{},"realFlick":{},"tools":[{}],"windows":{},"active":"{}","items":[{}],"notAnswered":[{}]}})",
						   Enabled() ? "true" : "false", FLICK_API_VERSION_ANSWERED, FLICK_SLOT_COUNT, g_requests.load(),
						   g_realFlick ? "true" : "false", tools, windows, g_active ? esc(g_active->Name() ? g_active->Name() : "") : "", items, notes);
	}
}

extern "C" __declspec(dllexport) void* RequestFUCK()
{
	if (!flick::Enabled()) { return nullptr; }
	const auto n = flick::g_requests.fetch_add(1) + 1;
	logger::info("flick: RequestFUCK -> AMF's table (version {}), request #{}", FLICK_API_VERSION_ANSWERED, n);
	return &flick::Table();
}
