#include "DevBenchTool.h"
#include "FlickHost.h"
#include "PrismaRedux.h"
#include "Presets.h"
#include "IniPages.h"
#include "TomlPages.h"
#include "MfmPages.h"
#include "ImguiHost.h"
#include "SystemRow.h"

#include "DevBench/DevBenchAPI.h"
#include "Input.h"
#include "Keyboard.h"
#include "GameField.h"
#include "MainMenuDriver.h"
#include "McmLoader.h"
#include "Renderer.h"
#include <imgui.h>
#include "Theme.h"
#include "Settings.h"
#include "Skin.h"
#include "Strings.h"
#include "TextInput.h"
#include <cmath>
#include <chrono>
#include <thread>
#include <ctime>
#include <filesystem>
#include "Watchdog.h"
#include "Persistence.h"
#include <cstdio>
#include "utils/Logger.h"
#include "utils/NullGuard.h"

namespace renderer { void RequestFontRebuild(); }   // Renderer.cpp - at the next frame's start (2.2.4: setting textScale)

#include <iterator>
#include <string>

// Exported from main.cpp (the consumer-header surface DEM resolves via GetProcAddress); called
// in-process here for the keybind widget's reserved-key verdict.
extern "C" std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity);

namespace devbenchtool
{
	namespace
	{
		// A string made safe inside JSON quotes (2.1.9, for file names in op=cleanstate): quote, backslash and control characters.
		std::string JsonEscape(const std::string& a_s)
		{
			std::string out;
			out.reserve(a_s.size() + 8);
			for (const char ch : a_s)
			{
				const auto u = static_cast<unsigned char>(ch);
				if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
				else if (u < 0x20) { char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", u); out += buf; }
				else { out += ch; }
			}
			return out;
		}
		// Minimal extractor for a top-level JSON number field: "key":123 or "key":12.5.
		double JsonNum(const std::string& a_json, const char* a_key, double a_default)
		{
			const std::string needle = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(needle);
			if (pos == std::string::npos) return a_default;
			pos = a_json.find(':', pos + needle.size());
			if (pos == std::string::npos) return a_default;
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '	')) ++pos;
			try { return std::stod(a_json.substr(pos)); } catch (...) { return a_default; }
		}

		// Minimal extractor for a top-level JSON string field: finds "key":"value" and returns
		// value (handling backslash escapes). Enough for this tool's small, controlled argument
		// shape - no dependency on a JSON library.
		std::string JsonStr(const std::string& a_json, const char* a_key)
		{
			// A KEY is the quoted name followed by a colon. Finding the quoted name alone matched a VALUE first: in
			// {"op": "separator", "separator": "::sep:2"} the lookup of "separator" hit op's value and returned the
			// next field's ("collapse"), so amf.menu op separator collapse / send failed (found in the Oblivion port,
			// 2026-10-02).
			const std::string key = std::string("\"") + a_key + "\"";
			std::string::size_type pos = 0;
			for (;;)
			{
				pos = a_json.find(key, pos);
				if (pos == std::string::npos)
				{
					return "";
				}
				auto after = pos + key.size();
				while (after < a_json.size() && (a_json[after] == ' ' || a_json[after] == '\t')) { ++after; }
				if (after < a_json.size() && a_json[after] == ':')
				{
					pos = after;
					break;
				}
				pos += key.size();
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
			AMF_REQUIRE_VOID(a_write);   // DevBench always passes its writer; without one there is no one to answer
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			std::string result;
			if (op == "open")
			{
				// 1.8.0: nested=true opens the window the way the SKSE MENUS row does (sized to the journal),
				// so a journal-art fit can be measured with the journal open and nobody pressing the row.
				const bool nested = args.find("\"nested\":true") != std::string::npos;
				if (nested) { renderer::SetSelectedNode("system/mods"); }
				renderer::SetMenuVisible(true, nested);
				result = std::string("{\"ok\":true,\"op\":\"open\",\"nested\":") + (nested ? "true" : "false") + "}";
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
			else if (op == "language")
			{
				// 1.6.4: switch the framework's own text live - args language ("german", "auto"). Reloads the
				// strings, saves the INI override and rebuilds the atlas for the language's glyphs.
				const std::string lang = JsonStr(args, "language");
				strings::SetLanguage(lang);
				std::string avail;
				for (const auto& l : strings::Available()) { avail += (avail.empty() ? "\"" : ",\"") + l + "\""; }
				result = "{\"ok\":true,\"op\":\"language\",\"showing\":\"" + strings::Language() + "\",\"available\":[" + avail + "]}";
			}
			else if (op == "font")
			{
				// 1.8.9: what the last atlas build holds - the atlas is rebuilt on the render thread after a
				// language switch, so read this again a moment after op=language.
				const auto p = renderer::GetFontProbe();
				result = "{\"ok\":true,\"op\":\"font\",\"language\":\"" + p.language + "\",\"gameLanguage\":\"" + p.gameLanguage +
					"\",\"builds\":" + std::to_string(p.builds) + ",\"glyphs\":" + std::to_string(p.glyphs) +
					",\"atlas\":[" + std::to_string(p.atlasWidth) + "," + std::to_string(p.atlasHeight) + "]" +
					",\"hasKana\":" + (p.hasKana ? "true" : "false") + ",\"hasHangul\":" + (p.hasHangul ? "true" : "false") +
					",\"hasHanzi\":" + (p.hasHanzi ? "true" : "false") + ",\"hasCyrillic\":" + (p.hasCyrillic ? "true" : "false") +
					",\"hasHanziTraditional\":" + (p.hasHanziTraditional ? "true" : "false") + ",\"hasHanziRare\":" + (p.hasHanziRare ? "true" : "false") +
					",\"hasLatinExtA\":" + (p.hasLatinExtA ? "true" : "false") + ",\"fitStep\":" + std::to_string(p.fitStep) +
					",\"rangeLevel\":" + std::to_string(p.rangeLevel) + ",\"oversampleH\":" + std::to_string(p.oversampleH) +
					",\"onDemand\":" + std::to_string(p.onDemand) + "}";
			}
			else if (op == "theme")
			{
				const std::string id = JsonStr(args, "id");
				const bool ok = renderer::SetTheme(id);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"theme\",\"id\":\"" + id + "\"}";
			}
			else if (op == "flick")
			{
				// 2.1.6: the FLICK host - the FLICK mods connected, their pages, the page open now, and the calls not answered yet.
				// {"op":"flick","dll":"<mod>.dll","place":"amf"|"flick"} sets the per-mod choice (applies from the next start).
				// 2.2.1: each consumer also says "chosen" (the player picked its place) and "windows" (it has FLICK windows, so with
				// no choice it goes to the real FLICK when FLICK is installed); leftToFlick is where it goes from the next start.
				const std::string dll = JsonStr(args, "dll");
				const std::string place = JsonStr(args, "place");
				// 2.1.8: {"op":"flick","show":"<entry> (FLICK)"} lists a FLICK copy the one-entry rule hid, for this session
				if (const std::string show = JsonStr(args, "show"); !show.empty()) { flick::ShowCopy(show); }
				if (!dll.empty() && (place == "amf" || place == "flick"))
				{
					std::string shown;
					for (const auto& c : flick::Consumers()) { if (c.dll == dll) { shown = c.name; } }
					flick::SetLeftToFlick(dll, place == "flick", shown);
				}
				std::string consumers = "[";
				for (const auto& c : flick::Consumers())
				{
					if (consumers.size() > 1) { consumers += ","; }
					consumers += "{\"dll\":\"" + c.dll + "\",\"name\":\"" + c.name + "\",\"toAmf\":" + (c.toAmf ? "true" : "false") +
								 ",\"leftToFlick\":" + (c.leftToFlick ? "true" : "false") + ",\"chosen\":" + (c.chosen ? "true" : "false") +
								 ",\"windows\":" + (c.windows ? "true" : "false") + ",\"clashFlick\":\"" + c.clashFlick + "\",\"era\":\"" + c.era +
								 "\",\"api\":" + std::to_string(c.apiVersion) + ",\"camera\":" + (c.camera ? "true" : "false") +   // 2.3.0
								 ",\"tooOldFlick\":\"" + c.tooOldFlick + "\"}";
				}
				consumers += "]";
				const std::string status = flick::StatusJson();
				result = status.substr(0, status.rfind('}')) + ",\"consumers\":" + consumers + "}";
			}
			else if (op == "ini")
			{
				// 2.1.9: INI-only mods' pages (IniPages.h) - {"op":"ini","do":"list|get|set","mod":"SSEDisplayTweaks","section":"Render","name":"EnableVSync","value":"false"}
				result = inipages::ToolJson(args);
			}
			else if (op == "mfm")
			{
				// next version: Mod Function Menu entries (MfmPages.h) - {"op":"mfm","do":"list|run","path":"Mod/Toggle UI/Toggle UI.toml"}.
				// run hands the call to the render thread and waits for it (a mod's function is never called on this thread).
				result = mfmpages::ToolJson(args);
			}
			else if (op == "toml" || op == "gso")
			{
				// 2.2.1: TOML files' pages (TomlPages.h) - {"op":"toml","do":"list|get|set|reload","file":"EngineFixes","name":"General.bVerboseLogging",
				// "value":"true"}. set and reload run on the render thread, as the page does. op=gso: only Game Settings Override's files.
				result = tomlpages::ToolJson(args, op == "gso");
			}
			else if (op == "imgui")
			{
				// 2.2.0: other mods' own menus (ImguiHost.h) - {"op":"imgui","do":"list|open|close","id":"oar|cs|ied|optiscaler|enb"}.
				// open / close run on the render thread, as the page's buttons do.
				result = imguihost::ToolJson(args);
			}
			else if (op == "liveapply")
			{
				// 2.2.1: live apply on converted pages (LiveApplyCore.h) - {"op":"liveapply"} reports; {"op":"liveapply","on":0|1}
				// also sets and saves [MCM] bLiveApply as the settings page's switch does.
				result = mcmloader::LiveApplyJson(args);
			}
			else if (op == "presets")
			{
				// 2.3.0: settings presets (Presets.h) - {"op":"presets","do":"list|save|diff|load|delete|status","name":"Hard"}; save takes
				// skip:[group keys], diff shipped:true for a shipped preset, load keys:[group keys] (default: the mods switched on).
				result = presets::ToolJson(args);
			}
			else if (op == "prisma")
			{
				// 2.1.6: the Prisma MCM Redux menus - {"op":"prisma","do":"list|get|set|action|apply","mod":"<ModID>",...}
				result = prisma::ToolJson(args);
			}
			else if (op == "art")
			{
				// 2.1.6: the Appearance > Art picks. {"op":"art"} lists; {"op":"art","kind":"frame","name":"oathvein"} sets
				// (name "" = the theme's own, "none" = no art of that kind).
				const std::string kind = JsonStr(args, "kind");
				result = renderer::ArtOp(kind, JsonStr(args, "name"), !kind.empty());
			}
			else if (op == "alias")
			{
				// Menu-shell personalization: rename a mod's entry (empty name clears the alias).
				const std::string mod = JsonStr(args, "mod");
				const std::string name = JsonStr(args, "name");
				const bool ok = renderer::SetModAlias(mod, name);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"alias\",\"mod\":\"" + mod + "\",\"name\":\"" + name + "\"}";
			}
			else if (op == "move")
			{
				// 1-based position; every other entry re-flows around it.
				const std::string mod = JsonStr(args, "mod");
				int position = 0;
				try { position = std::stoi(JsonStr(args, "position")); } catch (...) { position = 0; }
				const bool ok = position > 0 && renderer::MoveModTo(mod, position);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"move\",\"mod\":\"" + mod + "\",\"position\":" + std::to_string(position) + "}";
			}
			else if (op == "separator")
			{
				// 2026-10-02: separators in the mod list - action add {name, mod: the row it goes above} | remove
				// {separator} | send {mod, separator: "" = out of every group} | collapse {separator} | favourite {separator}
				result = renderer::SeparatorOp(JsonStr(args, "action"), JsonStr(args, "name"), JsonStr(args, "mod"), JsonStr(args, "separator"));
			}
			else if (op == "resetorder")
			{
				renderer::ResetModOrder();
				result = "{\"ok\":true,\"op\":\"resetorder\"}";
			}
			else if (op == "preset")
			{
				// 2.0.3: menu-list layout presets - action save|load|delete {name} | list. The same calls the Settings
				// page's buttons make.
				const std::string action = JsonStr(args, "action");
				const std::string name = JsonStr(args, "name");
				bool ok = true;
				if (action == "save") { ok = settings::SaveLayoutPreset(name); }
				else if (action == "load") { ok = settings::LoadLayoutPreset(name); }
				else if (action == "delete") { ok = settings::DeleteLayoutPreset(name); }
				else if (action != "list") { ok = false; }
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"preset\",\"action\":\"" + action + "\",\"presets\":[";
				const auto presets = settings::ListLayoutPresets();
				for (std::size_t i = 0; i < presets.size(); ++i)
				{
					result += (i ? ",\"" : "\"") + presets[i] + "\"";
				}
				result += "]}";
			}
			else if (op == "nav")
			{
				// 1.6.7: drive the navigation exactly as the D-pad does - args dir left|right.
				// Inside a mod with several sections this walks the tab bar; left at the first tab
				// (or in a pane with no tabs) goes back to the mod list.
				const std::string dir = JsonStr(args, "dir");
				const bool ok = renderer::QueueNav(dir);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"nav\",\"dir\":\"" + dir + "\"}";
			}
			else if (op == "focus")
			{
				// Put nav in a pane so a nav case can be set up without pressing anything.
				const std::string pane = JsonStr(args, "pane");
				const bool ok = renderer::FocusPane(pane);
				result = std::string("{\"ok\":") + (ok ? "true" : "false") +
						 ",\"op\":\"focus\",\"pane\":\"" + pane + "\"}";
			}
			else if (op == "cursor")
			{
				// Point-and-click driving (1.5.6): place the software cursor at an absolute
				// display-space position. The OS cursor is not what ImGui reads here.
				const double x = JsonNum(args, "x", -1);
				const double y = JsonNum(args, "y", -1);
				if (x < 0 || y < 0)
				{
					result = "{\"ok\":false,\"error\":\"cursor needs x and y (display pixels)\"}";
				}
				else
				{
					input::SetCursorAbsolute(static_cast<float>(x), static_cast<float>(y));
					result = "{\"ok\":true,\"op\":\"cursor\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + "}";
				}
			}
			else if (op == "inject")
			{
				// A REAL engine press spliced ahead of this plugin's hook: device keyboard|gamepad|mouse, code, hold.
				const std::string dev = JsonStr(args, "device");
				const std::uint32_t d = dev == "gamepad" ? 2u : (dev == "mouse" ? 1u : 0u);
				const int code = static_cast<int>(JsonNum(args, "code", 0)); const int hold = static_cast<int>(JsonNum(args, "hold", 4));
				if (code <= 0) { result = "{\"ok\":false,\"error\":\"inject needs a code\"}"; }
				else { input::InjectPress(d, static_cast<std::uint32_t>(code), hold); result = "{\"ok\":true,\"op\":\"inject\",\"code\":" + std::to_string(code) + ",\"hold\":" + std::to_string(hold) + "}"; }
			}
			else if (op == "injectchar")
			{
				// REAL CharEvents (what the engine makes from DirectInput keys), one per dispatch, ahead of the hook. 2.2.1: one
				// per code point of the UTF-8 text, so CJK arrives as itself (chars = code points, bytes = UTF-8 length).
				const std::string text = JsonStr(args, "text");
				input::InjectText(text);
				result = "{\"ok\":true,\"op\":\"injectchar\",\"chars\":" + std::to_string(textcore::DecodeUtf8(text).size()) +
						 ",\"bytes\":" + std::to_string(text.size()) + "}";
			}
			else if (op == "type")
			{
				// Type text into whatever ImGui item is active (the real record path, from the queue on). 2.2.1: UTF-8 decoded,
				// one character per code point (it used to push each byte - mojibake for anything past ASCII).
				const std::string text = JsonStr(args, "text");
				input::QueueText(text);
				result = "{\"ok\":true,\"op\":\"type\",\"chars\":" + std::to_string(textcore::DecodeUtf8(text).size()) +
						 ",\"bytes\":" + std::to_string(text.size()) + "}";
			}
			else if (op == "ime")
			{
				// TYPING IN OTHER LANGUAGES (2.2.1; TextInput.h): the window procedure, the IME and which path typed each
				// character - read it after typing in a box to see whether WM_CHAR / the IME reached AMF.
				result = textinput::StateJson();
			}
			else if (op == "stick")
			{
				// Push a thumbstick and let it go: which 0 left | 1 right, x/y in -1..1 (y > 0 is up), hold in frames.
				const int which = static_cast<int>(JsonNum(args, "which", 1));
				const float x = static_cast<float>(JsonNum(args, "x", 0)), y = static_cast<float>(JsonNum(args, "y", 0));
				const int hold = static_cast<int>(JsonNum(args, "hold", 6));
				input::QueueStick(which, x, y, hold);
				result = "{\"ok\":true,\"op\":\"stick\",\"which\":" + std::to_string(which) + ",\"hold\":" + std::to_string(hold) + "}";
			}
			else if (op == "key")
			{
				// Press one key by DirectInput scan code (Backspace 0x0E, Enter 0x1C, Escape 0x01 ...).
				const int code = static_cast<int>(JsonNum(args, "code", 0));
				if (code <= 0) { result = "{\"ok\":false,\"error\":\"key needs a scan code\"}"; }
				else { input::QueueKey(static_cast<std::uint32_t>(code)); result = "{\"ok\":true,\"op\":\"key\",\"code\":" + std::to_string(code) + "}"; }
			}
			else if (op == "click")
			{
				// Press and release at the current cursor; optional x/y places it first. Down and
				// up land on separate frames - trickling is off in this framework, so same-frame pairs vanish.
				const double x = JsonNum(args, "x", -1);
				const double y = JsonNum(args, "y", -1);
				const int button = static_cast<int>(JsonNum(args, "button", 0));
				if (x >= 0 && y >= 0)
				{
					input::SetCursorAbsolute(static_cast<float>(x), static_cast<float>(y));
				}
				input::QueueMouseClick(static_cast<std::uint32_t>(button));
				result = "{\"ok\":true,\"op\":\"click\",\"button\":" + std::to_string(button) + "}";
			}
			else if (op == "mousemove")
			{
				// 2.1.7: a RELATIVE mouse movement in raw mouse counts (args dx, dy), through the same record and scaling a
				// real mouse takes - read state's cursor before and after to measure the pointer speed (pointer.scale px per
				// count; the game's own menus move 1/1280 of the screen width per count x fMouseCursorSpeed).
				const double dx = JsonNum(args, "dx", 0), dy = JsonNum(args, "dy", 0);
				float cx = 0.0f, cy = 0.0f;
				input::GetCursor(cx, cy);
				input::QueueMouseMove(static_cast<float>(dx), static_cast<float>(dy));
				result = "{\"ok\":true,\"op\":\"mousemove\",\"dx\":" + std::to_string(dx) + ",\"dy\":" + std::to_string(dy) +
						 ",\"cursorBefore\":[" + std::to_string(static_cast<int>(cx)) + "," + std::to_string(static_cast<int>(cy)) +
						 "],\"scale\":" + std::to_string(input::PointerScale()) + "}";
			}
			else if (op == "setting")
			{
				// 2.1.7: set one of the Settings > General values the settings page sets, and save the INI, exactly as the
				// page's control does. args name: pointerSpeed (value 0.25-4.00) | wheelSwitchesTabs (0/1) | pauseGame (0/1)
				// | keepCameraAwake (0/1, next after 2.1.9) | textScale (2.2.4: 1.00-2.50, the INI's range - the Appearance
				// slider stops at 2.00 - then the font is re-rasterised as on the slider's release).
				// No name (or no value) just reports them. The write runs on the game's main thread, where the frame reads them.
				const std::string name = JsonStr(args, "name");
				const double value = JsonNum(args, "value", -1000.0);
				bool ok = true;
				std::string error;
				if (!name.empty() && value > -999.0)
				{
					if (name != "pointerSpeed" && name != "wheelSwitchesTabs" && name != "pauseGame" && name != "keepCameraAwake" &&
						name != "textScale")
					{
						ok = false; error = "unknown setting '" + name + "' (pointerSpeed|wheelSwitchesTabs|pauseGame|keepCameraAwake|textScale)";
					}
					else if (name == "pointerSpeed" && !(value >= 0.25 && value <= 4.0))
					{
						ok = false; error = "pointerSpeed takes 0.25-4.00";
					}
					else if (name == "textScale" && !(value >= 1.0 && value <= 2.5))
					{
						ok = false; error = "textScale takes 1.00-2.50";
					}
					else if (auto* task = SKSE::GetTaskInterface())
					{
						task->AddTask([name, value]() {
							auto& v = settings::Get();
							if (name == "pointerSpeed")     { v.pointerSpeed = static_cast<float>(std::round(value * 100.0) / 100.0); }
							else if (name == "wheelSwitchesTabs") { v.wheelSwitchesTabs = value != 0.0; }
							else if (name == "keepCameraAwake") { v.keepCameraAwake = value != 0.0; }
							else if (name == "textScale")
							{
								v.textScale = static_cast<float>(std::round(value * 100.0) / 100.0);
								renderer::RequestFontRebuild();   // as the Appearance slider's release does
							}
							else                             { v.pauseGameWhileOpen = value != 0.0; }
							logger::info("devbench: setting {} -> {} (saved)", name, value);
							settings::Save();
						});
					}
					else
					{
						ok = false; error = "no SKSE task interface";
					}
				}
				const auto& v = settings::Get();
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"setting\"" +
						 (error.empty() ? std::string() : ",\"error\":\"" + error + "\"") +
						 ",\"pointerSpeed\":" + std::to_string(v.pointerSpeed) +
						 ",\"wheelSwitchesTabs\":" + (v.wheelSwitchesTabs ? "true" : "false") +
						 ",\"pauseGame\":" + (v.pauseGameWhileOpen ? "true" : "false") +
						 ",\"keepCameraAwake\":" + (v.keepCameraAwake ? "true" : "false") +
						 ",\"textScale\":" + std::to_string(v.textScale) +
						 ",\"note\":\"a change is applied on the next game frame; read op=state or op=setting again\"}";
			}
			else if (op == "cleanstate")
			{
				// 2.1.9 (AmeliaZ): report and run the orphaned-state clean-up - every <save>.amf-state.ini whose save is gone is
				// deleted (none when the folder holds no save). args value 1 = list only (nothing deleted). Reports the switch
				// too (Settings > General's "Tidy up save state left by deleted saves").
				const bool listOnly = JsonNum(args, "value", 0.0) != 0.0;
				const persistence::CleanResult c = persistence::CleanOrphanedState(listOnly);
				std::string files;
				for (const std::string& f : c.files) { files += (files.empty() ? "\"" : ",\"") + JsonEscape(f) + "\""; }
				result = std::string("{\"ok\":") + (c.ran ? "true" : "false") + ",\"op\":\"cleanstate\",\"listOnly\":" + (listOnly ? "true" : "false") +
						 ",\"enabledAtStart\":" + (settings::Get().cleanOrphanedState ? "true" : "false") +
						 ",\"stateFiles\":" + std::to_string(c.stateFiles) + ",\"orphaned\":" + std::to_string(c.orphaned) +
						 ",\"saves\":" + std::to_string(c.saves) +
						 ",\"deleted\":" + std::to_string(c.deleted) + ",\"failed\":" + std::to_string(c.failed) +
						 ",\"saveFolder\":\"" + JsonEscape(c.saveFolder) + "\"" +
						 (c.error.empty() ? std::string() : ",\"error\":\"" + JsonEscape(c.error) + "\"") +
						 ",\"files\":[" + files + "]}";
			}
			else if (op == "window")
			{
				// 2.3.0 (ssuamier's report; the owner's Move style / Resize style settings): drive the framework window.
				//  {"op":"window"} reports the rect, the screen and the styles.
				//  "moveStyle":0|1|2 (Anywhere|Top edge|Fixed), "resizeStyle":0|1 (Centred|Edges), "freeResize":0|1 set and save
				//  them exactly as the Window tab does (applied on the next game frame).
				//  "place":1 with x,y,w,h (pixels) puts the window there (saved as the player's geometry).
				//  "drag":"left|right|top|bottom|bottomleft|bottomright|topbar|body" with dx,dy (pixels) and optional steps
				//  presses there, moves the cursor in steps (one frame or more each) and releases - the real mouse path - and
				//  reports the rect before and after. Run with the menu open and nothing else holding the mouse.
				std::string error;
				const double moveStyle = JsonNum(args, "moveStyle", -1.0);
				const double resizeStyle = JsonNum(args, "resizeStyle", -1.0);
				const double freeResize = JsonNum(args, "freeResize", -1.0);
				if (moveStyle >= 0.0 || resizeStyle >= 0.0 || freeResize >= 0.0)
				{
					if (moveStyle > 2.0 || resizeStyle > 1.0 || freeResize > 1.0) { error = "moveStyle takes 0-2, resizeStyle 0-1, freeResize 0/1"; }
					else if (auto* task = SKSE::GetTaskInterface())
					{
						task->AddTask([moveStyle, resizeStyle, freeResize]() {
							auto& v = settings::Get();
							if (moveStyle >= 0.0) { v.moveStyle = static_cast<int>(moveStyle); }
							if (resizeStyle >= 0.0) { v.resizeStyle = static_cast<int>(resizeStyle); }
							if (freeResize >= 0.0) { v.freeResize = freeResize != 0.0; }
							logger::info("devbench: window move style {}, resize style {}, resize {} (saved)", v.moveStyle, v.resizeStyle, v.freeResize);
							settings::Save();
						});
					}
					else { error = "no SKSE task interface"; }
				}
				const auto rectJson = [](const renderer::WindowRect& a_r) {
					return "{\"x\":" + std::to_string(static_cast<int>(std::lround(a_r.x))) + ",\"y\":" + std::to_string(static_cast<int>(std::lround(a_r.y))) +
						   ",\"w\":" + std::to_string(static_cast<int>(std::lround(a_r.w))) + ",\"h\":" + std::to_string(static_cast<int>(std::lround(a_r.h))) +
						   ",\"right\":" + std::to_string(static_cast<int>(std::lround(a_r.x + a_r.w))) + ",\"bottom\":" + std::to_string(static_cast<int>(std::lround(a_r.y + a_r.h))) +
						   ",\"onScreen\":" + ((a_r.x >= -0.5f && a_r.y >= -0.5f && a_r.x + a_r.w <= a_r.dispW + 0.5f && a_r.y + a_r.h <= a_r.dispH + 0.5f) ? "true" : "false") + "}";
				};
				std::string extra;
				if (error.empty() && JsonNum(args, "place", 0.0) != 0.0)
				{
					const double x = JsonNum(args, "x", -1.0), y = JsonNum(args, "y", -1.0), w = JsonNum(args, "w", -1.0), h = JsonNum(args, "h", -1.0);
					if (x < 0.0 || y < 0.0 || w <= 0.0 || h <= 0.0) { error = "place needs x, y, w, h (pixels)"; }
					else { renderer::PlaceWindow(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)); }
				}
				const std::string from = JsonStr(args, "drag");
				if (error.empty() && !from.empty())
				{
					const renderer::WindowRect r = renderer::GetWindowRect();
					float px = -1.0f, py = -1.0f;
					if (from == "left") { px = r.x; py = r.y + r.h * 0.5f; }
					else if (from == "right") { px = r.x + r.w - 1.0f; py = r.y + r.h * 0.5f; }
					else if (from == "top") { px = r.x + r.w * 0.5f; py = r.y; }
					else if (from == "bottom") { px = r.x + r.w * 0.5f; py = r.y + r.h - 1.0f; }
					else if (from == "bottomleft") { px = r.x + 3.0f; py = r.y + r.h - 3.0f; }
					else if (from == "bottomright") { px = r.x + r.w - 3.0f; py = r.y + r.h - 3.0f; }
					else if (from == "topbar") { px = r.x + r.w * 0.5f; py = r.bandY >= 0.0f ? r.bandY : r.y + 12.0f; }
					else if (from == "body") { px = r.gapX; py = r.gapY; }
					if (px < 0.0f || py < 0.0f) { error = "drag takes left|right|top|bottom|bottomleft|bottomright|topbar|body, with the menu open"; }
					else
					{
						const float dx = static_cast<float>(JsonNum(args, "dx", 0.0));
						const float dy = static_cast<float>(JsonNum(args, "dy", 0.0));
						const int steps = std::clamp(static_cast<int>(JsonNum(args, "steps", 10.0)), 1, 60);
						// Each record lands on its own frame (trickling is off), so every step waits a few frames' time.
						const auto wait = [](int a_ms) { std::this_thread::sleep_for(std::chrono::milliseconds(a_ms)); };
						input::SetCursorAbsolute(px, py);
						wait(150);   // hover first: ImGui's resize borders answer only once hovered
						input::QueueMouseButton(0, true);
						wait(100);
						for (int i = 1; i <= steps; ++i)
						{
							input::SetCursorAbsolute(px + dx * static_cast<float>(i) / static_cast<float>(steps), py + dy * static_cast<float>(i) / static_cast<float>(steps));
							wait(50);
						}
						wait(100);
						input::QueueMouseButton(0, false);
						wait(200);
						extra = ",\"drag\":\"" + from + "\",\"pressAt\":[" + std::to_string(static_cast<int>(px)) + "," + std::to_string(static_cast<int>(py)) +
								"],\"dx\":" + std::to_string(static_cast<int>(dx)) + ",\"dy\":" + std::to_string(static_cast<int>(dy)) + ",\"before\":" + rectJson(r);
						logger::info("devbench: window drag {} by {:.0f},{:.0f} from {:.0f},{:.0f}", from, dx, dy, px, py);
					}
				}
				const renderer::WindowRect now = renderer::GetWindowRect();
				const auto& v = settings::Get();
				result = std::string("{\"ok\":") + (error.empty() ? "true" : "false") + ",\"op\":\"window\"" +
						 (error.empty() ? std::string() : ",\"error\":\"" + error + "\"") + extra +
						 ",\"rect\":" + rectJson(now) +
						 ",\"display\":[" + std::to_string(static_cast<int>(now.dispW)) + "," + std::to_string(static_cast<int>(now.dispH)) + "]" +
						 ",\"moveStyle\":" + std::to_string(v.moveStyle) + ",\"resizeStyle\":" + std::to_string(v.resizeStyle) +
						 ",\"freeResize\":" + (v.freeResize ? "true" : "false") +
						 ",\"saved\":[" + std::to_string(v.hotkeyWindow.x) + "," + std::to_string(v.hotkeyWindow.y) + "," + std::to_string(v.hotkeyWindow.w) + "," +
						 std::to_string(v.hotkeyWindow.h) + "]}";
			}
			else if (op == "mouse")
			{
				// 2.1.1: press OR release a mouse button (args button, down true/false), so a test can drag - press on a
				// window edge, move the cursor with op=cursor, release - and read state's mainWindow size after.
				const int button = static_cast<int>(JsonNum(args, "button", 0));
				const bool down = args.find("\"down\":true") != std::string::npos;
				input::QueueMouseButton(static_cast<std::uint32_t>(button), down);
				result = std::string("{\"ok\":true,\"op\":\"mouse\",\"button\":") + std::to_string(button) + ",\"down\":" + (down ? "true" : "false") + "}";
			}
			else if (op == "bounds")
			{
				// 1.8.0: measure any clip of the open journal as screen fractions (args path), so a new
				// art replacer's pane is measured rather than guessed at.
				const std::string path = JsonStr(args, "path");
				float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
				const bool ok = systemrow::MeasurePath(path, x, y, w, h);
				std::string esc; for (char c : path) { if (c == '"' || c == '\\') { esc += '\\'; } esc += c; }
				result = std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"bounds\",\"path\":\"" + esc +
					"\",\"x\":" + std::to_string(x) + ",\"y\":" + std::to_string(y) + ",\"w\":" + std::to_string(w) + ",\"h\":" + std::to_string(h) +
					",\"paneLeft\":" + std::to_string(systemrow::PaneLeft()) + "}";
			}
			else if (op == "systemrow")
			{
				// 1.8.4: the live journal category list - every row in order, and whether our press
				// listener is attached. Our row's index moves under us (SetShowMod splices a Mod Manager
				// row in at index 2), so this is how a "the row does nothing" report is actually checked.
				result = std::string("{\"ok\":true,\"op\":\"systemrow\",\"systemRow\":") + systemrow::ListJson() + "}";
			}
			else if (op == "style")
			{
				// 1.7.7: the style colours the menu is DRAWING with, for the "blue selection" report.
				const ImVec4* c = ImGui::GetStyle().Colors;
				auto hex = [](const ImVec4& v) {
					char b[16];
					std::snprintf(b, sizeof(b), "#%02X%02X%02X%02X", (int)(v.x * 255), (int)(v.y * 255), (int)(v.z * 255), (int)(v.w * 255));
					return std::string(b);
				};
				result = std::string("{\"ok\":true,\"op\":\"style\",\"theme\":\"") + theme::GetActiveTheme().id +
					"\",\"Header\":\"" + hex(c[ImGuiCol_Header]) + "\",\"HeaderHovered\":\"" + hex(c[ImGuiCol_HeaderHovered]) +
					"\",\"HeaderActive\":\"" + hex(c[ImGuiCol_HeaderActive]) + "\",\"TabActive\":\"" + hex(c[ImGuiCol_TabActive]) +
					"\",\"NavHighlight\":\"" + hex(c[ImGuiCol_NavHighlight]) + "\",\"WindowBg\":\"" + hex(c[ImGuiCol_WindowBg]) +
					"\",\"TextSelectedBg\":\"" + hex(c[ImGuiCol_TextSelectedBg]) + "\",\"alpha\":" + std::to_string(ImGui::GetStyle().Alpha) + "}";
			}
			else if (op == "textentry")
			{
				// TEST HOOK (2026-09-21, phbd01's "typing stops after a while"): plays the part of another mod
				// that releases text input it never took, by lowering ControlMap's text-entry count by "drop"
				// (default 0 = just report). Marshalled to the game thread, where the count is read. The
				// proof is then a REAL key typed into a focused field - this op only sets the stage.
				const int drop = static_cast<int>(JsonNum(args, "drop", 0));
				auto* controls = RE::ControlMap::GetSingleton();
				if (!controls) { result = "{\"ok\":false,\"error\":\"no ControlMap\"}"; }
				else
				{
					const int before = controls->GetRuntimeData().textEntryCount;
					if (drop > 0)
					{
						if (auto* task = SKSE::GetTaskInterface())
						{
							task->AddTask([drop]() {
								if (auto* c = RE::ControlMap::GetSingleton())
								{
									for (int i = 0; i < drop && i < 16; ++i) { c->AllowTextInput(false); }
									logger::info("devbench: textentry test dropped the text-entry count by {} (now {})",
												 drop, static_cast<int>(c->GetRuntimeData().textEntryCount));
								}
							});
						}
					}
					result = "{\"ok\":true,\"op\":\"textentry\",\"count\":" + std::to_string(before) +
							 ",\"drop\":" + std::to_string(drop) + ",\"wantsText\":" +
							 (renderer::WantsTextInput() ? "true" : "false") + "}";
				}
			}
			else if (op == "keyboard")
			{
				// 2.3.0 THE UNIVERSAL KEYBOARD (Keyboard.h): do=state|open [menu]|close|press row col|type text|key name|layout name
				const std::string what = JsonStr(args, "do");
				const std::string arg = !JsonStr(args, "text").empty() ? JsonStr(args, "text") :
										!JsonStr(args, "name").empty() ? JsonStr(args, "name") : JsonStr(args, "menu");
				result = keyboard::ToolJson(what, arg, static_cast<int>(JsonNum(args, "row", -1)), static_cast<int>(JsonNum(args, "col", -1)));
			}
			else if (op == "gamefield")
			{
				// 2.3.0: the game half (GameField.h). do=probe [menu] (READ-ONLY: which menus and fields are found, which member
				// paths answer, and per route how many edits reached the field) | state | start [menu] | detect | route n
				result = gamefield::ToolJson(args);
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
			AMF_REQUIRE_VOID(a_write);   // DevBench always passes its writer; without one there is no one to answer
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			if (op == "kill")
			{
				// Answer BEFORE terminating, so the caller gets a reply rather than a dropped socket.
				a_write(a_sink, "{\"ok\":true,\"op\":\"kill\",\"note\":\"terminating now\"}");
				watchdog::KillNow("amf.process kill requested over DevBench");
			}

			// op=skin - what menu art actually loaded, and op=skinreload - load it again from
			// disk without restarting the game, so a UI author can iterate on their PNGs live.
			if (op == "skin")
			{
				a_write(a_sink, (std::string(R"({"ok":true,"op":"skin","skin":)") + skin::StatusJson() + "}").c_str());
				return;
			}
			if (op == "skinreload")
			{
				// 2.1.9 (theme-switch-fix): on the render thread - the art's mipmaps use the game's immediate context
				renderer::ReloadArtOnRenderThread(3000);
				a_write(a_sink, (std::string(R"({"ok":true,"op":"skinreload","skin":)") + skin::StatusJson() + "}").c_str());
				return;
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

		// Human-readable names for the common DirectInput scan codes the widget reports.
		// Unknown codes fall back to "scan <n>" - the code number is always in the reply too.
		const char* DikName(std::uint32_t a_code)
		{
			switch (a_code)
			{
			case 0x01: return "Escape"; case 0x0F: return "Tab"; case 0x1C: return "Enter";
			case 0x1D: return "Left Ctrl"; case 0x2A: return "Left Shift"; case 0x36: return "Right Shift";
			case 0x38: return "Left Alt"; case 0x39: return "Space"; case 0x3A: return "Caps Lock";
			case 0x0E: return "Backspace"; case 0xC8: return "Up Arrow"; case 0xD0: return "Down Arrow";
			case 0xCB: return "Left Arrow"; case 0xCD: return "Right Arrow";
			case 0x10: return "Q"; case 0x11: return "W"; case 0x12: return "E"; case 0x13: return "R";
			case 0x14: return "T"; case 0x15: return "Y"; case 0x16: return "U"; case 0x17: return "I";
			case 0x18: return "O"; case 0x19: return "P"; case 0x1E: return "A"; case 0x1F: return "S";
			case 0x20: return "D"; case 0x21: return "F"; case 0x22: return "G"; case 0x23: return "H";
			case 0x24: return "J"; case 0x25: return "K"; case 0x26: return "L"; case 0x2C: return "Z";
			case 0x2D: return "X"; case 0x2E: return "C"; case 0x2F: return "V"; case 0x30: return "B";
			case 0x31: return "N"; case 0x32: return "M";
			case 0x02: return "1"; case 0x03: return "2"; case 0x04: return "3"; case 0x05: return "4";
			case 0x06: return "5"; case 0x07: return "6"; case 0x08: return "7"; case 0x09: return "8";
			case 0x0A: return "9"; case 0x0B: return "0";
			case 0x3B: return "F1"; case 0x3C: return "F2"; case 0x3D: return "F3"; case 0x3E: return "F4";
			case 0x3F: return "F5"; case 0x40: return "F6"; case 0x41: return "F7"; case 0x42: return "F8";
			case 0x43: return "F9"; case 0x44: return "F10"; case 0x57: return "F11"; case 0x58: return "F12";
			default: return nullptr;
			}
		}

		// The framework's own reserved-key report, called in-process (same DLL, same export
		// consumers like DEM use through GetProcAddress).
		bool IsReservedKey(std::uint32_t a_code)
		{
			std::int32_t buffer[32]{};
			const auto count = SMF_GetReservedKeyCodes(buffer, static_cast<std::uint32_t>(std::size(buffer)));
			for (std::uint32_t i = 0; i < count && i < std::size(buffer); ++i)
			{
				if (static_cast<std::uint32_t>(buffer[i]) == a_code) { return true; }
			}
			return false;
		}

		// The keybind-capture widget (queue L26): a reusable capture surface for testing binds
		// without going through a mod's own settings page. arm -> the next real (or InputBench-
		// spliced) keyboard/gamepad press is recorded WITHOUT being consumed; state -> what got
		// captured, with a name, the device, and whether the framework reserves that key;
		// rebind -> arms the REAL menu toggle-key rebind (the consuming settings-page path);
		// cancel -> disarms both.
		// MCM loader. Listener thread; mcmloader::ToolJson is thread-safe and a set is queued to the
		// main thread through SKSE's task interface.
		void McmTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			AMF_REQUIRE_VOID(a_write);   // DevBench always passes its writer; without one there is no one to answer
			const std::string result = mcmloader::ToolJson(a_argsJson ? a_argsJson : "{}");
			a_write(a_sink, result.c_str());
		}

		void KeybindTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			AMF_REQUIRE_VOID(a_write);   // DevBench always passes its writer; without one there is no one to answer
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			if (op == "arm")
			{
				input::ArmKeyCapture();
				a_write(a_sink, "{\"ok\":true,\"op\":\"arm\",\"note\":\"next keyboard/gamepad press is recorded, not consumed\"}");
				return;
			}
			if (op == "rebind")
			{
				input::BeginRebindToggleKey();
				a_write(a_sink, "{\"ok\":true,\"op\":\"rebind\",\"note\":\"next keyboard key becomes the menu toggle key; Escape cancels\"}");
				return;
			}
			if (op == "cancel")
			{
				input::CancelKeyCapture();
				a_write(a_sink, "{\"ok\":true,\"op\":\"cancel\"}");
				return;
			}
			if (op == "state" || op.empty())
			{
				const auto packed = input::LastCapturedKey();
				std::string captured = "null";
				if (packed >= 0)
				{
					const auto device = static_cast<std::uint32_t>(packed >> 32);
					const auto code = static_cast<std::uint32_t>(packed & 0xFFFFFFFF);
					const char* name = device == 0 ? DikName(code) : nullptr;
					const char* deviceName = device == 0 ? "keyboard" : (device == 2 ? "gamepad" : "other");
					const std::string nameStr = name ? std::string{ name } : ("scan " + std::to_string(code));
					captured = "{\"code\":" + std::to_string(code) +
							   ",\"device\":\"" + deviceName + "\"" +
							   ",\"name\":\"" + nameStr + "\"" +
							   ",\"reserved\":" + (device == 0 && IsReservedKey(code) ? "true" : "false") + "}";
				}
				const std::string reply =
					std::string("{\"ok\":true") +
					",\"armed\":" + (input::IsKeyCaptureArmed() ? "true" : "false") +
					",\"rebindArmed\":" + (input::IsAwaitingRebind() ? "true" : "false") +
					",\"toggleKey\":" + std::to_string(settings::Get().toggleKey) +
					",\"toggleKeySource\":\"" + settings::ToggleKeySourceName(settings::GetToggleKeySource()) + "\"" +
					",\"captured\":" + captured + "}";
				a_write(a_sink, reply.c_str());
				return;
			}
			a_write(a_sink, "{\"ok\":false,\"error\":\"op must be arm|state|rebind|cancel\"}");
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
			"op: open|close|select|activate|state|alias|move|separator (action add|remove|send|collapse|favourite)|resetorder|theme|language|font|nav|focus|cursor|click "
			"(args language: a translation file name or auto). For select, node is a path: settings, controls, help "
			"(pre-1.4.4 system/... paths are still accepted) or mod:<index>. "
			"activate is a no-op in the SMF shape (kept for compatibility). alias renames a mod's menu entry "
			"(args mod, name; empty name clears it); move sends it to a 1-based position (args mod, position) and "
			"re-flows the rest; resetorder returns the list to alphabetical; theme switches the active theme by "
			"registry id (arg id: skyrim, untarnished). nav presses the D-pad (arg dir: left|right) - inside a "
			"mod with several sections that walks its tabs, and left at the first tab returns to the mod list; "
			"focus puts navigation in a pane (arg pane: list|options). state returns visibility, the "
			"selected node, the open mod's page/pageIndex/pageCount, every registered mod + its pages, "
			"the player-facing displayOrder, and - for diagnosing consumer hotkeys - blockingWindowOpen "
			"(exactly what IsAnyBlockingWindowOpened answers a mod) plus consumerWindows, each with its "
			"open/blocking flags and view name, so a window latched open names itself; since 2.0.4 each also "
			"carries acceptsMouse and submitted (the top-level ImGui windows it drew last frame: name, flags, "
			"noMouseInputs, noInputs, pos, size), and consumerInput says whether a mod's window holds the "
			"input (open, blocking AND a submitted window that takes the mouse). "
			"2.1.7: mousemove queues a relative mouse movement in raw counts (args dx, dy) through the real pointer scaling; "
			"setting sets pointerSpeed (0.25-4.00), wheelSwitchesTabs or pauseGame (0/1), or 2.2.4 textScale (1.00-2.50, font re-rasterised) and saves the INI (args name, value; "
			"no args reports them); state carries pointer {speed, gameCursorSpeed, scale}, wheelSwitchesTabs, pauseGame and "
			"pauseHeld (the game is paused by this menu now). The wheel over a row of tabs (a mod's, or 2.1.9 Settings / Controls / Help "
			"and the settings sub-tab rows; state.tabBars lists each bar drawn last frame as x0,y0,x1,y1, outer first): op=cursor onto it, then "
			"op=inject device=mouse code=9 (down, next tab) or code=8 (up). "
			"2.1.9: cleanstate reports and runs the orphaned save-state clean-up - every <save>.amf-state.ini whose .ess is gone is DELETED "
			"(nothing when the folder holds no .ess); args value=1 lists only; returns saves, stateFiles, orphaned, deleted, failed, "
			"saveFolder, files and enabledAtStart (the Settings > General switch). "
			"2.1.9: ini drives the INI-only mods' pages (SSE Display Tweaks): do=list | get {mod} | set {mod, section, name, value} - set "
			"writes the mod's override file exactly as the page's control does (value checked against the key's kind). "
			"Next version: mfm drives the Mod Function Menu entries: do=list (each entry's path, dll, api, type, actions, resolved, "
			"missing; mfmLoaded = Mod Function Menu's own DLL is loaded too) | run {path} - presses the entry as the page does, on the render thread, and returns ok, type, message (the "
			"text a Message/MessageBox function returned) and error. "
			"2.2.1: liveapply reports live apply on converted pages ([MCM] bLiveApply): enabled, pending, pendingMod, pendingQuietFor, "
			"inFlight, openMod, lastMod, lastEvent, lastDecision, onSettingChangeSent, liveClosesSent, liveOpensSent, skipped, dropped, "
			"neverList, modsLookedAt (each MCM Helper mod's OnConfigClose: native, Papyrus, none of its own, unreadable) and prisma "
			"{pending, pendingMod, appliedSent, dropped, last}; arg on=0|1 sets and saves the switch. "
			"2.2.0: imgui drives Framework > Other menus (other mods' own menus): do=list (id, name, loaded, shown, module, "
			"method api|key, key, dik, keyFrom, blocked, state -1 not known / 0 closed / 1 open, presses, lastOutcome; loadGuard) | open {id} | close {id} "
			"(ids oar, cs, ied, optiscaler, enb) - as the page's buttons: open closes this menu first, then OAR's API call or the mod's key "
			"(SendInput, game in the foreground only) over the next frames; refused while the game is loading or before its main menu. "
			"list carries each mod's path (game: its key is pressed into the game's own input events; win32: SendInput), injectorReady, and hold (AMF holds this key: applies, available, held, restored - a mod manager put the file back -, amfKey - the free key AMF holds or would hold -, configKey, liveKey, pending, why, config, backup); "
			"hold {id, on} writes AMF's key into that mod's own config (on) or the original back from <config>.amf-key-backup (off), as the row's switch. "
			"2.3.0 the universal keyboard: keyboard do=state (keyboard + game snapshot) | open [menu=<game menu>: its start gesture] | close | "
			"press {row, col} | type {text} | key {name: back|space|clear|shift|enter|done|a|b} | layout {name: en|de|fr|es|it|pl|cs|ru|ja|ko|next|prev}; "
			"gamefield do=probe [menu] (READ-ONLY: open menus, adapter, which member paths answer, the focused field, its type and text, "
			"158716/B612, per route sent/proven/unchanged) | state | start [menu] | detect | route {n: 1|2|3, 0 = automatic}. "
			"2.3.0 settings presets: presets do=list | save {name, skip:[group keys]} | diff {name, shipped} | load {keys:[group keys], after diff; "
			"default the mods switched on} | delete {name} | status (busy, phase, last, inUse, compare {ready, rows, notInstalled, ticked, groups}). "
			"save / diff / load start the work and return; poll status.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"},"
			"\"mod\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"},\"position\":{\"type\":\"string\"},"
			"\"id\":{\"type\":\"string\"},\"dir\":{\"type\":\"string\"},\"pane\":{\"type\":\"string\"},"
			"\"dx\":{\"type\":\"number\"},\"dy\":{\"type\":\"number\"},\"value\":{\"type\":\"number\"}}},"
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

		constexpr const char* keybindDescriptor =
			"{"
			"\"description\":\"Keybind-capture widget for testing binds. op: arm (record the next "
			"keyboard/gamepad press WITHOUT consuming it) | state (armed flags, current toggle key, last "
			"captured key with name/device/reserved verdict) | rebind (arm the real menu toggle-key "
			"rebind; Escape cancels) | cancel.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("amf.keybind", keybindDescriptor, &KeybindTool, nullptr))
		{
			logger::info("Registered \"amf.keybind\" (capture widget) with DevBench");
		}

		// MCM loader: read and drive the MCM Helper pages AMF builds from config.json.
		constexpr const char* mcmDescriptor =
			"{"
			"\"description\":\"MCM loader (MCM Helper menus). op: list (mods drawn, pages, controls, phase-2 rows, "
			"config-script state, and skipped mods with reasons) | get {mod,id} | set {mod,id,value} (written through "
			"MCM.SetModSetting* then OnSettingChange, the same path as the page) | script {mod}.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"mod\":{\"type\":\"string\"},"
			"\"id\":{\"type\":\"string\"},\"value\":{}}},"
			"\"readOnly\":false"
			"}";
		if (dev->RegisterTool("amf.mcm", mcmDescriptor, &McmTool, nullptr))
		{
			logger::info("Registered \"amf.mcm\" (MCM loader) with DevBench");
		}

		// Rule 64's start-menu extension: the vanilla Main Menu driver (amf.mainmenu).
		mainmenudriver::Register(dev);

		registered = true;
	}
}
