#include "TomlPages.h"

#include "GsoCore.h"
#include "HelpBar.h"
#include "IniCore.h"
#include "IniPages.h"
#include "PageRefresh.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tomlpages
{
	namespace
	{
		namespace fs = std::filesystem;
		using tomlcore::Control;
		using tomlcore::TomlType;

		constexpr const char* kRoot = "Data/SKSE/Plugins";
		constexpr const char* kGsoFolder = "ccld_GameSettingsOverride";
		constexpr const char* kMfmFolder = "ccld_ModFunctionMenu";
		constexpr std::uintmax_t kMaxBytes = 256 * 1024;
		constexpr double kSettleSeconds = 0.4;   // a reload waits this long after the last change, and for the control to be let go

		// a value's edit state while its control is in use (render thread, under g_lock)
		struct Row
		{
			bool pending = false;     // changed and still in use: written when it is let go (and the wheel has rested)
			double wheelAt = -1.0;    // ImGui time of the last wheel notch
			double f = 0.0;
			long long i = 0;
			float color[4]{};
			char text[512]{};
		};

		struct File
		{
			std::string id;       // "skse/plugins/enginefixes.toml", "skse/plugins/ccld_gamesettingsoverride/x.toml"
			std::string name;     // "EngineFixes.toml"
			std::string base;     // "EngineFixes"
			std::string path;     // "Data/SKSE/Plugins/EngineFixes.toml"
			tomlcore::Document doc;
			std::vector<Row> rows;   // one per doc.entries
			bool backupTaken = false;
			int writes = 0;
		};

		struct Tab
		{
			std::string name;     // registered page name
			std::size_t file = 0;
			std::size_t section = 0;
			bool wholeFile = false;   // a folder page's tab: every table of the file, as headings
		};

		struct Page
		{
			std::string id;       // a top-level file's id, or "skse/plugins/<folder>/" for a folder
			std::string folder;   // "" for a top-level file
			bool gso = false;     // Game Settings Override's folder
			std::string name;     // shown
			std::string entry;    // registered: "<name> (TOML)"; GSO: "Game Settings Override"
			std::string base;     // the file's or folder's own name, for the own-page match
			std::vector<std::string> matchNames;
			std::vector<std::size_t> files;   // g_files indices, in load order
			std::vector<Tab> tabs;
			bool registered = false;
			std::string ownPage;
			bool reloadLooked = false;
			pagerefresh::Reload reload;
			bool reloadPending = false;
			double pendingSince = 0.0;
			std::string lastError;
			std::string lastApply;
		};

		struct Skipped
		{
			std::string where;    // "Data/SKSE/Plugins" or "Data/SKSE/Plugins/<folder>"
			std::string file;
			std::string reason;
		};

		std::mutex g_lock;              // guards everything below (the render thread draws and writes, DevBench lists)
		std::vector<File> g_files;      // filled once by Load; never resized after
		std::vector<Page> g_pages;
		std::vector<Skipped> g_skipped;
		bool g_loaded = false;

		std::string J(const std::string& a_s) { return nlohmann::json(a_s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		double Now() { return ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0; }

		// ---- the game's setting (Game Settings Override's files) --------------------------------------------------

		struct Live
		{
			RE::Setting* setting = nullptr;   // null: the game has no setting by this name
			gsocore::GameType type = gsocore::GameType::kUnknown;
		};

		Live LiveOf(const std::string& a_key)
		{
			Live out;
			auto* coll = RE::GameSettingCollection::GetSingleton();
			if (!coll)
			{
				static bool s_warned = false;
				if (!s_warned) { s_warned = true; logger::warn("toml: the game's setting collection is not there yet - GSO live values are not shown"); }
				return out;
			}
			out.setting = coll->GetSetting(a_key.c_str());
			if (out.setting)
			{
				// the type by the engine's own name's first letter (GsoCore.h) - the lookup ignores case
				const char* name = out.setting->GetName();
				out.type = gsocore::GameTypeFromName(name ? std::string(name) : a_key);
			}
			return out;
		}

		// a colour setting's four bytes in memory order red, green, blue, alpha - read and written as bytes so both build
		// lines (a Color struct on SE/AE, a uint32 over the same bytes on 1.7) agree
		std::uint32_t ReadColor(const RE::Setting& a_s)
		{
			std::uint8_t p[4];
			std::memcpy(p, &a_s.data, 4);
			return tomlcore::ColorFromParts(p);
		}

		std::string LiveText(const Live& a_l, int a_decimals)
		{
			if (!a_l.setting) { return {}; }
			const RE::Setting& s = *a_l.setting;
			switch (a_l.type)
			{
			case gsocore::GameType::kBool: return tomlcore::FormatBool(s.data.b);
			case gsocore::GameType::kFloat: return tomlcore::FormatFloat(s.data.f, a_decimals);
			case gsocore::GameType::kInt: return std::to_string(s.data.i);
			case gsocore::GameType::kUInt: return std::to_string(s.data.u);
			case gsocore::GameType::kColor: return tomlcore::FormatColor(ReadColor(s));
			case gsocore::GameType::kString: return tomlcore::FormatString(s.data.s ? s.data.s : "", false);
			default: return "?";
			}
		}

		bool SetEngine(const Live& a_l, const tomlcore::Value& a_v)
		{
			if (!a_l.setting) { return false; }
			RE::Setting& s = *a_l.setting;
			using gsocore::GameType;
			switch (a_l.type)
			{
			case GameType::kBool:
				if (a_v.type != TomlType::kBool) { return false; }
				s.data.b = a_v.b;
				return true;
			case GameType::kFloat:
				if (a_v.type != TomlType::kFloat && a_v.type != TomlType::kInt) { return false; }
				s.data.f = static_cast<float>(a_v.type == TomlType::kFloat ? a_v.f : static_cast<double>(a_v.i));
				return true;
			case GameType::kInt:
				if (a_v.type != TomlType::kInt) { return false; }
				s.data.i = static_cast<std::int32_t>(a_v.i);
				return true;
			case GameType::kUInt:
				if (a_v.type != TomlType::kInt) { return false; }
				s.data.u = static_cast<std::uint32_t>(a_v.i);
				return true;
			case GameType::kColor:
			{
				if (a_v.type != TomlType::kInt) { return false; }
				std::uint8_t p[4];
				tomlcore::ColorParts(a_v.i, p);
				std::memcpy(&s.data, p, 4);
				return true;
			}
			case GameType::kString:
				if (a_v.type != TomlType::kString) { return false; }
				s.data.s = pagerefresh::KeepString(a_v.s);   // the game's own string is left where it was
				return true;
			default: return false;
			}
		}

		bool GsoRow(const Page& a_p, const tomlcore::Entry& a_e) { return a_p.gso && a_e.section == 0; }

		// ---- reading -----------------------------------------------------------------------------------------------

		bool ReadBytes(const std::string& a_path, std::string& a_out)
		{
			std::ifstream in(fs::path(a_path), std::ios::binary);
			if (!in.is_open()) { return false; }
			std::stringstream ss;
			ss << in.rdbuf();
			a_out = ss.str();
			return true;
		}

		std::string Why(const tomlcore::ParseError& a_e)
		{
			char buf[512];
			switch (a_e.problem)
			{
			case tomlcore::Problem::kUtf16: return strings::TR("AMF_IniWhyUtf16", "UTF-16 text, which this reader does not take");
			case tomlcore::Problem::kNotSetting:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_TomlWhyLine", "line %d is not a table, a setting (name = value) or a comment"), a_e.line);
				return buf;
			case tomlcore::Problem::kValue:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_TomlWhyValue", "line %d has a value that is not TOML"), a_e.line);
				return buf;
			case tomlcore::Problem::kUnclosed:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_TomlWhyUnclosed", "the text or list that starts on line %d is never closed"), a_e.line);
				return buf;
			case tomlcore::Problem::kKeyTwice:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_TomlWhyKeyTwice", "%s is set twice (line %d)"), a_e.key.c_str(), a_e.line);
				return buf;
			case tomlcore::Problem::kTableTwice:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_TomlWhyTableTwice", "the table [%s] appears twice (line %d)"), a_e.key.c_str(), a_e.line);
				return buf;
			default: return {};
			}
		}

		void Skip(const std::string& a_where, const std::string& a_file, const std::string& a_reason)
		{
			g_skipped.push_back({ a_where, a_file, a_reason });
			logger::info("toml: {}/{} - not shown: {}", a_where, a_file, a_reason);
		}

		std::set<std::string> OnSet() { return tomlcore::ParseIdList(settings::Get().tomlPagesOn); }
		bool PageOn(const Page& a_p, const std::set<std::string>& a_on) { return settings::Get().tomlMenus && a_on.contains(a_p.id); }

		// Called with g_lock held.
		void ApplyVisibilityLocked()
		{
			const std::set<std::string> on = OnSet();
			for (const Page& p : g_pages)
			{
				if (!p.registered) { continue; }
				for (const Tab& t : p.tabs) { registry::SetPageVisible(p.entry.c_str(), t.name.c_str(), PageOn(p, on)); }
			}
		}

		std::vector<const tomlcore::Document*> GsoDocs(const Page& a_p)
		{
			std::vector<const tomlcore::Document*> out;
			for (const std::size_t fi : a_p.files) { out.push_back(&g_files[fi].doc); }
			return out;
		}

		// the page's declared reload, looked up the first time it is needed (MFM's entries are read by then)
		const pagerefresh::Reload& ReloadOf(Page& a_p)
		{
			if (!a_p.reloadLooked)
			{
				a_p.reloadLooked = true;
				std::vector<std::string> names;
				if (a_p.folder.empty()) { names = refreshcore::Candidates(g_files[a_p.files.front()].base, ""); }
				else
				{
					names.push_back(a_p.folder);
					for (const std::size_t fi : a_p.files) { names.push_back(g_files[fi].base); }
				}
				a_p.reload = pagerefresh::Find(names);
				logger::info("toml: {} - {}", a_p.entry, a_p.reload.found ? "reloads in game via " + a_p.reload.decl.dll + " ! " + a_p.reload.decl.api + " (" +
																			 a_p.reload.decl.rel + ")"
																	   : std::string("no declared reload - changes apply the next time the game starts"));
			}
			return a_p.reload;
		}

		pagerefresh::Kind KindOf(Page& a_p)
		{
			const pagerefresh::Reload& r = ReloadOf(a_p);
			return r.found && pagerefresh::Loaded(r) ? pagerefresh::Kind::kReload : pagerefresh::Kind::kNextLaunch;
		}

		// Run the page's declared reload now. Render thread, g_lock held.
		void RunReload(Page& a_p, const std::string& a_why)
		{
			a_p.reloadPending = false;
			const pagerefresh::Reload& r = ReloadOf(a_p);
			if (!r.found) { return; }
			const pagerefresh::Called c = pagerefresh::Call(r);
			const std::string text = pagerefresh::CalledText(r, c);
			a_p.lastApply = a_p.lastApply.empty() ? text : a_p.lastApply + " " + text;
			logger::info("toml: {} - reload ({}): {}", a_p.entry, a_why, text);
		}

		// ---- writing -----------------------------------------------------------------------------------------------

		struct Outcome
		{
			bool ok = false;
			bool changed = false;     // false: the value was already that (nothing written)
			std::string text;         // the value as written
			std::string error;        // English, for the log and DevBench; the page shows it after "Not saved:"
			std::string engine;       // GSO: "set" | "later file: <name>" | "not a game setting" | "type" | ""
			std::string shown;        // the page's line before any reload, in the player's language
		};

		// Write one value (and, for a GSO setting, set the game's value). The page's reload is left pending. Render thread,
		// g_lock held.
		Outcome Commit(Page& a_p, std::size_t a_file, const std::string& a_id, const std::string& a_input)
		{
			Outcome o;
			File& f = g_files[a_file];
			const tomlcore::Entry* cur = tomlcore::Find(f.doc, a_id);
			if (!cur) { o.error = a_id + " is not in " + f.name; return o; }
			const bool gso = GsoRow(a_p, *cur);
			const Live live = gso ? LiveOf(cur->key) : Live{};
			const Control control = gso ? gsocore::ControlFor(live.type, *cur) : tomlcore::ControlForValue(*cur);
			if (!tomlcore::TextFor(control, cur->value, a_input, o.text, o.error)) { o.error = a_id + " " + o.error; return o; }

			// the file as it is on disk NOW (it may have been edited since the game started) must still read cleanly
			std::string bytes;
			if (!ReadBytes(f.path, bytes)) { o.error = "could not read " + f.path; return o; }
			tomlcore::Document disk;
			tomlcore::ParseError pe;
			if (!tomlcore::Parse(bytes, disk, pe)) { o.error = f.name + " no longer reads cleanly (" + Why(pe) + ")"; return o; }
			const tomlcore::Entry* onDisk = tomlcore::Find(disk, a_id);
			if (!onDisk || !onDisk->Editable()) { o.error = a_id + " is no longer a value in " + f.name + " that can be written"; return o; }
			if (onDisk->raw == o.text)
			{
				f.doc = std::move(disk);
				f.rows.assign(f.doc.entries.size(), Row{});
				o.ok = true;
				return o;
			}
			const std::string oldText = onDisk->raw;
			std::string next;
			tomlcore::Document after;
			if (!tomlcore::WithValue(disk, a_id, o.text, next) || !tomlcore::Parse(next, after, pe) || !tomlcore::Find(after, a_id))
			{
				o.error = "the new line for " + a_id + " would not read back - nothing written";
				return o;
			}
			std::error_code ec;
			if (!f.backupTaken)
			{
				// the file as it first was, kept once (an older backup is never replaced)
				const std::string backup = f.path + ".amf-backup";
				if (!fs::exists(fs::path(backup), ec))
				{
					fs::copy_file(fs::path(f.path), fs::path(backup), ec);
					if (ec) { o.error = "could not keep a backup (" + ec.message() + ")"; return o; }
					logger::info("toml: {} kept as {} before the first change", f.path, backup);
				}
				f.backupTaken = true;
			}
			const std::string tmp = f.path + ".amf-tmp";
			{
				std::ofstream out(fs::path(tmp), std::ios::binary | std::ios::trunc);
				if (!out.is_open()) { o.error = "could not write " + tmp; return o; }
				out << next;
				out.flush();
				if (!out.good()) { o.error = "the write to " + tmp + " failed"; return o; }
			}
			fs::rename(fs::path(tmp), fs::path(f.path), ec);
			if (ec)
			{
				o.error = "could not put the new file in place (" + ec.message() + ")";
				fs::remove(fs::path(tmp), ec);
				return o;
			}
			f.doc = std::move(after);
			f.rows.assign(f.doc.entries.size(), Row{});
			++f.writes;
			o.ok = true;
			o.changed = true;
			logger::info("toml: {} = {} written to {} (was {}; that value only)", a_id, o.text, f.path, oldText);

			std::string shown = strings::TR("AMF_GsoSaved", "Saved.");
			if (gso)
			{
				// the game's value at once - only when this file is the one that wins the key
				const tomlcore::Entry* written = tomlcore::Find(f.doc, a_id);
				const auto docs = GsoDocs(a_p);
				const int winnerPos = gsocore::Winner(docs, written ? written->key : a_id);
				const std::size_t winner = winnerPos >= 0 ? a_p.files[static_cast<std::size_t>(winnerPos)] : SIZE_MAX;
				char buf[512];
				if (!live.setting)
				{
					o.engine = "not a game setting";
					shown += std::string(" ") + strings::TR("AMF_GsoSetUnknown", "The game has no setting by this name, so nothing was set in the game.");
				}
				else if (winner != a_file)
				{
					o.engine = "later file: " + (winner != SIZE_MAX ? g_files[winner].name : std::string("?"));
					std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoSetLater", "%s sets it later and wins, so the game keeps that file's value."),
								  winner != SIZE_MAX ? g_files[winner].name.c_str() : "?");
					shown += std::string(" ") + buf;
				}
				else if (written && SetEngine(live, written->value))
				{
					o.engine = "set";
					shown += std::string(" ") + strings::TR("AMF_GsoSetLive", "The game's value was set at once.");
				}
				else { o.engine = "type"; }
				logger::info("toml: GSO {} - the game's value: {} (now {})", a_id, o.engine, LiveText(live, written ? written->value.decimals : -1));
			}
			o.shown = shown;
			a_p.lastApply = shown;
			if (ReloadOf(a_p).found)
			{
				a_p.reloadPending = true;
				a_p.pendingSince = Now();
			}
			return o;
		}

		// ---- drawing -----------------------------------------------------------------------------------------------

		int ShownDecimals(const tomlcore::Value& a_v) { return a_v.type == TomlType::kFloat && a_v.decimals >= 1 ? std::min(a_v.decimals, 9) : 6; }

		void SyncRow(Row& a_r, const tomlcore::Entry& a_e)
		{
			const tomlcore::Value& v = a_e.value;
			a_r.f = v.type == TomlType::kFloat ? v.f : static_cast<double>(v.i);
			a_r.i = v.i;
			std::uint8_t p[4];
			tomlcore::ColorParts(v.i, p);
			for (int k = 0; k < 4; ++k) { a_r.color[k] = p[k] / 255.0f; }
			std::snprintf(a_r.text, sizeof(a_r.text), "%s", v.type == TomlType::kString ? v.s.c_str() : a_e.raw.c_str());
		}

		// The mouse wheel on the number box just drawn (as a group): one step per notch. The box takes the wheel while the
		// pointer is on it, so the page does not scroll under it (ImGui's key ownership, as AMF's sliders do).
		int WheelOnGroup()
		{
			ImGuiContext& g = *GImGui;
			if (!ImGui::IsItemHovered() || g.WheelingWindow != nullptr) { return 0; }
			ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGui::GetID("##toml-wheel"));
			const float w = g.IO.MouseWheel;
			return w > 0.0f ? 1 : (w < 0.0f ? -1 : 0);
		}

		std::string ShortRaw(const std::string& a_raw)
		{
			std::string s;
			for (const char c : a_raw) { s += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c; }
			if (s.size() > 160) { s = s.substr(0, 157) + "..."; }
			return s;
		}

		// One value's row. Returns the input to write ("" = none this frame). Render thread, g_lock held.
		std::string DrawRow(Page& a_p, File& a_f, std::size_t a_ri, const std::vector<const tomlcore::Document*>& a_gsoDocs, std::size_t a_fileIndex)
		{
			const tomlcore::Entry& e = a_f.doc.entries[a_ri];
			Row& r = a_f.rows[a_ri];
			const bool gso = GsoRow(a_p, e);
			const Live live = gso ? LiveOf(e.key) : Live{};
			const Control control = gso ? gsocore::ControlFor(live.type, e) : tomlcore::ControlForValue(e);
			if (!r.pending) { SyncRow(r, e); }
			// GSO: the EditorID as the game names it; anything else: the key in words, as on INI pages (the file keeps its own)
			const std::string label = gso ? e.key : inicore::Label(e.key, true);
			ImGui::PushID(static_cast<int>(a_ri));
			std::string input;
			bool now = false;
			ImGui::BeginGroup();
			switch (control)
			{
			case Control::kSwitch:
			{
				bool b = e.value.type == TomlType::kBool ? e.value.b : e.value.i != 0;
				if (widgets::Toggle(label.c_str(), &b)) { input = b ? "true" : "false"; now = true; }
				break;
			}
			case Control::kFloat:
			{
				// a number box stepping ONE unit of the last digit the file shows (rule 68): "1.45" steps 0.01
				const int decimals = ShownDecimals(e.value);
				const double step = std::pow(10.0, -decimals);
				char fmt[16];
				std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
				if (ImGui::InputDouble(label.c_str(), &r.f, step, step * 10.0, fmt)) { r.pending = true; }
				break;
			}
			case Control::kInt:
			{
				int v = static_cast<int>(std::clamp<long long>(r.i, INT32_MIN, INT32_MAX));
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
				if (ImGui::InputInt(label.c_str(), &v, 1, 10)) { r.i = v; r.pending = true; }
				break;
			}
			case Control::kUInt:
			{
				ImU32 v = static_cast<ImU32>(std::clamp<long long>(r.i, 0, 0xFFFFFFFFll));
				const ImU32 one = 1, ten = 10;
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
				if (ImGui::InputScalar(label.c_str(), ImGuiDataType_U32, &v, &one, &ten)) { r.i = v; r.pending = true; }
				break;
			}
			case Control::kInt64:
			{
				ImS64 v = r.i;
				const ImS64 one = 1, ten = 10;
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
				if (ImGui::InputScalar(label.c_str(), ImGuiDataType_S64, &v, &one, &ten)) { r.i = v; r.pending = true; }
				break;
			}
			case Control::kColor:
			{
				// no picker pop-up: its window is outside this row, so "let go" could not be seen while it is open
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
				if (ImGui::ColorEdit4(label.c_str(), r.color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB |
																  ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_AlphaPreviewHalf))
				{
					r.pending = true;
				}
				break;
			}
			case Control::kText:
			case Control::kDate:
			{
				ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
				const bool tooLong = e.value.s.size() >= sizeof(r.text) - 1;
				if (ImGui::InputText(label.c_str(), r.text, sizeof(r.text), tooLong ? ImGuiInputTextFlags_ReadOnly : 0)) { r.pending = true; }
				break;
			}
			default:
			{
				// an array, an inline table or a text over several lines: shown, never written
				ImGui::TextUnformatted(label.c_str());
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::TextWrapped("%s  %s", ShortRaw(e.raw).c_str(), strings::TR("AMF_TomlShownOnly", "(shown only)"));
				ImGui::PopStyleColor();
				break;
			}
			}
			ImGui::EndGroup();
			const bool active = ImGui::IsItemActive();   // the group: any of the control's parts still in use
			const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
			if (control == Control::kFloat || control == Control::kInt || control == Control::kUInt || control == Control::kInt64)
			{
				if (const int dir = WheelOnGroup(); dir != 0)
				{
					if (control == Control::kFloat)
					{
						const double step = std::pow(10.0, -ShownDecimals(e.value));
						r.f = std::round(r.f / step) * step + dir * step;
					}
					else
					{
						r.i += dir;
						if (control == Control::kUInt) { r.i = std::clamp<long long>(r.i, 0, 0xFFFFFFFFll); }
						if (control == Control::kInt) { r.i = std::clamp<long long>(r.i, INT32_MIN, INT32_MAX); }
					}
					r.pending = true;
					r.wheelAt = ImGui::GetTime();
				}
			}
			if (!e.help.empty() && hovered) { helpbar::OfferForLastItem(e.help); }
			const bool wheelResting = r.wheelAt < 0.0 || ImGui::GetTime() - r.wheelAt > kSettleSeconds * 0.75;
			if (r.pending && !active && wheelResting)
			{
				r.pending = false;
				r.wheelAt = -1.0;
				switch (control)
				{
				case Control::kFloat:
				{
					const int decimals = ShownDecimals(e.value);
					const double step = std::pow(10.0, -decimals);
					input = tomlcore::FormatFloat(std::round(r.f / step) * step, decimals);
					break;
				}
				case Control::kInt:
				case Control::kUInt:
				case Control::kInt64: input = std::to_string(r.i); break;
				case Control::kColor:
				{
					std::uint8_t p[4];
					for (int k = 0; k < 4; ++k) { p[k] = static_cast<std::uint8_t>(std::lround(std::clamp(r.color[k], 0.0f, 1.0f) * 255.0f)); }
					input = tomlcore::FormatColor(tomlcore::ColorFromParts(p));
					break;
				}
				case Control::kText:
				case Control::kDate: input = r.text; break;
				default: break;
				}
				now = true;
			}
			if (gso)
			{
				// the game's value beside the file's, and what is worth knowing about the key
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				if (live.setting) { ImGui::Text(strings::TR("AMF_GsoLive", "game: %s"), LiveText(live, ShownDecimals(e.value)).c_str()); }
				else { ImGui::TextUnformatted(strings::TR("AMF_GsoUnknown", "not a game setting - the game has none by this name")); }
				if (live.setting && !gsocore::Matches(live.type, e.value.type))
				{
					ImGui::Indent();
					ImGui::TextWrapped("%s", strings::TR("AMF_GsoMismatch", "The file's value is not written as this setting's type; a change writes it as that type."));
					ImGui::Unindent();
				}
				const int winnerPos = gsocore::Winner(a_gsoDocs, e.key);
				if (winnerPos >= 0 && a_p.files[static_cast<std::size_t>(winnerPos)] != a_fileIndex)
				{
					ImGui::Indent();
					ImGui::TextWrapped(strings::TR("AMF_GsoLaterFile", "%s sets it later and wins."), g_files[a_p.files[static_cast<std::size_t>(winnerPos)]].name.c_str());
					ImGui::Unindent();
				}
				ImGui::PopStyleColor();
			}
			ImGui::PopID();
			return now ? input : std::string();
		}

		void DrawTab(std::size_t a_page, std::size_t a_tab)
		{
			std::scoped_lock l(g_lock);
			if (a_page >= g_pages.size() || a_tab >= g_pages[a_page].tabs.size()) { return; }
			Page& p = g_pages[a_page];
			const Tab t = p.tabs[a_tab];
			File& f = g_files[t.file];
			helpbar::Want("toml|" + f.id + "|" + std::to_string(t.section));

			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			const pagerefresh::Kind kind = KindOf(p);
			ImGui::TextWrapped("%s", pagerefresh::MethodText(kind, p.reload.via).c_str());
			if (p.gso)
			{
				ImGui::TextWrapped(strings::TR("AMF_GsoPageHelp", "Game Settings Override reads its files when the game starts, in A-Z order: when two files "
								   "set the same setting, the later one wins. A change is written into %s itself, only that value; the file as it first "
								   "was is kept as %s.amf-backup."), f.name.c_str(), f.name.c_str());
				if (kind != pagerefresh::Kind::kReload)
				{
					ImGui::TextWrapped("%s", strings::TR("AMF_GsoNotLoaded", "Game Settings Override is not loaded: a change is written to the file and set in "
														   "the game at once, but the file itself is read only when Game Settings Override is installed."));
				}
			}
			else
			{
				const std::string file = f.name;
				ImGui::TextWrapped(strings::TR("AMF_TomlWrites", "A change is written into %s itself, only the value you change; the file as it first was is "
								   "kept as %s.amf-backup."), file.c_str(), file.c_str());
			}
			if (!p.lastError.empty()) { ImGui::TextWrapped(strings::TR("AMF_IniError", "Not saved: %s"), p.lastError.c_str()); }
			if (!p.lastApply.empty()) { ImGui::TextWrapped("%s", p.lastApply.c_str()); }
			ImGui::PopStyleColor();
			if (p.reload.found)
			{
				if (!pagerefresh::Loaded(p.reload)) { ImGui::BeginDisabled(); }
				if (ImGui::Button(strings::TR("AMF_RefreshButton", "Reload")))
				{
					p.lastApply.clear();
					RunReload(p, "Reload button");
				}
				if (!pagerefresh::Loaded(p.reload)) { ImGui::EndDisabled(); }
			}
			ImGui::Spacing();

			const auto docs = p.gso ? GsoDocs(p) : std::vector<const tomlcore::Document*>{};
			std::string commitId, commitInput;
			auto drawSection = [&](std::size_t a_section, bool a_heading) {
				const tomlcore::Section& sec = f.doc.sections[a_section];
				if (sec.entries.empty()) { return; }
				if (a_heading && a_section != 0) { ImGui::SeparatorText(sec.name.c_str()); }
				if (!sec.help.empty() && a_heading)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::TextWrapped("%s", sec.help.c_str());
					ImGui::PopStyleColor();
				}
				ImGui::PushID(static_cast<int>(a_section));
				for (const std::size_t ri : sec.entries)
				{
					if (ri >= f.rows.size()) { continue; }
					std::string input = DrawRow(p, f, ri, docs, t.file);
					if (!input.empty() && commitId.empty()) { commitId = f.doc.entries[ri].id; commitInput = input; }
				}
				ImGui::PopID();
			};
			if (t.wholeFile) { for (std::size_t s = 0; s < f.doc.sections.size(); ++s) { drawSection(s, true); } }
			else
			{
				if (!f.doc.sections[t.section].help.empty())
				{
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::TextWrapped("%s", f.doc.sections[t.section].help.c_str());
					ImGui::PopStyleColor();
				}
				drawSection(t.section, false);
			}
			if (!commitId.empty())
			{
				// after the rows: a write re-reads the file and rebuilds them
				const Outcome o = Commit(p, t.file, commitId, commitInput);
				p.lastError = o.ok ? std::string() : o.error;
				if (!o.ok) { logger::warn("toml: {} {} not set - {}", f.name, commitId, o.error); }
			}
		}

		void ScanFolder(const fs::path& a_dir, const std::string& a_folder, std::vector<std::pair<std::string, std::string>>& a_out)
		{
			// a_out: (folder, file name) in the order found
			std::error_code ec;
			const std::string where = a_folder.empty() ? std::string(kRoot) : std::string(kRoot) + "/" + a_folder;
			std::vector<std::string> names;
			for (const auto& de : fs::directory_iterator(a_dir, ec))
			{
				std::error_code fe;
				if (!de.is_regular_file(fe)) { continue; }
				if (Lower(de.path().extension().string()) != ".toml") { continue; }
				const std::wstring w = de.path().filename().wstring();
				if (std::any_of(w.begin(), w.end(), [](wchar_t c) { return c > 0x7E || c < 0x20; }))
				{
					// its narrow form would not name the same file again
					Skip(where, "?.toml", strings::TR("AMF_IniWhyName", "its file name is not plain ASCII"));
					continue;
				}
				names.push_back(de.path().filename().string());
			}
			const bool gso = Lower(a_folder) == Lower(kGsoFolder);
			if (gso) { gsocore::SortLoadOrder(names); }   // GSO's own order
			else { std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); }); }
			for (const std::string& n : names)
			{
				if (gso && !gsocore::GsoReadsName(n))
				{
					Skip(where, n, strings::TR("AMF_GsoWhyCase", "Game Settings Override reads only names ending in .toml in small letters"));
					continue;
				}
				if (Lower(n).rfind("apocryphamenuframework", 0) == 0) { Skip(where, n, strings::TR("AMF_IniWhyOwn", "AMF's own settings")); continue; }
				a_out.emplace_back(a_folder, n);
			}
		}
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (g_loaded) { return; }   // once per session
		g_loaded = true;
		const fs::path root(kRoot);
		std::error_code ec;
		if (!fs::is_directory(root, ec)) { logger::info("toml: {} is not there", kRoot); return; }
		std::vector<std::pair<std::string, std::string>> found;   // (folder, file)
		ScanFolder(root, "", found);
		std::vector<fs::path> dirs;
		for (const auto& de : fs::directory_iterator(root, ec))
		{
			std::error_code fe;
			if (de.is_directory(fe)) { dirs.push_back(de.path()); }
		}
		std::sort(dirs.begin(), dirs.end(), [](const fs::path& a, const fs::path& b) { return Lower(a.filename().string()) < Lower(b.filename().string()); });
		for (const fs::path& d : dirs)
		{
			const std::wstring w = d.filename().wstring();
			if (std::any_of(w.begin(), w.end(), [](wchar_t c) { return c > 0x7E || c < 0x20; })) { continue; }
			const std::string folder = d.filename().string();
			const std::string lf = Lower(folder);
			if (lf == Lower(kMfmFolder))
			{
				Skip(kRoot, folder + "/", strings::TR("AMF_TomlWhyMfm", "Mod Function Menu's buttons - they are the MFM menu (Settings > Converted menus > MFM)"));
				continue;
			}
			if (lf.rfind("apocryphamenuframework", 0) == 0) { continue; }   // AMF's own folder (themes, art): never settings of another mod
			ScanFolder(d, folder, found);
		}

		// read each file; a folder's files become one page, a top-level file its own
		for (const auto& [folder, name] : found)
		{
			const std::string where = folder.empty() ? std::string(kRoot) : std::string(kRoot) + "/" + folder;
			const fs::path p = folder.empty() ? root / name : root / folder / name;
			const std::uintmax_t bytes = fs::file_size(p, ec);
			if (ec || bytes > kMaxBytes) { Skip(where, name, strings::TR("AMF_IniWhyBig", "over 256 KB - too big for a settings file")); continue; }
			File f;
			f.name = name;
			f.base = name.substr(0, name.size() - 5);
			f.path = p.generic_string();
			f.id = Lower(folder.empty() ? "skse/plugins/" + name : "skse/plugins/" + folder + "/" + name);
			std::string data;
			if (!ReadBytes(f.path, data)) { Skip(where, name, strings::TR("AMF_IniWhyUnread", "could not be opened")); continue; }
			tomlcore::ParseError pe;
			if (!tomlcore::Parse(data, f.doc, pe)) { Skip(where, name, Why(pe)); continue; }
			if (f.doc.entries.empty()) { Skip(where, name, strings::TR("AMF_GsoWhyEmpty", "no settings in it")); continue; }
			f.rows.assign(f.doc.entries.size(), Row{});
			std::size_t editable = 0;
			for (const auto& e : f.doc.entries) { editable += e.Editable() ? 1 : 0; }
			logger::info("toml: {} - {} key(s) ({} editable) in {} table(s){}{}", f.path, f.doc.entries.size(), editable, f.doc.sections.size() - 1,
						 f.doc.bom ? ", BOM" : "", !f.doc.endings.empty() && f.doc.endings.front() == "\r\n" ? ", CRLF" : "");
			g_files.push_back(std::move(f));
			const std::size_t fi = g_files.size() - 1;
			const std::string pageId = folder.empty() ? g_files[fi].id : Lower("skse/plugins/" + folder + "/");
			auto pg = std::find_if(g_pages.begin(), g_pages.end(), [&](const Page& x) { return x.id == pageId; });
			if (pg == g_pages.end())
			{
				Page np;
				np.id = pageId;
				np.folder = folder;
				np.gso = Lower(folder) == Lower(kGsoFolder);
				np.base = folder.empty() ? g_files[fi].base : folder;
				g_pages.push_back(std::move(np));
				pg = g_pages.end() - 1;
			}
			pg->files.push_back(fi);
		}

		// names, entries and tabs
		std::set<std::string> entries;
		for (Page& p : g_pages)
		{
			const std::string dll = inipages::DllProductName(p.base);
			p.name = p.gso ? std::string("Game Settings Override") : (!dll.empty() ? inicore::Label(dll, false) : inicore::Label(p.base, false));
			p.entry = p.gso ? p.name : p.name + " (TOML)";
			if (!entries.insert(Lower(p.entry)).second) { p.entry = p.name + " (TOML, " + (p.folder.empty() ? g_files[p.files.front()].name : p.folder) + ")"; }
			for (const std::string& n : { p.base, dll, p.name })
			{
				if (const std::string k = inicore::Normal(n); !k.empty() && std::find(p.matchNames.begin(), p.matchNames.end(), k) == p.matchNames.end()) { p.matchNames.push_back(k); }
			}
			std::set<std::string> tabNames;
			auto addTab = [&](Tab t) {
				std::string n = t.name;
				for (int k = 2; tabNames.contains(n); ++k) { n = t.name + " (" + std::to_string(k) + ")"; }
				t.name = n;
				tabNames.insert(n);
				p.tabs.push_back(std::move(t));
			};
			if (p.folder.empty())
			{
				const File& f = g_files[p.files.front()];
				for (std::size_t s = 0; s < f.doc.sections.size(); ++s)
				{
					if (f.doc.sections[s].entries.empty()) { continue; }
					addTab({ s == 0 ? inicore::Label(f.base, false) : f.doc.sections[s].name, p.files.front(), s, false });
				}
			}
			else
			{
				for (const std::size_t fi : p.files) { addTab({ p.gso ? g_files[fi].base : inicore::Label(g_files[fi].base, false), fi, 0, true }); }
			}
		}
		logger::info("toml: {} page(s) from {} file(s), {} not shown", g_pages.size(), g_files.size(), g_skipped.size());
	}

	void Sync()
	{
		std::scoped_lock l(g_lock);
		if (g_pages.empty()) { return; }
		// 2.3.0: skipped when neither the registry nor the TOML list changed since the last Sync (as IniPages::Sync)
		static std::uint64_t s_doneGeneration = ~0ull;
		static std::size_t s_donePages = 0;
		if (registry::Generation() == s_doneGeneration && g_pages.size() == s_donePages) { return; }
		std::set<std::string> ours;
		for (const Page& p : g_pages) { ours.insert(p.entry); }
		struct Other
		{
			std::string name, title, module;
		};
		std::vector<Other> others;   // each entry normalised once, not once per page
		for (const registry::Entry& e : registry::Snapshot())
		{
			if (ours.contains(e.modName)) { continue; }
			others.push_back({ e.modName, inicore::Normal(e.modName), inicore::Normal(e.module) });
		}
		bool changed = false;
		for (std::size_t i = 0; i < g_pages.size(); ++i)
		{
			Page& p = g_pages[i];
			if (!p.ownPage.empty()) { continue; }
			std::string own;
			const std::string base = inicore::Normal(p.base);
			for (const Other& e : others)
			{
				if (inicore::IsOwnPageNormal(p.matchNames, base, e.title, e.module)) { own = e.name; break; }
			}
			if (!own.empty())
			{
				p.ownPage = own;
				if (p.registered)
				{
					registry::Remove(p.entry, "");
					p.registered = false;
				}
				logger::info("toml: {} - no page: it has its own page here (\"{}\")", p.entry, own);
				continue;
			}
			if (p.registered) { continue; }
			for (std::size_t t = 0; t < p.tabs.size(); ++t)
			{
				if (!registry::RegisterFn(p.entry.c_str(), p.tabs[t].name.c_str(), [i, t]() { DrawTab(i, t); }, true))
				{
					logger::warn("toml: tab \"{}\" of \"{}\" NOT registered (the name is taken)", p.tabs[t].name, p.entry);
				}
			}
			p.registered = true;
			changed = true;
		}
		if (changed) { ApplyVisibilityLocked(); }
		s_doneGeneration = registry::Generation();   // after our own registrations, so they do not count as a change
		s_donePages = g_pages.size();
	}

	void Tick()
	{
		static std::uint64_t s_seen = 0;   // render thread only
		const std::uint64_t now = registry::Generation();
		if (now != s_seen)
		{
			s_seen = now;
			Sync();
		}
		if (!ImGui::GetCurrentContext()) { return; }
		std::scoped_lock l(g_lock);
		const double t = ImGui::GetTime();
		for (Page& p : g_pages)
		{
			if (!p.reloadPending) { continue; }
			// once the controls are let go and the last change has rested - one reload for a run of changes, not one per notch
			if (ImGui::IsAnyItemActive() || t - p.pendingSince < kSettleSeconds) { continue; }
			RunReload(p, "after a save");
		}
	}

	void ApplyVisibility()
	{
		std::scoped_lock l(g_lock);
		ApplyVisibilityLocked();
		const std::set<std::string> on = OnSet();
		std::size_t shown = 0;
		for (const Page& p : g_pages) { shown += p.registered && PageOn(p, on) ? 1 : 0; }
		logger::info("toml: {} of {} page(s) shown{}", shown, g_pages.size(), settings::Get().tomlMenus ? "" : " (all off)");
	}

	void DrawSettingsList()
	{
		Sync();
		std::scoped_lock l(g_lock);
		const std::set<std::string> on = OnSet();
		bool any = false;
		for (Page& p : g_pages)
		{
			if (!p.ownPage.empty()) { continue; }
			any = true;
			bool pageOn = on.contains(p.id);
			const std::string label = p.entry + "##toml-" + p.id;
			if (widgets::Toggle(label.c_str(), &pageOn))
			{
				settings::Get().tomlPagesOn = tomlcore::WithId(settings::Get().tomlPagesOn, p.id, pageOn);
				logger::info("settings page: TOML page {} -> {}", p.id, pageOn ? "on" : "off");
				settings::Save();
				ApplyVisibilityLocked();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::Indent();
			std::size_t keys = 0;
			std::string files;
			for (const std::size_t fi : p.files)
			{
				keys += g_files[fi].doc.entries.size();
				files += (files.empty() ? "" : ", ") + g_files[fi].name;
			}
			char count[96];
			std::snprintf(count, sizeof(count), strings::TRN("AMF_IniSettings", static_cast<long long>(keys), "%d setting", "%d settings"), static_cast<int>(keys));
			ImGui::TextWrapped("%s - %s", count, (p.folder.empty() ? files : p.folder + "/: " + files).c_str());
			ImGui::TextWrapped("%s", pagerefresh::MethodText(KindOf(p), p.reload.via).c_str());
			ImGui::Unindent();
			ImGui::PopStyleColor();
		}
		if (!any)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::TextWrapped("%s", strings::TR("AMF_TomlNone", "No TOML file with settings was found in Data/SKSE/Plugins or its folders."));
			ImGui::PopStyleColor();
		}
		bool heading = false;
		auto line = [&](const std::string& a_where, const std::string& a_file, const std::string& a_reason) {
			if (!heading)
			{
				heading = true;
				ImGui::Spacing();
				ImGui::SeparatorText(strings::TR("AMF_IniSkipped", "Not shown"));
			}
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::BulletText("%s/%s: %s", a_where.c_str(), a_file.c_str(), a_reason.c_str());
			ImGui::PopStyleColor();
		};
		char buf[512];
		for (const Page& p : g_pages)
		{
			if (p.ownPage.empty()) { continue; }
			std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyOwnPage", "%s already has its own page here"), p.ownPage.c_str());
			line(kRoot, p.folder.empty() ? g_files[p.files.front()].name : p.folder + "/", buf);
		}
		for (const Skipped& s : g_skipped) { line(s.where, s.file, s.reason); }
	}

	std::string ToolJson(const std::string& a_argsJson, bool a_gso)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		const std::string op = a_gso ? "gso" : "toml";
		auto inScope = [a_gso](const Page& a_p) { return !a_gso || a_p.gso; };
		// a file: its id, its name, or its name without ".toml"; the page it is on
		auto findFile = [a_gso](const std::string& a_name, std::size_t& a_page) -> int {
			const std::string want = Lower(a_name);
			for (std::size_t pi = 0; pi < g_pages.size(); ++pi)
			{
				if (a_gso && !g_pages[pi].gso) { continue; }
				for (const std::size_t fi : g_pages[pi].files)
				{
					const File& f = g_files[fi];
					if (f.id == want || Lower(f.name) == want || Lower(f.base) == want) { a_page = pi; return static_cast<int>(fi); }
				}
			}
			return -1;
		};
		if (what == "list")
		{
			std::scoped_lock l(g_lock);
			const std::set<std::string> on = OnSet();
			std::string pages;
			for (Page& p : g_pages)
			{
				if (!inScope(p)) { continue; }
				std::string files;
				for (const std::size_t fi : p.files)
				{
					const File& f = g_files[fi];
					files += (files.empty() ? "" : ",") + std::string("{\"file\":") + J(f.id) + ",\"name\":" + J(f.name) + ",\"keys\":" + std::to_string(f.doc.entries.size()) +
							 ",\"tables\":" + std::to_string(f.doc.sections.size() - 1) + ",\"writes\":" + std::to_string(f.writes) + "}";
				}
				const pagerefresh::Reload& r = ReloadOf(p);
				pages += (pages.empty() ? "" : ",") + std::string("{\"page\":") + J(p.id) + ",\"entry\":" + J(p.entry) + ",\"gso\":" + (p.gso ? "true" : "false") +
						 ",\"switch\":" + (on.contains(p.id) ? "true" : "false") + ",\"shown\":" + (p.registered && PageOn(p, on) ? "true" : "false") +
						 ",\"registered\":" + (p.registered ? "true" : "false") + (p.ownPage.empty() ? std::string() : ",\"ownPage\":" + J(p.ownPage)) +
						 ",\"refresh\":" + J(KindOf(p) == pagerefresh::Kind::kReload ? "reload" : "next launch") + ",\"reloadVia\":" + J(r.found ? r.decl.dll + " ! " + r.decl.api : "") +
						 ",\"reloadLoaded\":" + (pagerefresh::Loaded(r) ? "true" : "false") + ",\"lastError\":" + J(p.lastError) + ",\"lastApply\":" + J(p.lastApply) +
						 ",\"files\":[" + files + "]}";
			}
			std::string skipped;
			for (const Skipped& s : g_skipped)
			{
				if (a_gso && Lower(s.where).find(Lower(kGsoFolder)) == std::string::npos) { continue; }
				skipped += (skipped.empty() ? "" : ",") + std::string("{\"where\":") + J(s.where) + ",\"file\":" + J(s.file) + ",\"reason\":" + J(s.reason) + "}";
			}
			return std::string("{\"ok\":true,\"op\":\"") + op + "\",\"do\":\"list\",\"enabled\":" + (settings::Get().tomlMenus ? "true" : "false") +
				   ",\"gsoLoaded\":" + (::GetModuleHandleW(L"ccld_GameSettingsOverride.dll") ? "true" : "false") + ",\"pages\":[" + pages + "],\"skipped\":[" + skipped + "]}";
		}
		if (what == "get")
		{
			std::scoped_lock l(g_lock);
			std::size_t pi = 0;
			const int fi = findFile(str("file"), pi);
			if (fi < 0) { return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"no such file (do=list names them)\"}"; }
			Page& p = g_pages[pi];
			const File& f = g_files[static_cast<std::size_t>(fi)];
			const auto docs = p.gso ? GsoDocs(p) : std::vector<const tomlcore::Document*>{};
			std::string out;
			for (const tomlcore::Entry& e : f.doc.entries)
			{
				const bool gso = GsoRow(p, e);
				const Live live = gso ? LiveOf(e.key) : Live{};
				const Control control = gso ? gsocore::ControlFor(live.type, e) : tomlcore::ControlForValue(e);
				out += (out.empty() ? "" : ",") + std::string("{\"name\":") + J(e.id) + ",\"table\":" + J(f.doc.sections[e.section].name) + ",\"value\":" + J(e.raw) +
					   ",\"toml\":\"" + tomlcore::TypeName(e.value.type) + "\",\"control\":" + J(tomlcore::ControlName(control)) +
					   ",\"editable\":" + (e.Editable() ? "true" : "false") + ",\"help\":" + J(e.help);
				if (gso)
				{
					const int w = gsocore::Winner(docs, e.key);
					out += ",\"game\":\"" + std::string(gsocore::GameTypeName(live.type)) + "\",\"known\":" + (live.setting ? "true" : "false") + ",\"live\":" +
						   J(LiveText(live, ShownDecimals(e.value))) + ",\"matches\":" + (gsocore::Matches(live.type, e.value.type) ? "true" : "false") +
						   ",\"winner\":" + J(w >= 0 ? g_files[p.files[static_cast<std::size_t>(w)]].name : std::string());
				}
				out += "}";
			}
			return "{\"ok\":true,\"op\":\"" + op + "\",\"do\":\"get\",\"file\":" + J(f.id) + ",\"page\":" + J(p.entry) + ",\"settings\":[" + out + "]}";
		}
		if (what == "set" || what == "reload")
		{
			const std::string file = str("file"), name = str("name"), pageArg = str("page");
			std::string value = str("value");
			if (args.is_object() && args.contains("value") && args["value"].is_number()) { value = args["value"].dump(); }
			if (args.is_object() && args.contains("value") && args["value"].is_boolean()) { value = args["value"].get<bool>() ? "true" : "false"; }
			std::size_t pageIndex = 0;
			int fileIndex = -1;
			{
				std::scoped_lock l(g_lock);
				if (what == "set")
				{
					fileIndex = findFile(file, pageIndex);
					if (fileIndex < 0) { return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"no such file (do=list names them)\"}"; }
					if (!tomlcore::Find(g_files[static_cast<std::size_t>(fileIndex)].doc, name))
					{
						return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"no such name in the file (do=get lists them)\"}";
					}
				}
				else
				{
					const std::string want = Lower(pageArg.empty() ? file : pageArg);
					bool found = false;
					for (std::size_t pi = 0; pi < g_pages.size() && !found; ++pi)
					{
						if (inScope(g_pages[pi]) && (g_pages[pi].id == want || Lower(g_pages[pi].entry) == want)) { pageIndex = pi; found = true; }
					}
					if (!found && findFile(want, pageIndex) >= 0) { found = true; }
					if (!found) { return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"no such page (do=list names them)\"}"; }
				}
			}
			// written and reloaded on the render thread, as the page does (a mod's reload is never called from this thread);
			// the result comes back through a shared block, so a job that runs after the wait gave up writes into nothing gone
			auto result = std::make_shared<Outcome>();
			auto applied = std::make_shared<std::string>();
			auto done = std::make_shared<std::atomic<bool>>(false);
			const bool isSet = what == "set";
			const bool ran = renderer::RunOnRenderThreadWait([isSet, pageIndex, fileIndex, name, value, result, applied, done]() {
				std::scoped_lock l(g_lock);
				Page& p = g_pages[pageIndex];
				if (isSet)
				{
					*result = Commit(p, static_cast<std::size_t>(fileIndex), name, value);
					p.lastError = result->ok ? std::string() : result->error;
					if (result->ok && result->changed && p.reloadPending) { RunReload(p, "DevBench set"); }
				}
				else
				{
					p.lastApply.clear();
					RunReload(p, "DevBench reload");
					result->ok = p.reload.found;
					if (!p.reload.found) { result->error = "this page has no declared reload"; }
				}
				*applied = p.lastApply;
				done->store(true);
			}, 5000);
			if (!ran || !done->load())
			{
				return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"no frame took the call within 5 s (is the game drawing?) - it runs at the next frame\"}";
			}
			const Outcome o = *result;
			return std::string("{\"ok\":") + (o.ok ? "true" : "false") + ",\"op\":\"" + op + "\",\"do\":\"" + what + "\",\"file\":" + J(file) + ",\"name\":" + J(name) +
				   ",\"written\":" + J(o.text) + ",\"changed\":" + (o.changed ? "true" : "false") + ",\"engine\":" + J(o.engine) + ",\"shown\":" + J(*applied) +
				   (o.error.empty() ? std::string() : ",\"error\":" + J(o.error)) + "}";
		}
		return "{\"ok\":false,\"op\":\"" + op + "\",\"error\":\"do = list | get {file} | set {file, name, value} | reload {page}\"}";
	}
}
