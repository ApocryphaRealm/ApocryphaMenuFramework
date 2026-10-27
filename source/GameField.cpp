#include "PCH.h"

#include "GameField.h"

#include "Input.h"
#include "KeyboardLayouts.h"
#include "Renderer.h"
#include "Settings.h"
#include "TextInputCore.h"
#include "utils/Logger.h"
#include "utils/NullGuard.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

// See GameField.h for what this is and why it is built this way.

namespace gamefield
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		}

		// ---- the menus -----------------------------------------------------------------------------------
		constexpr std::string_view kInventory = "InventoryMenu", kContainer = "ContainerMenu", kBarter = "BarterMenu",
								   kGift = "GiftMenu", kMagic = "MagicMenu", kMap = "MapMenu", kCrafting = "Crafting Menu",
								   kJournal = "Journal Menu", kConsole = "Console";

		// Names a menu in the UI's stack can be matched against (the generic adapter names any other one by its movie).
		// nonnull: constexpr table of string literals only
		constexpr const char* kKnownMenus[] = { "InventoryMenu", "ContainerMenu", "BarterMenu", "GiftMenu", "MagicMenu", "MapMenu",
			"Crafting Menu", "Journal Menu", "CustomMenu", "RaceSex Menu", "Console", "HUD Menu", "Cursor Menu", "MessageBoxMenu",
			"Book Menu", "Sleep/Wait Menu", "Tween Menu", "Lockpicking Menu", "Training Menu", "Dialogue Menu", "FavoritesMenu",
			"Main Menu", "Loading Menu", "Fader Menu", "Mist Menu", "TutorialMenu", "StatsMenu", "LevelUp Menu", "Quantity Menu",
			"Creation Club Menu", "Mod Manager Menu", "Credits Menu", "Kinect Menu" };

		// Menus that sit above the real one without being it (the pointer, the fader): never the "top" menu.
		bool IsOverlayMenu(std::string_view a_name)
		{
			return a_name == "Cursor Menu" || a_name == "HUD Menu" || a_name == "Fader Menu" || a_name == "Mist Menu";
		}

		// ---- candidate member paths: asked of the running movie, never assumed (rule 30) -------------------
		// SkyUI's InventoryLists (item menus and the magic menu) and its SearchWidget (startInput / endInput /
		// textField), the map menu's showLocationFinder, as mapped from Virtual Keyboard (158716).
		// nonnull: constexpr table of string literals only
		constexpr const char* kSearchCandidates[] = { "_root.Menu_mc.inventoryLists.searchWidget",
			"_root.Menu_mc.InventoryLists_mc.searchWidget", "_root.Menu_mc.inventoryLists.panelContainer.searchWidget" };
		// nonnull: constexpr table of string literals only
		constexpr const char* kListCandidates[] = { "_root.Menu_mc.inventoryLists.itemList", "_root.Menu_mc.InventoryLists_mc.itemList",
			"_root.Menu_mc.inventoryLists.panelContainer.itemList" };
		// SkyUI's map (map.swf, Map.MapMenu): the menu object is _root.WorldMap; its LocalMapMenu.showLocationFinder() opens the
		// finder panel (the F key / Find Location button), and the panel's search widget is started by Space
		// (LocationFinder.handleInput -> searchWidget.startInput()). The panel is _level0.locationFinderFader.locationFinder,
		// held by the menu as _locationFinder. Read from SkyUI 6.9 SE's map.swf / mapfinderpanel.swf (2.3.0 retest, the
		// batch 12 probe found _root.WorldMap._locationFinder; the old candidates _root.Menu_mc / MapMenu_mc / Map_mc never existed).
		constexpr const char* kMapOwner = "_root.WorldMap";
		constexpr const char* kMapFinder = "_root.WorldMap._locationFinder";
		constexpr const char* kVkClips[] = { "_root.items_vk", "_root.map_vk", "_root.enchanting_vk" };   // 158716's injected clips; nonnull: constexpr table of string literals only

		enum class Kind { kItem, kMagic, kMap, kEnchant, kMcm, kGeneric };

		Kind KindOf(std::string_view a_menu)
		{
			if (a_menu == kInventory || a_menu == kContainer || a_menu == kBarter || a_menu == kGift) { return Kind::kItem; }
			if (a_menu == kMagic) { return Kind::kMagic; }
			if (a_menu == kMap) { return Kind::kMap; }
			if (a_menu == kCrafting) { return Kind::kEnchant; }
			if (a_menu == kJournal) { return Kind::kMcm; }
			return Kind::kGeneric;
		}

		const char* AdapterId(Kind a_k)
		{
			switch (a_k)
			{
			case Kind::kItem: return "skyui-item-search";
			case Kind::kMagic: return "skyui-magic-search";
			case Kind::kMap: return "map-location-finder";
			case Kind::kEnchant: return "enchant-name";
			case Kind::kMcm: return "skyui-mcm-dialog";
			default: return "generic";
			}
		}

		std::atomic<bool> g_vk158716{ false };
		std::atomic<bool> g_b612{ false };

		// 158716 keeps the boxes it hooks; ours stand down there.
		bool OwnedByOther(Kind a_k)
		{
			return g_vk158716.load(std::memory_order_relaxed) && (a_k == Kind::kItem || a_k == Kind::kMagic || a_k == Kind::kMap || a_k == Kind::kEnchant);
		}

		bool AdapterOn(Kind a_k)
		{
			const auto& s = settings::Get();
			if (!s.onScreenKeyboard || !s.keyboardGameFields) { return false; }
			switch (a_k)
			{
			case Kind::kItem:
			case Kind::kMagic: return s.keyboardItemSearch;
			case Kind::kMap: return s.keyboardMapSearch;
			case Kind::kEnchant: return s.keyboardEnchantName;
			case Kind::kMcm: return s.keyboardMcmDialog;
			default: return s.keyboardOtherPrompts;
			}
		}

		// ---- shared state ---------------------------------------------------------------------------------
		struct MenuCache
		{
			std::string searchPath;    // SkyUI's searchWidget, when found
			std::string listPath;      // the item list (selectedIndex), when found
			std::string finderOwner;   // the map menu object (_root.WorldMap) whose LocalMapMenu.showLocationFinder() opens the finder
			std::string subtype;       // Crafting Menu: _root.Menu._subtypeName
			std::string url;
			bool vkClips = false;      // 158716's clips are in this menu
			int probes = 0;
			bool logged = false;
		};

		struct RouteStats { unsigned sent = 0, proven = 0, unchanged = 0; };

		std::mutex g_lock;   // guards everything in this block
		std::vector<std::string> g_openMenus;   // in the order they opened (the menu sink)
		std::map<std::string, MenuCache> g_cache;
		Target g_target;
		bool g_hasTarget = false;
		std::string g_expected;       // what the field should read after our edits
		std::string g_fieldText;      // what it read last
		std::string g_verifyBefore;   // the text before the first unverified edit
		bool g_verifyPending = false;
		int g_route = 1;
		bool g_routeForced = false;
		RouteStats g_stats[4];
		bool g_routeWarned = false;   // per target
		std::optional<Target> g_openReq;
		std::optional<std::string> g_closeReq;
		std::vector<PadEvent> g_pad;
		std::string g_lastDetect = "none yet";
		std::string g_focusApi;       // which Selection path answered
		struct Timed { std::uint64_t at; std::function<void()> fn; };
		std::vector<Timed> g_timed;

		std::atomic<int> g_claim{ 0 };
		std::atomic<bool> g_padInUse{ false };
		// the top item list's VISIBLE row (ListRow below), re-read every input dispatch while an item or magic menu is in
		// front: >= 0 the row, -1 the list is empty, -2 unknown, kNoRow rows but none selected
		std::atomic<int> g_listIndex{ -2 };
		constexpr int kNoRow = -3;
		std::atomic<std::uint64_t> g_rowReadAt{ 0 };   // the dispatch a row read was queued on; 0 none in flight
		std::atomic<bool> g_topSearchKnown{ false };
		std::atomic<bool> g_parkList{ false };      // parked over a menu whose list AMF moves itself (D-pad up / down)
		std::atomic<bool> g_rectValid{ false };
		std::atomic<float> g_rect[4]{};
		std::atomic<long long> g_explicitUntilMs{ 0 };
		std::atomic<int> g_gameCount{ 0 };
		std::atomic<int> g_amfAdded{ 0 };
		std::atomic<bool> g_gameWants{ false };
		std::atomic<std::uint64_t> g_dispatch{ 0 };

		// game thread only
		std::unordered_set<std::uint32_t> g_consumedPad;   // pad presses we took, so their releases are taken too
		bool g_mouseConsumed = false;

		std::string Esc(const std::string& a_s)
		{
			std::string out;
			for (const char ch : a_s)
			{
				const auto u = static_cast<unsigned char>(ch);
				if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
				else if (u < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", u); out += b; }
				else { out += ch; }
			}
			return out;
		}

		std::string ArgStr(const std::string& a_json, const char* a_key)
		{
			const std::string key = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(key);
			if (pos == std::string::npos) { return {}; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string::npos) { return {}; }
			pos = a_json.find('"', pos);
			if (pos == std::string::npos) { return {}; }
			std::string out;
			for (++pos; pos < a_json.size() && a_json[pos] != '"'; ++pos)
			{
				if (a_json[pos] == '\\' && pos + 1 < a_json.size()) { ++pos; }
				out += a_json[pos];
			}
			return out;
		}

		double ArgNum(const std::string& a_json, const char* a_key, double a_default)
		{
			const std::string key = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(key);
			if (pos == std::string::npos) { return a_default; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string::npos) { return a_default; }
			try { return std::stod(a_json.substr(pos + 1)); } catch (...) { return a_default; }
		}

		void AddUITask(std::function<void()> a_fn)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddUITask(std::move(a_fn)); }
			else
			{
				static bool s_said = false;
				if (!s_said) { s_said = true; logger::warn("keyboard: no SKSE task interface - game fields cannot be reached"); }
			}
		}

		// After a_dispatches more input dispatches, run a_fn as a UI task (a field's text is read back a moment later).
		void After(std::uint64_t a_dispatches, std::function<void()> a_fn)
		{
			std::scoped_lock l(g_lock);
			g_timed.push_back({ g_dispatch.load(std::memory_order_relaxed) + a_dispatches, std::move(a_fn) });
		}

		std::string TopMenuLocked()
		{
			for (auto it = g_openMenus.rbegin(); it != g_openMenus.rend(); ++it)
			{
				if (!IsOverlayMenu(*it)) { return *it; }
			}
			return {};
		}

		void RequestClose(const std::string& a_why)
		{
			std::scoped_lock l(g_lock);
			if (g_hasTarget || g_claim.load() != 0) { g_closeReq = a_why; }
			g_hasTarget = false;
			g_claim.store(0, std::memory_order_release);
		}

		// ---- Scaleform helpers (UI task only) --------------------------------------------------------------
		std::string NameOf(RE::IMenu* a_menu)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui || !a_menu) { return {}; }
			for (const char* name : kKnownMenus)
			{
				auto m = ui->GetMenu(name);
				if (m && m.get() == a_menu) { return name; }
			}
			return {};
		}

		std::string UrlOf(RE::GFxMovieView* a_movie)
		{
			if (!a_movie) { return {}; }
			auto* def = a_movie->GetMovieDef();
			const char* url = def ? def->GetFileURL() : nullptr;
			return url ? url : "";
		}

		RE::GPtr<RE::IMenu> FindMenu(const std::string& a_name, const std::string& a_url)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return {}; }
			if (!a_name.empty())
			{
				auto m = ui->GetMenu(a_name);
				if (m && m->uiMovie) { return m; }
				return {};
			}
			for (auto& m : ui->menuStack)
			{
				if (m && m->uiMovie && UrlOf(m->uiMovie.get()) == a_url) { return m; }
			}
			return {};
		}

		bool Avail(RE::GFxMovieView* a_mv, const std::string& a_path) { return a_mv && a_mv->IsAvailable(a_path.c_str()); }

		std::string GetStr(RE::GFxMovieView* a_mv, const std::string& a_path, bool* a_ok = nullptr)
		{
			RE::GFxValue v;
			const bool ok = a_mv && a_mv->GetVariable(&v, a_path.c_str()) && v.IsString() && v.GetString();
			if (a_ok) { *a_ok = ok; }
			return ok ? std::string(amf::Str(v.GetString())) : std::string();
		}

		bool GetBool(RE::GFxMovieView* a_mv, const std::string& a_path)
		{
			RE::GFxValue v;
			return a_mv && a_mv->GetVariable(&v, a_path.c_str()) && v.IsBool() && v.GetBool();
		}

		// SkyUI's SearchWidget.startInput() empties its field with the engine's TextField.SetText(""), and the field reads back ONE
		// SPACE straight after (batch 12: " " at detection, 7 ms after startInput() and before AMF's first edit, on every
		// start - the pad's L3 / Up and SkyUI's own Space key alike; P7's real keys "ir" landed BEFORE it, at the caret
		// setSelection(0,0) put at 0). So a freshly started search field holding exactly " " is emptied before the first key.
		bool IsSkyUiSearch(std::string_view a_adapter)
		{
			return a_adapter == "skyui-item-search" || a_adapter == "skyui-magic-search" || a_adapter == "map-location-finder";
		}

		bool GetNum(RE::GFxMovieView* a_mv, const std::string& a_path, double& a_out)
		{
			RE::GFxValue v;
			if (!a_mv || !a_mv->GetVariable(&v, a_path.c_str()) || !v.IsNumber()) { return false; }
			a_out = v.GetNumber();
			return true;
		}

		// 2.3.1: the list's selected row as the player sees it. SkyUI's selectedIndex is an index into its whole entry
		// array (every category, in load order), so 0 there can be any row on screen; getSelectedListEnumIndex() is the
		// place in the visible, filtered list - the number SkyUI's own moveSelectionUp tests. -1 (no selection) is the top
		// only when the list is empty: with rows in it the selection is elsewhere - Inventory Reforged's R3 slot mode sets
		// selectedIndex to -1, as does the pointer leaving the list - and Up belongs to that (owner report 2026-10-27:
		// Up on row 8 and on row 6 opened the keyboard).
		bool ListRow(RE::GFxMovieView* a_mv, const std::string& a_listPath, int& a_out)
		{
			double sel = -1;
			if (!a_mv || a_listPath.empty() || !GetNum(a_mv, a_listPath + ".selectedIndex", sel)) { return false; }
			RE::GFxValue r;
			if (sel < 0)
			{
				if (!a_mv->Invoke((a_listPath + ".getListEnumSize").c_str(), &r, nullptr, 0) || !r.IsNumber()) { return false; }
				a_out = r.GetNumber() > 0 ? kNoRow : -1;
				return true;
			}
			if (!a_mv->Invoke((a_listPath + ".getSelectedListEnumIndex").c_str(), &r, nullptr, 0) || !r.IsNumber()) { return false; }
			a_out = static_cast<int>(r.GetNumber());
			return true;
		}

		// Selection.getFocus(): the focused text field's path, or empty. Both spellings are tried and the one that answered
		// is remembered (logged once). Selection is an object, so this is a method call on a real object - not the
		// "through a function object" shape that crashed (logic library 5215).
		std::string FocusPath(RE::GFxMovieView* a_mv)
		{
			if (!a_mv) { return {}; }
			static constexpr const char* kApis[] = { "Selection.getFocus", "_global.Selection.getFocus" };
			for (const char* api : kApis)
			{
				RE::GFxValue r;
				if (a_mv->Invoke(api, &r, nullptr, 0) && r.IsString() && r.GetString() && *r.GetString())
				{
					bool first = false;
					{
						std::scoped_lock l(g_lock);
						if (g_focusApi != api) { first = g_focusApi.empty(); g_focusApi = api; }
					}
					if (first) { logger::info("keyboard: the focused field is read with {}()", api); }
					return r.GetString();
				}
			}
			return {};
		}

		// The GFx character event (Scaleform 3.x GFxCharEvent: the UTF-16 unit at +4, the keyboard index at +8). CommonLib
		// carries GFxKeyEvent but not this one; defined here from that layout.
		struct GfxCharEvent : RE::GFxEvent
		{
			explicit GfxCharEvent(std::uint32_t a_unit) : RE::GFxEvent(RE::GFxEvent::EventType::kCharEvent), wcharCode(a_unit), keyboardIndex(0) {}
			std::uint32_t wcharCode;
			std::uint32_t keyboardIndex;
		};
		static_assert(sizeof(GfxCharEvent) == 0xC);

		void GfxKeyTap(RE::GFxMovieView* a_mv, RE::GFxKey::Code a_code, std::uint8_t a_ascii)
		{
			AMF_REQUIRE_VOID(a_mv);  // 2.3.0: no movie, no key to send
			RE::GFxSpecialKeysState keys{};
			keys.states = static_cast<std::uint8_t>(RE::GFxSpecialKeysState::Key::kInitializedBit);
			const RE::GFxKeyEvent down(RE::GFxEvent::EventType::kKeyDown, a_code, a_ascii, a_ascii, keys);
			const RE::GFxKeyEvent up(RE::GFxEvent::EventType::kKeyUp, a_code, a_ascii, a_ascii, keys);
			a_mv->HandleEvent(down);
			a_mv->HandleEvent(up);
		}

		void GfxChars(RE::GFxMovieView* a_mv, const std::string& a_utf8)
		{
			AMF_REQUIRE_VOID(a_mv);  // 2.3.0: no movie, no text to send
			for (const char32_t cp : textcore::DecodeUtf8(a_utf8))
			{
				std::uint16_t units[2]{};
				const int n = textcore::ToUtf16(cp, units);
				for (int i = 0; i < n; ++i)
				{
					const GfxCharEvent ev(units[i]);
					a_mv->HandleEvent(ev);
				}
			}
		}

		// Route 3: the text set from code, then the field's change handler called by hand (setting .text from code fires no
		// onChanged in Flash).
		void SetTextAndNotify(RE::GFxMovieView* a_mv, const std::string& a_path, const std::string& a_text)
		{
			AMF_REQUIRE_VOID(a_mv);  // 2.3.0: no movie, no field to set
			a_mv->SetVariable((a_path + ".text").c_str(), a_text.c_str());
			if (Avail(a_mv, a_path + ".onChanged"))
			{
				RE::GFxValue field;
				if (a_mv->GetVariable(&field, a_path.c_str()))
				{
					a_mv->Invoke((a_path + ".onChanged").c_str(), nullptr, &field, 1);
				}
			}
		}

		// ---- the per-menu probe (rule 30, retried while SkyUI builds its clips - rule 17) -------------------
		void ProbeMenu(const std::string& a_menu)
		{
			auto menu = FindMenu(a_menu, {});
			if (!menu) { return; }
			RE::GFxMovieView* mv = menu->uiMovie.get();
			const Kind k = KindOf(a_menu);
			MenuCache c;
			c.url = UrlOf(mv);
			for (const char* p : kVkClips) { if (Avail(mv, p)) { c.vkClips = true; } }
			if (k == Kind::kItem || k == Kind::kMagic)
			{
				for (const char* p : kSearchCandidates) { if (Avail(mv, std::string(p) + ".startInput")) { c.searchPath = p; break; } }
				for (const char* p : kListCandidates) { if (Avail(mv, std::string(p) + ".selectedIndex")) { c.listPath = p; break; } }
			}
			if (k == Kind::kMap)
			{
				if (Avail(mv, std::string(kMapOwner) + ".LocalMapMenu.showLocationFinder")) { c.finderOwner = kMapOwner; }
				if (Avail(mv, std::string(kMapFinder) + ".searchWidget.startInput")) { c.searchPath = std::string(kMapFinder) + ".searchWidget"; }
				if (Avail(mv, std::string(kMapFinder) + ".list.selectedIndex")) { c.listPath = std::string(kMapFinder) + ".list"; }
			}
			if (k == Kind::kEnchant) { c.subtype = GetStr(mv, "_root.Menu._subtypeName"); }
			int idx = -2;
			const bool haveIdx = ListRow(mv, c.listPath, idx);
			bool log = false;
			bool retry = false;
			{
				std::scoped_lock l(g_lock);
				auto& slot = g_cache[a_menu];
				const int probes = slot.probes + 1;
				const bool wasLogged = slot.logged;
				slot = c;
				slot.probes = probes;
				const bool complete = (k == Kind::kItem || k == Kind::kMagic) ? (!c.searchPath.empty() && !c.listPath.empty()) :
									  (k == Kind::kMap) ? (!c.finderOwner.empty() && !c.searchPath.empty()) : true;
				retry = !complete && probes < 4;
				log = !wasLogged && (complete || !retry);
				slot.logged = wasLogged || log;
				if (TopMenuLocked() == a_menu) { g_topSearchKnown.store(!c.searchPath.empty() || !c.finderOwner.empty(), std::memory_order_release); }
			}
			if (haveIdx) { g_listIndex.store(idx, std::memory_order_release); }
			if (log)
			{
				// one info line per menu session: what the adapter found (rule 14: an edge, not a frame)
				logger::info("keyboard: {} opened - adapter {} ({}{}), search {}, list {}, map finder {}, subtype '{}', 158716 clips {}, movie {}",
							 a_menu, AdapterId(k), AdapterOn(k) ? "on" : "off", OwnedByOther(k) ? ", left to Virtual Keyboard (158716)" : "",
							 c.searchPath.empty() ? "(not found)" : c.searchPath, c.listPath.empty() ? "(not found)" : c.listPath,
							 c.finderOwner.empty() ? "(not found)" : c.finderOwner, c.subtype, c.vkClips ? "present" : "none", c.url);
			}
			if (retry)
			{
				const std::string menuName = a_menu;
				After(30, [menuName]() { ProbeMenu(menuName); });
			}
		}

		void ReadIndex(const std::string& a_menu)
		{
			std::string listPath;
			{
				std::scoped_lock l(g_lock);
				if (auto it = g_cache.find(a_menu); it != g_cache.end()) { listPath = it->second.listPath; }
			}
			if (listPath.empty()) { return; }
			auto menu = FindMenu(a_menu, {});
			if (!menu) { return; }
			int idx = -2;
			if (ListRow(menu->uiMovie.get(), listPath, idx)) { g_listIndex.store(idx, std::memory_order_release); }
		}

		// ---- finding the focused field (the edge, or a gesture) ------------------------------------------
		void Detect(bool a_explicit)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return; }
			if (renderer::IsMainWindowVisible()) { return; }
			std::string why = "no focused input field in any open menu";
			for (std::int32_t i = static_cast<std::int32_t>(ui->menuStack.size()) - 1; i >= 0; --i)
			{
				auto& m = ui->menuStack[static_cast<std::uint32_t>(i)];
				if (!m || !m->uiMovie) { continue; }
				RE::GFxMovieView* mv = m->uiMovie.get();
				const std::string name = NameOf(m.get());
				if (name == kConsole) { continue; }   // D4: the console is left out
				if (IsOverlayMenu(name)) { continue; }
				const std::string path = FocusPath(mv);
				if (path.empty()) { continue; }
				const std::string type = GetStr(mv, path + ".type");
				if (type != "input") { why = name + ": the focus " + path + " is not an input field (type '" + type + "')"; continue; }
				const std::string url = UrlOf(mv);
				const Kind k = KindOf(name);
				// the co-installed keyboards keep their own boxes
				if (OwnedByOther(k)) { why = name + ": left to Virtual Keyboard (158716)"; break; }
				if (k == Kind::kGeneric)
				{
					bool vk = false;
					for (const char* p : kVkClips) { if (Avail(mv, p)) { vk = true; } }
					std::string lower = url;
					for (auto& ch : lower) { ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); }
					if (vk) { why = name + ": the menu shows Virtual Keyboard (158716)'s own keyboard"; break; }
					if (g_b612.load() && lower.find("b612") != std::string::npos) { why = "B612's own text box (" + url + ") keeps its keyboard"; break; }
				}
				if (!AdapterOn(k)) { why = std::string(name.empty() ? url : name) + ": adapter " + AdapterId(k) + " is switched off"; break; }
				Target t;
				t.menu = name;
				t.url = url;
				t.path = path;
				t.adapter = AdapterId(k);
				t.text = GetStr(mv, path + ".text");
				t.explicitOpen = a_explicit || NowMs() < g_explicitUntilMs.load();
				const auto& s = settings::Get();
				const bool device = t.explicitOpen || g_padInUse.load() || s.keyboardWithMouse;
				const bool open = t.explicitOpen || (s.keyboardAutoOpen && device);
				if (open && t.text == " " && IsSkyUiSearch(t.adapter))
				{
					// the one space startInput()'s SetText("") leaves (IsSkyUiSearch above) - gone before the first key
					mv->SetVariable((path + ".text").c_str(), "");
					t.text = GetStr(mv, path + ".text");
					logger::debug("keyboard: {} started holding ' ' (SetText(\"\") in startInput) - emptied, now '{}'", path, t.text);
				}
				bool same = false;
				{
					std::scoped_lock l(g_lock);
					same = g_hasTarget && g_target.path == t.path && g_target.url == t.url;
					if (!same)
					{
						g_target = t;
						g_hasTarget = open;
						g_expected = t.text;
						g_fieldText = t.text;
						g_verifyPending = false;
						g_routeWarned = false;
						if (!g_routeForced) { g_route = 1; }
						if (open) { g_openReq = t; }
					}
					g_lastDetect = (name.empty() ? url : name) + " " + path + (open ? " - opened" : (device ? " - auto-open is off" : " - not on a controller"));
				}
				if (!same)
				{
					logger::info("keyboard: game field {} in {} (adapter {}, text '{}') - {}", path, name.empty() ? url : name, t.adapter, t.text,
								 open ? (t.explicitOpen ? "opened (asked for)" : "opened by itself") : (device ? "not opened: auto-open is off" : "not opened: keyboard and mouse in use"));
					// route 1 types at the caret: put it at the end, where our expected text adds
					if (open) { GfxKeyTap(mv, RE::GFxKey::kEnd, 0); }
				}
				return;
			}
			bool changed = false;
			{
				std::scoped_lock l(g_lock);
				changed = g_lastDetect != why;
				g_lastDetect = why;
			}
			if (changed) { logger::debug("keyboard: no game field taken - {}", why); }
		}

		// The map's finder panel shows a few frames after showLocationFinder() (from the world map the game first toggles to the
		// local map); its search is started the moment it reports itself shown, looked at every 5 dispatches, a_tries times.
		void StartMapSearch(const std::string& a_menu, int a_tries)
		{
			const std::string menuName = a_menu;
			After(5, [menuName, a_tries]() {
				auto menu = FindMenu(menuName, {});
				if (!menu) { return; }
				RE::GFxMovieView* mv = menu->uiMovie.get();
				AMF_REQUIRE_VOID(mv);  // 2.3.0: a menu with no movie has no finder to start
				if (!GetBool(mv, std::string(kMapFinder) + "._bShown"))
				{
					if (a_tries > 1) { StartMapSearch(menuName, a_tries - 1); }
					else { logger::info("keyboard: the map's location finder did not show - its search was not started"); }
					return;
				}
				const bool called = mv->Invoke((std::string(kMapFinder) + ".searchWidget.startInput").c_str(), nullptr, nullptr, 0);
				logger::debug("keyboard: the map's finder is shown - {}.searchWidget.startInput() -> {}", kMapFinder, called);
				g_explicitUntilMs.store(NowMs() + 2500);
				After(4, []() { Detect(true); });
			});
		}

		void Start(const std::string& a_menu)
		{
			auto menu = FindMenu(a_menu, {});
			if (!menu) { logger::debug("keyboard: start on {} - the menu is not open", a_menu); return; }
			RE::GFxMovieView* mv = menu->uiMovie.get();
			AMF_REQUIRE_VOID(mv);  // 2.3.0: a menu with no movie has no field to start
			MenuCache c;
			{
				std::scoped_lock l(g_lock);
				if (auto it = g_cache.find(a_menu); it != g_cache.end()) { c = it->second; }
			}
			const Kind k = KindOf(a_menu);
			bool called = false;
			if ((k == Kind::kItem || k == Kind::kMagic) && !c.searchPath.empty())
			{
				called = mv->Invoke((c.searchPath + ".startInput").c_str(), nullptr, nullptr, 0);
				logger::debug("keyboard: {}.startInput() on {} -> {}", c.searchPath, a_menu, called);
			}
			else if (k == Kind::kMap && !c.finderOwner.empty() && !c.searchPath.empty())
			{
				// as the player does it: F opens the finder panel (LocalMapMenu.showLocationFinder()), then Space starts its
				// search (searchWidget.startInput()) - the second once the panel says it is shown (its _bShown)
				if (GetBool(mv, std::string(kMapFinder) + "._bShown"))
				{
					called = mv->Invoke((c.searchPath + ".startInput").c_str(), nullptr, nullptr, 0);
					logger::debug("keyboard: the map's finder is shown - {}.startInput() -> {}", c.searchPath, called);
				}
				else
				{
					called = mv->Invoke((c.finderOwner + ".LocalMapMenu.showLocationFinder").c_str(), nullptr, nullptr, 0);
					logger::debug("keyboard: {}.LocalMapMenu.showLocationFinder() on {} -> {} - its search starts once the panel is shown", c.finderOwner, a_menu, called);
					StartMapSearch(a_menu, 40);
				}
			}
			else
			{
				logger::info("keyboard: no start gesture for {} (adapter {}: its member was not found when the menu opened)", a_menu, AdapterId(k));
			}
			g_explicitUntilMs.store(NowMs() + 2500);
			// the edge usually finds the field; looked for again a moment later in case the count was already up
			After(4, []() { Detect(true); });
			if (k == Kind::kMap) { After(20, []() { Detect(true); }); }   // the finder's filter box takes focus after its open
		}

		// ---- edits ---------------------------------------------------------------------------------------
		void Verify();

		void DoEdit(int a_back, std::string a_insert, Target a_t)
		{
			auto menu = FindMenu(a_t.menu, a_t.url);
			if (!menu) { RequestClose("the field's menu is gone"); return; }
			RE::GFxMovieView* mv = menu->uiMovie.get();
			const std::string before = GetStr(mv, a_t.path + ".text");
			int route = 1;
			std::string expected;
			{
				std::scoped_lock l(g_lock);
				route = g_route;
				if (g_expected != before && !g_verifyPending) { g_expected = before; }   // typed on a PC keyboard meanwhile
				for (int i = 0; i < a_back; ++i) { kblayout::PopCodePoint(g_expected); }
				g_expected += a_insert;
				expected = g_expected;
				if (!g_verifyPending) { g_verifyBefore = before; g_verifyPending = true; }
				++g_stats[route].sent;
			}
			if (route == 1)
			{
				for (int i = 0; i < a_back; ++i) { GfxKeyTap(mv, RE::GFxKey::kBackspace, 8); }
				GfxChars(mv, a_insert);
			}
			else if (route == 2)
			{
				for (int i = 0; i < a_back; ++i) { input::InjectPress(0, 0x0E, 1); }   // DIK_BACK
				if (!a_insert.empty()) { input::InjectText(a_insert); }
			}
			else
			{
				SetTextAndNotify(mv, a_t.path, expected);
			}
			logger::debug("keyboard: edit -{} +'{}' -> {} through route {} (expect '{}')", a_back, a_insert, a_t.path, route, expected);
			After(route == 2 ? 8 : 3, []() { Verify(); });
		}

		void Verify()
		{
			Target t;
			std::string expected, before;
			int route = 1;
			{
				std::scoped_lock l(g_lock);
				if (!g_hasTarget || !g_verifyPending) { return; }
				t = g_target;
				expected = g_expected;
				before = g_verifyBefore;
				route = g_route;
			}
			auto menu = FindMenu(t.menu, t.url);
			if (!menu) { return; }
			RE::GFxMovieView* mv = menu->uiMovie.get();
			const std::string actual = GetStr(mv, t.path + ".text");
			bool firstProof = false, warn = false;
			int next = route;
			{
				std::scoped_lock l(g_lock);
				g_verifyPending = false;
				g_fieldText = actual;
				if (actual == expected)
				{
					firstProof = g_stats[route].proven == 0;
					++g_stats[route].proven;
				}
				else if (actual == before)
				{
					++g_stats[route].unchanged;
					warn = !g_routeWarned;
					g_routeWarned = true;
					if (!g_routeForced && route < 3) { next = route + 1; g_route = next; }
				}
				else
				{
					g_expected = actual;   // something else changed it too (a PC key, the field's own limit) - follow the field
				}
			}
			if (firstProof) { logger::info("keyboard: route {} reaches {} in {} (read back '{}')", route, t.path, t.menu.empty() ? t.url : t.menu, actual); }
			if (actual == before && actual != expected)
			{
				if (warn)
				{
					logger::warn("keyboard: route {} did not reach {} in {} (text still '{}', expected '{}') - route {} takes over",
								 route, t.path, t.menu.empty() ? t.url : t.menu, actual, expected, next);
				}
				// the player never loses the character: the expected text goes in through route 3
				SetTextAndNotify(mv, t.path, expected);
				std::scoped_lock l(g_lock);
				g_fieldText = expected;
			}
		}

		void DoEnter(Target a_t)
		{
			auto menu = FindMenu(a_t.menu, a_t.url);
			if (!menu) { return; }
			int route = 1;
			{
				std::scoped_lock l(g_lock);
				route = g_route;
			}
			if (route == 2) { input::InjectPress(0, 0x1C, 1); }   // DIK_RETURN
			else { GfxKeyTap(menu->uiMovie.get(), RE::GFxKey::kReturn, 13); }
			logger::debug("keyboard: Enter to {} (route {})", a_t.path, route == 2 ? 2 : 1);
		}

		void DoEnd(Target a_t, std::string a_why)
		{
			auto menu = FindMenu(a_t.menu, a_t.url);
			if (menu && menu->uiMovie)  // 2.3.0: a menu with no movie has no field to end
			{
				MenuCache c;
				{
					std::scoped_lock l(g_lock);
					if (auto it = g_cache.find(a_t.menu); it != g_cache.end()) { c = it->second; }
				}
				const Kind k = KindOf(a_t.menu);
				if ((k == Kind::kItem || k == Kind::kMagic || k == Kind::kMap) && !c.searchPath.empty() && g_gameWants.load())
				{
					const bool ok = menu->uiMovie->Invoke((c.searchPath + ".endInput").c_str(), nullptr, nullptr, 0);
					logger::debug("keyboard: {}.endInput() -> {}", c.searchPath, ok);
				}
			}
			logger::debug("keyboard: game field {} let go ({})", a_t.path, a_why);
		}
	}

	// ==== the menu sink ===============================================================================
	namespace
	{
		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) { return RE::BSEventNotifyControl::kContinue; }
				const std::string name(a_event->menuName.c_str() ? a_event->menuName.c_str() : "");
				if (name.empty()) { return RE::BSEventNotifyControl::kContinue; }
				const Kind k = KindOf(name);
				if (a_event->opening)
				{
					{
						std::scoped_lock l(g_lock);
						g_openMenus.push_back(name);
						g_cache.erase(name);
						if (!IsOverlayMenu(name)) { g_topSearchKnown.store(false); g_listIndex.store(-2); }
					}
					if (k != Kind::kGeneric)
					{
						After(2, [name]() { ProbeMenu(name); });
					}
				}
				else
				{
					bool closeTarget = false;
					{
						std::scoped_lock l(g_lock);
						for (auto it = g_openMenus.begin(); it != g_openMenus.end(); ++it)
						{
							if (*it == name) { g_openMenus.erase(it); break; }
						}
						g_cache.erase(name);
						closeTarget = g_hasTarget && g_target.menu == name;
						const std::string top = TopMenuLocked();
						if (auto it = g_cache.find(top); it != g_cache.end())
						{
							g_topSearchKnown.store(!it->second.searchPath.empty() || !it->second.finderOwner.empty());
						}
						else { g_topSearchKnown.store(false); }
					}
					if (closeTarget) { RequestClose(name + " closed"); }
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		static bool s_done = false;
		if (s_done) { return; }
		auto* ui = RE::UI::GetSingleton();
		if (!ui) { logger::warn("keyboard: no UI singleton at data load - game fields wait for the next message"); return; }
		static MenuSink sink;
		ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
		s_done = true;
		if (auto* dh = RE::TESDataHandler::GetSingleton())
		{
			g_vk158716.store(dh->LookupLoadedModByName("Virtual Keyboard - SkyUI.esp") != nullptr);
			g_b612.store(dh->LookupLoadedModByName("b612.esp") != nullptr);
		}
		logger::info("keyboard: game fields {} (Virtual Keyboard 158716 {}, B612 {}){}", settings::Get().keyboardGameFields ? "on" : "off",
					 g_vk158716.load() ? "installed" : "not installed", g_b612.load() ? "installed" : "not installed",
					 g_vk158716.load() ? " - the item, map and enchanting boxes are left to Virtual Keyboard" : "");
	}

	bool VirtualKeyboardInstalled() { return g_vk158716.load(); }
	bool B612Installed() { return g_b612.load(); }

	// ==== game thread ==================================================================================
	Claim CurrentClaim() { return static_cast<Claim>(g_claim.load(std::memory_order_acquire)); }

	void OnDispatch(const RE::InputEvent* a_head, int a_amfAdded, bool a_amfWindow)
	{
		const std::uint64_t dispatch = g_dispatch.fetch_add(1, std::memory_order_relaxed) + 1;

		// which device is in use (only deliberate input moves it, as Input.cpp's detector)
		for (const RE::InputEvent* e = a_head; e; e = e->next)  // nonnull: the loop condition tests e before each step
		{
			if (const auto* b = e->AsButtonEvent(); b && b->IsDown())
			{
				const bool pad = b->GetDevice() == RE::INPUT_DEVICE::kGamepad;
				if (pad != g_padInUse.load(std::memory_order_relaxed)) { g_padInUse.store(pad); logger::debug("keyboard: {} in use", pad ? "controller" : "keyboard/mouse"); }
			}
			else if (e->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick)
			{
				const auto* t = static_cast<const RE::ThumbstickEvent*>(e);
				if ((std::fabs(t->xValue) > 0.35f || std::fabs(t->yValue) > 0.35f) && !g_padInUse.load()) { g_padInUse.store(true); }
			}
		}

		// 2.3.1: the top item or magic list's visible row, re-read once per dispatch (one read in flight at a time). Reading
		// it only after each pad press left it stale: Inventory Reforged leaves its R3 slot mode on the RELEASE, after that
		// read, so the next Up saw -1 with the list back on row 8 and opened the keyboard (owner report 2026-10-27)
		// (a read not answered within 30 dispatches is treated as lost and queued again)
		if (const std::uint64_t at = g_rowReadAt.load(std::memory_order_acquire);
			g_topSearchKnown.load(std::memory_order_acquire) && settings::Get().keyboardUpOpensSearch && (at == 0 || dispatch - at > 30))
		{
			g_rowReadAt.store(dispatch, std::memory_order_release);
			std::string top;
			{
				std::scoped_lock l(g_lock);
				top = TopMenuLocked();
			}
			const Kind tk = KindOf(top);
			if (tk == Kind::kItem || tk == Kind::kMagic)
			{
				AddUITask([top]() {
					ReadIndex(top);
					g_rowReadAt.store(0, std::memory_order_release);
				});
			}
			else
			{
				g_rowReadAt.store(0, std::memory_order_release);
			}
		}

		// THE EDGE: a game menu wants text (the count minus what AMF itself added)
		int count = 0;
		if (auto* controls = RE::ControlMap::GetSingleton()) { count = controls->GetRuntimeData().textEntryCount; }
		g_gameCount.store(count, std::memory_order_relaxed);
		g_amfAdded.store(a_amfAdded, std::memory_order_relaxed);
		const bool wants = !a_amfWindow && (count - a_amfAdded) > 0;
		if (wants != g_gameWants.load(std::memory_order_relaxed))
		{
			g_gameWants.store(wants);
			logger::debug("keyboard: a game menu {} text input (text-entry count {}, AMF's {})", wants ? "started" : "stopped", count, a_amfAdded);
			if (wants)
			{
				if (settings::Get().onScreenKeyboard && settings::Get().keyboardGameFields) { AddUITask([]() { Detect(false); }); }
			}
			else
			{
				RequestClose("the game menu stopped taking text");
			}
		}
		if (a_amfWindow && g_claim.load() != 0) { RequestClose("AMF's window took the input"); }

		// the timed checks that are due, as UI tasks
		std::vector<std::function<void()>> due;
		{
			std::scoped_lock l(g_lock);
			for (auto it = g_timed.begin(); it != g_timed.end();)
			{
				if (it->at <= dispatch) { due.push_back(std::move(it->fn)); it = g_timed.erase(it); }
				else { ++it; }
			}
		}
		for (auto& fn : due) { AddUITask(std::move(fn)); }
	}

	bool TakeEvent(const RE::InputEvent* a_event, Claim a_claim)
	{
		if (!a_event) { return false; }
		const auto* b = a_event->AsButtonEvent();
		const bool pad = b && b->GetDevice() == RE::INPUT_DEVICE::kGamepad;
		const std::uint32_t code = b ? b->GetIDCode() : 0;
		const bool down = b && b->IsDown();
		const bool release = b && b->Value() == 0.0f;   // by its value: a press shorter than a frame has no held time (logic library 11217)

		// a release of a press we took is ours too, whatever the claim is now
		if (pad && release && g_consumedPad.erase(code) != 0) { return true; }

		auto queue = [](const PadEvent& a_e) {
			std::scoped_lock l(g_lock);
			if (g_pad.size() < 256) { g_pad.push_back(a_e); }
		};

		if (a_claim == Claim::kFull)
		{
			if (a_event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick)
			{
				const auto* t = static_cast<const RE::ThumbstickEvent*>(a_event);
				queue({ 1, t->IsLeft() ? 0u : 1u, false, t->xValue, t->yValue });
				return true;
			}
			if (pad)
			{
				if (down) { g_consumedPad.insert(code); queue({ 0, code, true }); }
				return true;   // held repeats and releases are taken too: the menu underneath sees none of it
			}
			if (b && b->GetDevice() == RE::INPUT_DEVICE::kMouse && code == 0 && settings::Get().keyboardWithMouse)
			{
				float x = 0.0f, y = 0.0f;
				if (auto* cursor = RE::MenuCursor::GetSingleton()) { x = cursor->GetRuntimeData().cursorPosX; y = cursor->GetRuntimeData().cursorPosY; }
				if (down && g_rectValid.load() && x >= g_rect[0].load() && y >= g_rect[1].load() && x < g_rect[2].load() && y < g_rect[3].load())
				{
					g_mouseConsumed = true;
					queue({ 2, 0, true, x, y });
					return true;
				}
				if (release && g_mouseConsumed)
				{
					g_mouseConsumed = false;
					queue({ 2, 0, false, x, y });
					return true;
				}
			}
			return false;
		}
		if (a_claim == Claim::kParked)
		{
			// D-pad up / down while parked move the list underneath (MoveList): SkyUI holds the list's selection while
			// its search is active, so the game's own D-pad cannot (InventoryLists.onSearchInputStart)
			const bool listMove = (code == 0x0001 || code == 0x0002) && g_parkList.load(std::memory_order_acquire);
			if (pad && (code == 0x1000 || code == 0x2000 || listMove))
			{
				if (down) { g_consumedPad.insert(code); queue({ 0, code, true }); }
				return true;
			}
			return false;
		}

		// no claim: the gestures that START a field, in the item lists and the map
		if (!pad || !down) { return false; }
		std::string top;
		bool hasTarget = false;
		{
			std::scoped_lock l(g_lock);
			top = TopMenuLocked();
			hasTarget = g_hasTarget;
		}
		// A BOX ALREADY TAKING TEXT that the keyboard was not opened on (it was started with the keyboard or mouse - "not
		// opened: keyboard and mouse in use" - or auto-open is off): the open button opens it there, in every menu (the
		// MCM dialog and the enchanting name box included, which have no start gesture of their own). Batch 12: L3 did
		// nothing there, because this branch only knew the item lists and the map, and stood down while g_gameWants.
		{
			const auto& s0 = settings::Get();
			if (g_gameWants.load() && !hasTarget && s0.onScreenKeyboard && s0.keyboardGameFields && s0.keyboardOpenPad != 0 &&
				code == static_cast<std::uint32_t>(s0.keyboardOpenPad))
			{
				g_consumedPad.insert(code);
				logger::debug("keyboard: the open button on {} while its text box is taking text - opening the keyboard there", top);
				AddUITask([]() { Detect(true); });
				return true;
			}
		}
		const Kind k = KindOf(top);
		if (k == Kind::kItem || k == Kind::kMagic)
		{
			// the list's index after this press settles, for the next Up
			const std::string menuName = top;
			AddUITask([menuName]() { ReadIndex(menuName); });
		}
		if (k != Kind::kItem && k != Kind::kMagic && k != Kind::kMap) { return false; }
		if (!AdapterOn(k) || OwnedByOther(k) || g_gameWants.load()) { return false; }
		const auto& s = settings::Get();
		const bool known = g_topSearchKnown.load(std::memory_order_acquire);
		const bool button = s.keyboardOpenPad != 0 && code == static_cast<std::uint32_t>(s.keyboardOpenPad);
		// the visible row as of the last dispatch (OnDispatch re-reads it every frame while an item or magic menu is in
		// front): the first row, or an empty list - never "no row selected" (kNoRow), which is Inventory Reforged's slots
		// or the pointer holding the selection (2.3.1, owner report 2026-10-27)
		const int index = g_listIndex.load(std::memory_order_acquire);
		const bool up = code == 0x0001 && s.keyboardUpOpensSearch && (k == Kind::kItem || k == Kind::kMagic) && (index == 0 || index == -1);
		if (!known || (!button && !up)) { return false; }
		g_consumedPad.insert(code);
		logger::debug("keyboard: {} on {} (list index {}) - starting its text box", button ? "the open button" : "Up on the first row", top, index);
		const std::string menuName = top;
		AddUITask([menuName]() { Start(menuName); });
		return true;
	}

	// ==== render thread ===============================================================================
	bool TakeOpenRequest(Target& a_out)
	{
		std::scoped_lock l(g_lock);
		if (!g_openReq) { return false; }
		a_out = *g_openReq;
		g_openReq.reset();
		return true;
	}

	bool TakeCloseRequest(std::string& a_why)
	{
		std::scoped_lock l(g_lock);
		if (!g_closeReq) { return false; }
		a_why = *g_closeReq;
		g_closeReq.reset();
		return true;
	}

	void DrainPad(std::vector<PadEvent>& a_out)
	{
		std::scoped_lock l(g_lock);
		a_out.swap(g_pad);
		g_pad.clear();
	}

	void SetClaim(Claim a_claim)
	{
		const int was = g_claim.exchange(static_cast<int>(a_claim), std::memory_order_acq_rel);
		if (was != static_cast<int>(a_claim)) { logger::debug("keyboard: pad claim {} -> {}", was, static_cast<int>(a_claim)); }
		if (a_claim == Claim::kNone)
		{
			std::scoped_lock l(g_lock);
			g_hasTarget = false;
			g_pad.clear();
		}
		bool list = false;
		if (a_claim == Claim::kParked)
		{
			std::scoped_lock l(g_lock);
			if (auto it = g_cache.find(g_target.menu); g_hasTarget && it != g_cache.end()) { list = !it->second.listPath.empty(); }
		}
		g_parkList.store(list, std::memory_order_release);
	}

	void MoveList(int a_dir)
	{
		std::string menuName, listPath;
		{
			std::scoped_lock l(g_lock);
			if (!g_hasTarget) { return; }
			menuName = g_target.menu;
			if (auto it = g_cache.find(menuName); it != g_cache.end()) { listPath = it->second.listPath; }
		}
		if (listPath.empty()) { return; }
		AddUITask([menuName, listPath, a_dir]() {
			auto menu = FindMenu(menuName, {});
			if (!menu) { return; }
			RE::GFxMovieView* mv = menu->uiMovie.get();
			AMF_REQUIRE_VOID(mv);  // 2.3.0: a menu with no movie has no list to move
			// SkyUI's onSearchInputStart set disableSelection (and disableInput) on the list until the search ends, and
			// ScrollingList.moveSelectionUp/Down do nothing while it is set: lifted for the one step, then put back, so the
			// list stays as SkyUI keeps it while its search is active
			const std::string flag = listPath + ".disableSelection";
			RE::GFxValue was;
			const bool haveWas = mv->GetVariable(&was, flag.c_str());
			mv->SetVariable(flag.c_str(), RE::GFxValue(false));
			RE::GFxValue arg(false);   // a_bScrollPage: one row
			const bool ok = mv->Invoke((listPath + (a_dir < 0 ? ".moveSelectionUp" : ".moveSelectionDown")).c_str(), nullptr, &arg, 1);
			if (haveWas) { mv->SetVariable(flag.c_str(), was); }
			double idx = -1;
			GetNum(mv, listPath + ".selectedIndex", idx);
			if (int row = -2; ListRow(mv, listPath, row)) { g_listIndex.store(row, std::memory_order_release); }
			logger::debug("keyboard: parked - D-pad {} moved {} -> {} (selectedIndex {})", a_dir < 0 ? "up" : "down", listPath, ok, static_cast<int>(idx));
		});
	}

	void PublishRect(bool a_valid, float a_x0, float a_y0, float a_x1, float a_y1)
	{
		g_rect[0].store(a_x0); g_rect[1].store(a_y0); g_rect[2].store(a_x1); g_rect[3].store(a_y1);
		g_rectValid.store(a_valid);
	}

	void ApplyEdit(int a_backspaces, const std::string& a_insertUtf8)
	{
		Target t;
		{
			std::scoped_lock l(g_lock);
			if (!g_hasTarget) { return; }
			t = g_target;
		}
		if (a_backspaces <= 0 && a_insertUtf8.empty()) { return; }
		std::string insert = a_insertUtf8;
		AddUITask([a_backspaces, insert, t]() { DoEdit(a_backspaces, insert, t); });
	}

	void SendEnter()
	{
		Target t;
		{
			std::scoped_lock l(g_lock);
			if (!g_hasTarget) { return; }
			t = g_target;
		}
		AddUITask([t]() { DoEnter(t); });
	}

	void EndInput(const char* a_why)
	{
		Target t;
		bool had = false;
		{
			std::scoped_lock l(g_lock);
			had = g_hasTarget;
			t = g_target;
		}
		SetClaim(Claim::kNone);
		if (!had) { return; }
		std::string why = a_why ? a_why : "";
		AddUITask([t, why]() { DoEnd(t, why); });
	}

	std::string FieldText()
	{
		std::scoped_lock l(g_lock);
		return g_fieldText;
	}

	bool HasTarget()
	{
		std::scoped_lock l(g_lock);
		return g_hasTarget;
	}

	// ==== DevBench ====================================================================================
	std::string StateJson()
	{
		std::scoped_lock l(g_lock);
		std::string s = "{\"hasTarget\":" + std::string(g_hasTarget ? "true" : "false") + ",\"claim\":" + std::to_string(g_claim.load()) +
						",\"menu\":\"" + Esc(g_target.menu) + "\",\"url\":\"" + Esc(g_target.url) + "\",\"fieldPath\":\"" + Esc(g_target.path) +
						"\",\"adapter\":\"" + Esc(g_target.adapter) + "\",\"fieldText\":\"" + Esc(g_fieldText) + "\",\"expected\":\"" + Esc(g_expected) +
						"\",\"route\":" + std::to_string(g_route) + ",\"routeForced\":" + (g_routeForced ? "true" : "false") +
						",\"gameTextEntryCount\":" + std::to_string(g_gameCount.load()) + ",\"amfAdded\":" + std::to_string(g_amfAdded.load()) +
						",\"gameWantsText\":" + (g_gameWants.load() ? "true" : "false") + ",\"padInUse\":" + (g_padInUse.load() ? "true" : "false") +
						",\"listIndex\":" + std::to_string(g_listIndex.load()) + ",\"topSearchKnown\":" + (g_topSearchKnown.load() ? "true" : "false") +
						",\"topMenu\":\"" + Esc(TopMenuLocked()) + "\",\"lastDetect\":\"" + Esc(g_lastDetect) + "\",\"focusApi\":\"" + Esc(g_focusApi) +
						"\",\"virtualKeyboard158716\":" + (g_vk158716.load() ? "true" : "false") + ",\"b612\":" + (g_b612.load() ? "true" : "false") + ",\"routes\":{";
		for (int r = 1; r <= 3; ++r)
		{
			s += "\"" + std::to_string(r) + "\":{\"sent\":" + std::to_string(g_stats[r].sent) + ",\"proven\":" + std::to_string(g_stats[r].proven) +
				 ",\"unchanged\":" + std::to_string(g_stats[r].unchanged) + "}" + (r < 3 ? "," : "");
		}
		s += "}}";
		return s;
	}

	namespace
	{
		// READ-ONLY: nothing is called on the movie but IsAvailable, GetVariable and Selection.getFocus.
		std::string ProbeJson(const std::string& a_only)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) { return "{\"ok\":false,\"error\":\"no UI\"}"; }
			std::vector<std::string> menus;
			{
				std::scoped_lock l(g_lock);
				menus = g_openMenus;
			}
			std::string out = "{\"ok\":true,\"op\":\"gamefield\",\"do\":\"probe\",\"menus\":[";
			bool first = true;
			for (const auto& name : menus)
			{
				if (!a_only.empty() && name != a_only) { continue; }
				if (IsOverlayMenu(name)) { continue; }
				auto menu = FindMenu(name, {});
				const Kind k = KindOf(name);
				std::string m = "{\"menu\":\"" + Esc(name) + "\",\"adapter\":\"" + AdapterId(k) + "\",\"adapterOn\":" + (AdapterOn(k) ? "true" : "false") +
								",\"ownedBy158716\":" + (OwnedByOther(k) ? "true" : "false");
				if (!menu) { m += ",\"movie\":false}"; }
				else
				{
					RE::GFxMovieView* mv = menu->uiMovie.get();
					m += ",\"movie\":true,\"url\":\"" + Esc(UrlOf(mv)) + "\",\"paths\":{";
					std::vector<std::string> paths;
					if (k == Kind::kItem || k == Kind::kMagic)
					{
						for (const char* p : kSearchCandidates)
						{
							for (const char* mem : { "", ".startInput", ".endInput", ".textField", ".textField.text" }) { paths.push_back(std::string(p) + mem); }
						}
						for (const char* p : kListCandidates) { paths.push_back(std::string(p) + ".selectedIndex"); }
					}
					if (k == Kind::kMap)
					{
						paths.push_back(std::string(kMapOwner) + ".LocalMapMenu.showLocationFinder");
						for (const char* mem : { "", "._bShown", ".searchWidget.startInput", ".searchWidget.endInput", ".searchWidget.textField", ".list.selectedIndex" })
						{
							paths.push_back(std::string(kMapFinder) + mem);
						}
					}
					if (k == Kind::kEnchant) { paths.push_back("_root.Menu._subtypeName"); paths.push_back("_root.Menu.ItemInfo"); paths.push_back("_root.Menu.ItemCard"); }
					if (k == Kind::kMcm) { paths.push_back("_root.ConfigPanelFader.configPanel"); }
					for (const char* p : kVkClips) { paths.push_back(p); }
					for (std::size_t i = 0; i < paths.size(); ++i)
					{
						m += "\"" + Esc(paths[i]) + "\":" + (Avail(mv, paths[i]) ? "true" : "false") + (i + 1 < paths.size() ? "," : "");
					}
					m += "}";
					MenuCache c;
					{
						std::scoped_lock l(g_lock);
						if (auto it = g_cache.find(name); it != g_cache.end()) { c = it->second; }
					}
					double idx = -99;
					if (!c.listPath.empty()) { GetNum(mv, c.listPath + ".selectedIndex", idx); }
					const std::string focus = FocusPath(mv);
					m += ",\"cached\":{\"search\":\"" + Esc(c.searchPath) + "\",\"list\":\"" + Esc(c.listPath) + "\",\"finder\":\"" + Esc(c.finderOwner) +
						 "\",\"subtype\":\"" + Esc(k == Kind::kEnchant ? GetStr(mv, "_root.Menu._subtypeName") : c.subtype) + "\",\"selectedIndex\":" +
						 std::to_string(static_cast<int>(idx)) + "}";
					m += ",\"focus\":\"" + Esc(focus) + "\"";
					if (!focus.empty())
					{
						m += ",\"focusType\":\"" + Esc(GetStr(mv, focus + ".type")) + "\",\"focusText\":\"" + Esc(GetStr(mv, focus + ".text")) + "\"";
					}
					m += "}";
				}
				out += (first ? "" : ",") + m;
				first = false;
			}
			out += "],\"state\":" + StateJson() + "}";
			return out;
		}
	}

	std::string ToolJson(const std::string& a_args)
	{
		const std::string what = ArgStr(a_args, "do");
		if (what == "probe")
		{
			struct Result { std::mutex m; std::condition_variable cv; bool done = false; std::string json; };
			auto result = std::make_shared<Result>();
			const std::string only = ArgStr(a_args, "menu");
			AddUITask([result, only]() {
				std::string json = ProbeJson(only);
				std::scoped_lock l(result->m);
				result->json = std::move(json);
				result->done = true;
				result->cv.notify_all();
			});
			std::unique_lock l(result->m);
			if (!result->cv.wait_for(l, std::chrono::milliseconds(2500), [&]() { return result->done; }))
			{
				return "{\"ok\":false,\"op\":\"gamefield\",\"error\":\"the UI did not run the probe within 2.5 s (a load?) - try again\",\"state\":" + StateJson() + "}";
			}
			return result->json;
		}
		if (what == "start")
		{
			std::string menu = ArgStr(a_args, "menu");
			if (menu.empty()) { std::scoped_lock l(g_lock); menu = TopMenuLocked(); }
			AddUITask([menu]() { Start(menu); });
			return "{\"ok\":true,\"op\":\"gamefield\",\"do\":\"start\",\"menu\":\"" + Esc(menu) + "\"}";
		}
		if (what == "detect")
		{
			AddUITask([]() { Detect(true); });
			return "{\"ok\":true,\"op\":\"gamefield\",\"do\":\"detect\",\"note\":\"read do=state a moment later\"}";
		}
		if (what == "route")
		{
			const int n = static_cast<int>(ArgNum(a_args, "n", 0));
			std::scoped_lock l(g_lock);
			if (n >= 1 && n <= 3) { g_route = n; g_routeForced = true; }
			else { g_routeForced = false; g_route = 1; }
			logger::info("keyboard: route {} (devbench)", g_routeForced ? std::to_string(g_route) : std::string("automatic"));
			return "{\"ok\":true,\"op\":\"gamefield\",\"do\":\"route\",\"route\":" + std::to_string(g_route) + ",\"forced\":" + (g_routeForced ? "true" : "false") + "}";
		}
		if (what == "state" || what.empty())
		{
			return "{\"ok\":true,\"op\":\"gamefield\",\"state\":" + StateJson() + "}";
		}
		return "{\"ok\":false,\"op\":\"gamefield\",\"error\":\"unknown do '" + Esc(what) + "' (probe|state|start|detect|route)\"}";
	}
}
