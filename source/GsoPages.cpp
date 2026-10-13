#include "GsoPages.h"

#include "GsoCore.h"
#include "HelpBar.h"
#include "MfmCore.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gsopages
{
	namespace
	{
		namespace fs = std::filesystem;

		constexpr const char* kFolder = "Data/SKSE/Plugins/ccld_GameSettingsOverride";
		constexpr const wchar_t* kDll = L"ccld_GameSettingsOverride.dll";
		constexpr const char* kExport = "ReloadConfig";   // GSO's MFM entry calls it as a MessageBox function: void(char*, size_t)
		constexpr const char* kEntry = "Game Settings Override";
		constexpr std::uintmax_t kMaxBytes = 256 * 1024;

		// a setting's edit state while its control is being used (render thread, under g_lock)
		struct Row
		{
			bool pending = false;     // the control was changed and is still in use: written when it is let go
			double f = 0.0;
			long long i = 0;
			float color[4]{};
			char text[512]{};
		};

		struct File
		{
			std::string id;       // the file name, lower case - the switch and DevBench name
			std::string name;     // "GSO - No Fall Damage.toml"
			std::string tab;      // the name without ".toml"
			std::string path;     // "Data/SKSE/Plugins/ccld_GameSettingsOverride/GSO - No Fall Damage.toml"
			gsocore::Document doc;
			std::vector<Row> rows;   // one per doc.entries
			bool registered = false;
			bool backupTaken = false;
			int writes = 0;
			std::string lastError;
			std::string lastApply;
		};

		struct Skipped
		{
			std::string file;
			std::string reason;
		};

		std::mutex g_lock;              // guards everything below (the render thread draws and writes, DevBench lists)
		std::vector<File> g_files;      // filled once by Load, in load order; never resized after, so a tab's index stays good
		std::vector<Skipped> g_skipped;
		bool g_loaded = false;
		std::deque<std::string> g_engineStrings;   // a string set into the game lives here for the session (a deque never moves them)

		std::string J(const std::string& a_s) { return nlohmann::json(a_s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		bool GsoLoaded() { return ::GetModuleHandleW(kDll) != nullptr; }

		// ---- the game's setting ----------------------------------------------------------------------------------

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
				// rule 14: checked before use; at kDataLoaded and after it is always there, so this is a real anomaly
				static bool s_warned = false;
				if (!s_warned) { s_warned = true; logger::warn("gso: the game's setting collection is not there yet - live values are not shown"); }
				return out;
			}
			out.setting = coll->GetSetting(a_key.c_str());
			if (out.setting)
			{
				// the type by the engine's own name's first letter (GsoCore.h) - the lookup ignores case, so the file's spelling
				// may differ from the game's
				const char* name = out.setting->GetName();
				out.type = gsocore::GameTypeFromName(name ? std::string(name) : a_key);
			}
			return out;
		}

		// A colour setting's four bytes, in memory order red, green, blue, alpha (CommonLib's Color on the SE/AE line; a
		// uint32 over the same bytes on the 1.7 line), read and written as bytes so both lines agree.
		std::uint32_t ReadColor(const RE::Setting& a_s)
		{
			std::uint8_t p[4];
			std::memcpy(p, &a_s.data, 4);
			return gsocore::ColorFromParts(p);
		}

		std::string LiveText(const Live& a_l, int a_decimals)
		{
			if (!a_l.setting) { return {}; }
			const RE::Setting& s = *a_l.setting;
			switch (a_l.type)
			{
			case gsocore::GameType::kBool: return gsocore::FormatBool(s.data.b);
			case gsocore::GameType::kFloat: return gsocore::FormatFloat(s.data.f, a_decimals);
			case gsocore::GameType::kInt: return std::to_string(s.data.i);
			case gsocore::GameType::kUInt: return std::to_string(s.data.u);
			case gsocore::GameType::kColor: return gsocore::FormatColor(ReadColor(s));
			case gsocore::GameType::kString: return gsocore::FormatString(s.data.s ? s.data.s : "", false);
			default: return "?";
			}
		}

		// Sets the game's setting to the file's (already written and read back) value. False when the value is not of the
		// setting's type - which TextFor never writes, so it is a guard only.
		bool SetEngine(const Live& a_l, const gsocore::Value& a_v)
		{
			if (!a_l.setting) { return false; }
			RE::Setting& s = *a_l.setting;
			using gsocore::GameType;
			using gsocore::TomlType;
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
				gsocore::ColorParts(a_v.i, p);
				std::memcpy(&s.data, p, 4);
				return true;
			}
			case GameType::kString:
				if (a_v.type != TomlType::kString) { return false; }
				// the game's own string is left where it is (it may be the plugin's memory); ours lives for the session
				g_engineStrings.push_back(a_v.s);
				s.data.s = g_engineStrings.back().data();
				return true;
			default: return false;
			}
		}

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

		std::string Why(const gsocore::ParseError& a_e)
		{
			char buf[512];
			switch (a_e.problem)
			{
			case gsocore::Problem::kUtf16: return strings::TR("AMF_IniWhyUtf16", "UTF-16 text, which this reader does not take");
			case gsocore::Problem::kTable:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoWhyTable", "line %d is a [table]; Game Settings Override files are flat"), a_e.line);
				return buf;
			case gsocore::Problem::kNotSetting:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoWhyLine", "line %d is not a setting (name = value) or a comment"), a_e.line);
				return buf;
			case gsocore::Problem::kValue:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoWhyValue", "line %d has a value this page does not read (an array, a table, a date "
							  "or a text over several lines)"), a_e.line);
				return buf;
			case gsocore::Problem::kKeyTwice:
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoWhyKeyTwice", "%s is set twice in the file"), a_e.key.c_str());
				return buf;
			default: return {};
			}
		}

		void Skip(const std::string& a_file, const std::string& a_reason)
		{
			g_skipped.push_back({ a_file, a_reason });
			logger::info("gso: {} - not shown: {}", a_file, a_reason);
		}

		std::vector<const gsocore::Document*> Docs()
		{
			std::vector<const gsocore::Document*> out;
			for (const File& f : g_files) { out.push_back(&f.doc); }
			return out;
		}

		bool FileOn(const File& a_f, const std::set<std::string>& a_on) { return settings::Get().gsoMenus && a_on.contains(a_f.id); }

		// Called with g_lock held.
		void ApplyVisibilityLocked()
		{
			const std::set<std::string> on = gsocore::ParseIdList(settings::Get().gsoFilesOn);
			for (const File& f : g_files)
			{
				if (f.registered) { registry::SetPageVisible(kEntry, f.tab.c_str(), FileOn(f, on)); }
			}
		}

		// ---- writing and applying ------------------------------------------------------------------------------------

		struct Outcome
		{
			bool ok = false;
			bool changed = false;     // false: the value was already that (nothing written)
			std::string text;         // the value as written
			std::string error;        // English, for the log and DevBench; the page shows it after "Not saved:"
			bool engineSet = false;
			std::string engineWhy;    // "set" | "later file: <name>" | "not a game setting" | "type"
			bool reloaded = false;
			std::string reloadWhy;    // "called" | "not loaded" | "no export" | the failure
			std::string gsoMessage;   // what ReloadConfig returned
			std::string shown;        // the line the page shows, in the player's language
		};

		// Write one value and apply it. Render thread, g_lock held.
		Outcome Commit(std::size_t a_file, const std::string& a_key, const std::string& a_input)
		{
			Outcome o;
			File& f = g_files[a_file];
			const gsocore::Entry* cur = gsocore::Find(f.doc, a_key);
			if (!cur) { o.error = a_key + " is not in " + f.name; return o; }
			const Live live = LiveOf(a_key);
			const gsocore::Control control = gsocore::ControlFor(live.type, cur->value.type);
			if (!gsocore::TextFor(control, cur->value, a_input, o.text, o.error)) { o.error = a_key + " " + o.error; return o; }

			// the file as it is on disk NOW (it may have been edited since the game started) must still read cleanly
			std::string bytes;
			if (!ReadBytes(f.path, bytes)) { o.error = "could not read " + f.path; return o; }
			gsocore::Document disk;
			gsocore::ParseError pe;
			if (!gsocore::Parse(bytes, disk, pe)) { o.error = f.name + " no longer reads cleanly (" + Why(pe) + ")"; return o; }
			const gsocore::Entry* onDisk = gsocore::Find(disk, a_key);
			if (!onDisk) { o.error = a_key + " is no longer in " + f.name; return o; }
			if (disk.lines[onDisk->line].substr(onDisk->valueStart, onDisk->valueEnd - onDisk->valueStart) == o.text)
			{
				logger::debug("gso: {} {} is already {} - nothing written", f.name, a_key, o.text);
				f.doc = std::move(disk);
				f.rows.assign(f.doc.entries.size(), Row{});
				o.ok = true;
				return o;
			}
			std::string next;
			gsocore::Document after;
			if (!gsocore::WithValue(disk, a_key, o.text, next) || !gsocore::Parse(next, after, pe) || !gsocore::Find(after, a_key))
			{
				o.error = "the new line for " + a_key + " would not read back - nothing written";
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
					logger::info("gso: {} kept as {} before the first change", f.path, backup);
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
			const std::string oldText = disk.lines[onDisk->line].substr(onDisk->valueStart, onDisk->valueEnd - onDisk->valueStart);
			f.doc = std::move(after);
			f.rows.assign(f.doc.entries.size(), Row{});
			++f.writes;
			o.ok = true;
			o.changed = true;
			logger::info("gso: {} = {} written to {} (was {}; that value only)", a_key, o.text, f.path, oldText);

			// 1) the game's value at once - only when this file is the one that wins the key
			const gsocore::Entry* written = gsocore::Find(f.doc, a_key);
			const int winner = gsocore::Winner(Docs(), a_key);
			if (!live.setting) { o.engineWhy = "not a game setting"; }
			else if (winner != static_cast<int>(a_file)) { o.engineWhy = "later file: " + (winner >= 0 ? g_files[static_cast<std::size_t>(winner)].name : std::string("?")); }
			else if (written && SetEngine(live, written->value)) { o.engineSet = true; o.engineWhy = "set"; }
			else { o.engineWhy = "type"; }
			logger::info("gso: {} - the game's value: {} (now {})", a_key, o.engineWhy, LiveText(live, written ? written->value.decimals : -1));

			// 2) GSO reloads its files, through its own export (found, never loaded)
			if (HMODULE module = ::GetModuleHandleW(kDll); !module) { o.reloadWhy = "not loaded"; }
			else if (FARPROC proc = ::GetProcAddress(module, kExport); !proc) { o.reloadWhy = "no export"; }
			else
			{
				const mfm::CallResult c = mfm::Call(reinterpret_cast<void*>(proc), mfm::Type::kMessageBox);
				o.reloaded = c.ok;
				o.reloadWhy = c.ok ? "called" : c.error;
				o.gsoMessage = c.message;
				while (!o.gsoMessage.empty() && (o.gsoMessage.back() == '\n' || o.gsoMessage.back() == '\r' || o.gsoMessage.back() == ' ')) { o.gsoMessage.pop_back(); }
			}
			logger::info("gso: ReloadConfig - {}{}", o.reloadWhy, o.gsoMessage.empty() ? std::string() : ": " + o.gsoMessage);

			// the line the page shows: what was saved, what happened in the game, what GSO said
			char buf[1024];
			std::string shown = strings::TR("AMF_GsoSaved", "Saved.");
			if (o.engineSet) { shown += std::string(" ") + strings::TR("AMF_GsoSetLive", "The game's value was set at once."); }
			else if (!live.setting) { shown += std::string(" ") + strings::TR("AMF_GsoSetUnknown", "The game has no setting by this name, so nothing was set in the game."); }
			else if (winner != static_cast<int>(a_file) && winner >= 0)
			{
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoSetLater", "%s sets it later and wins, so the game keeps that file's value."),
							  g_files[static_cast<std::size_t>(winner)].name.c_str());
				shown += std::string(" ") + buf;
			}
			if (o.reloaded)
			{
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoReloaded", "Game Settings Override reloaded its files: %s"),
							  o.gsoMessage.empty() ? strings::TR("AMF_GsoNoMessage", "(no message)") : o.gsoMessage.c_str());
				shown += std::string(" ") + buf;
			}
			else if (o.reloadWhy == "not loaded") { shown += std::string(" ") + strings::TR("AMF_GsoNoReload", "Game Settings Override is not loaded, so nothing reloaded."); }
			else
			{
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_GsoReloadFailed", "Game Settings Override's reload could not run: %s"), o.reloadWhy.c_str());
				shown += std::string(" ") + buf;
			}
			o.shown = shown;
			return o;
		}

		// ---- drawing -------------------------------------------------------------------------------------------------

		int ShownDecimals(const gsocore::Value& a_v) { return a_v.type == gsocore::TomlType::kFloat && a_v.decimals >= 1 ? std::min(a_v.decimals, 9) : 6; }

		void SyncRow(Row& a_r, const gsocore::Value& a_v)
		{
			a_r.f = a_v.type == gsocore::TomlType::kFloat ? a_v.f : static_cast<double>(a_v.i);
			a_r.i = a_v.i;
			std::uint8_t p[4];
			gsocore::ColorParts(a_v.i, p);
			for (int k = 0; k < 4; ++k) { a_r.color[k] = p[k] / 255.0f; }
			std::snprintf(a_r.text, sizeof(a_r.text), "%s", a_v.s.c_str());
		}

		void DrawPage(std::size_t a_file)
		{
			std::scoped_lock l(g_lock);
			if (a_file >= g_files.size()) { return; }
			File& f = g_files[a_file];
			helpbar::Want("gso|" + f.id);
			const bool gso = GsoLoaded();

			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::TextWrapped(strings::TR("AMF_GsoPageHelp", "Game Settings Override reads its files when the game starts, in A-Z order: when two files "
							   "set the same setting, the later one wins. A change is written into %s itself, only that value; the file as it first "
							   "was is kept as %s.amf-backup."), f.name.c_str(), f.name.c_str());
			ImGui::TextWrapped("%s", gso ? strings::TR("AMF_GsoApplyLive", "After a change the game's value is set at once, and Game Settings Override "
															"reloads its files.")
										 : strings::TR("AMF_GsoNotLoaded", "Game Settings Override is not loaded: a change is written to the file and "
															"set in the game at once, but the file itself is read only when Game Settings Override is installed."));
			if (!f.lastError.empty()) { ImGui::TextWrapped(strings::TR("AMF_IniError", "Not saved: %s"), f.lastError.c_str()); }
			if (!f.lastApply.empty()) { ImGui::TextWrapped("%s", f.lastApply.c_str()); }
			ImGui::PopStyleColor();
			ImGui::Spacing();

			const auto docs = Docs();
			std::string commitKey, commitInput;
			for (std::size_t ri = 0; ri < f.doc.entries.size() && ri < f.rows.size(); ++ri)
			{
				const gsocore::Entry& e = f.doc.entries[ri];
				Row& r = f.rows[ri];
				const Live live = LiveOf(e.key);
				const gsocore::Control control = gsocore::ControlFor(live.type, e.value.type);
				if (!r.pending) { SyncRow(r, e.value); }
				ImGui::PushID(static_cast<int>(ri));
				const char* label = e.key.c_str();   // the EditorID as the game and GSO's files name it
				std::string input;
				bool now = false;   // a switch is written at once; the others when their control is let go
				ImGui::BeginGroup();
				switch (control)
				{
				case gsocore::Control::kSwitch:
				{
					bool b = e.value.type == gsocore::TomlType::kBool ? e.value.b : e.value.i != 0;
					if (widgets::Toggle(label, &b)) { input = b ? "true" : "false"; now = true; }
					break;
				}
				case gsocore::Control::kFloat:
				{
					// a number box stepping ONE unit of the last digit the file shows (rule 68): "1.45" steps 0.01
					const int decimals = ShownDecimals(e.value);
					const double step = std::pow(10.0, -decimals);
					char fmt[16];
					std::snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
					if (ImGui::InputDouble(label, &r.f, step, step * 10.0, fmt)) { r.pending = true; }
					break;
				}
				case gsocore::Control::kInt:
				{
					int v = static_cast<int>(std::clamp<long long>(r.i, INT32_MIN, INT32_MAX));
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
					if (ImGui::InputInt(label, &v, 1, 10)) { r.i = v; r.pending = true; }
					break;
				}
				case gsocore::Control::kUInt:
				{
					ImU32 v = static_cast<ImU32>(std::clamp<long long>(r.i, 0, 0xFFFFFFFFll));
					const ImU32 one = 1, ten = 10;
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
					if (ImGui::InputScalar(label, ImGuiDataType_U32, &v, &one, &ten)) { r.i = v; r.pending = true; }
					break;
				}
				case gsocore::Control::kColor:
				{
					// no picker pop-up: its window is outside this row, so "let go" could not be seen while it is open
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
					if (ImGui::ColorEdit4(label, r.color, ImGuiColorEditFlags_NoPicker | ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_DisplayRGB |
														  ImGuiColorEditFlags_NoOptions | ImGuiColorEditFlags_AlphaPreviewHalf))
					{
						r.pending = true;
					}
					break;
				}
				case gsocore::Control::kText:
				{
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
					if (e.value.type != gsocore::TomlType::kString)
					{
						// a key the game does not know, holding a value this page has no control for: shown, not changed
						const std::string raw = f.doc.lines[e.line].substr(e.valueStart, e.valueEnd - e.valueStart);
						std::snprintf(r.text, sizeof(r.text), "%s", raw.c_str());
						ImGui::InputText(label, r.text, sizeof(r.text), ImGuiInputTextFlags_ReadOnly);
					}
					else if (ImGui::InputText(label, r.text, sizeof(r.text), e.value.s.size() >= sizeof(r.text) - 1 ? ImGuiInputTextFlags_ReadOnly : 0))
					{
						r.pending = true;
					}
					break;
				}
				}
				ImGui::EndGroup();
				const bool active = ImGui::IsItemActive();   // the group: any of the control's parts still in use
				if (!e.help.empty() && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) { helpbar::OfferForLastItem(e.help); }
				if (r.pending && !active)
				{
					r.pending = false;
					switch (control)
					{
					case gsocore::Control::kFloat:
					{
						const int decimals = ShownDecimals(e.value);
						const double step = std::pow(10.0, -decimals);
						input = gsocore::FormatFloat(std::round(r.f / step) * step, decimals);
						break;
					}
					case gsocore::Control::kInt:
					case gsocore::Control::kUInt: input = std::to_string(r.i); break;
					case gsocore::Control::kColor:
					{
						std::uint8_t p[4];
						for (int k = 0; k < 4; ++k) { p[k] = static_cast<std::uint8_t>(std::lround(std::clamp(r.color[k], 0.0f, 1.0f) * 255.0f)); }
						input = gsocore::FormatColor(gsocore::ColorFromParts(p));
						break;
					}
					case gsocore::Control::kText: input = r.text; break;
					default: break;
					}
					now = true;
				}
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
				const int winner = gsocore::Winner(docs, e.key);
				if (winner >= 0 && winner != static_cast<int>(a_file))
				{
					ImGui::Indent();
					ImGui::TextWrapped(strings::TR("AMF_GsoLaterFile", "%s sets it later and wins."), g_files[static_cast<std::size_t>(winner)].name.c_str());
					ImGui::Unindent();
				}
				ImGui::PopStyleColor();
				ImGui::PopID();
				if (now && !input.empty() && commitKey.empty()) { commitKey = e.key; commitInput = input; }
			}
			if (!commitKey.empty())
			{
				// after the rows: a write re-reads the file and rebuilds them
				const Outcome o = Commit(a_file, commitKey, commitInput);
				f.lastError = o.ok ? std::string() : o.error;
				if (o.ok && o.changed) { f.lastApply = o.shown; }
				if (!o.ok) { logger::warn("gso: {} {} not set - {}", f.name, commitKey, o.error); }
			}
		}
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (g_loaded) { return; }   // once per session, as GSO reads its folder once at start
		g_loaded = true;
		const fs::path dir(kFolder);
		std::error_code ec;
		if (!fs::is_directory(dir, ec))
		{
			logger::info("gso: {} is not there - no Game Settings Override files (GSO {}loaded)", kFolder, GsoLoaded() ? "" : "not ");
			return;
		}
		std::vector<std::string> names;
		for (const auto& de : fs::directory_iterator(dir, ec))
		{
			std::error_code fe;
			if (!de.is_regular_file(fe)) { continue; }   // GSO reads the top level only
			const std::wstring w = de.path().filename().wstring();
			if (std::any_of(w.begin(), w.end(), [](wchar_t c) { return c > 0x7E || c < 0x20; }))
			{
				// its narrow form would not name the same file again
				if (Lower(de.path().extension().string()) == ".toml") { Skip("?.toml", strings::TR("AMF_IniWhyName", "its file name is not plain ASCII")); }
				continue;
			}
			const std::string name = de.path().filename().string();
			if (gsocore::GsoReadsName(name)) { names.push_back(name); }
			else if (Lower(name).size() > 5 && Lower(name).ends_with(".toml"))
			{
				Skip(name, strings::TR("AMF_GsoWhyCase", "Game Settings Override reads only names ending in .toml in small letters"));
			}
		}
		gsocore::SortLoadOrder(names);
		for (const std::string& name : names)
		{
			const fs::path p = dir / name;
			const std::uintmax_t bytes = fs::file_size(p, ec);
			if (ec || bytes > kMaxBytes) { Skip(name, strings::TR("AMF_IniWhyBig", "over 256 KB - too big for a settings file")); continue; }
			File f;
			f.name = name;
			f.id = Lower(name);
			f.tab = name.substr(0, name.size() - 5);
			f.path = p.generic_string();
			std::string data;
			if (!ReadBytes(f.path, data)) { Skip(name, strings::TR("AMF_IniWhyUnread", "could not be opened")); continue; }
			gsocore::ParseError pe;
			if (!gsocore::Parse(data, f.doc, pe)) { Skip(name, Why(pe)); continue; }
			if (f.doc.entries.empty()) { Skip(name, strings::TR("AMF_GsoWhyEmpty", "no settings in it")); continue; }
			f.rows.assign(f.doc.entries.size(), Row{});
			std::size_t unknown = 0;
			for (const gsocore::Entry& e : f.doc.entries)
			{
				const Live live = LiveOf(e.key);
				if (!live.setting) { ++unknown; logger::debug("gso: {} {} - the game has no setting by this name", name, e.key); }
				else if (!gsocore::Matches(live.type, e.value.type))
				{
					logger::debug("gso: {} {} - written as {}, the game's setting is {}", name, e.key, gsocore::TomlTypeName(e.value.type), gsocore::GameTypeName(live.type));
				}
			}
			logger::info("gso: {} - {} setting(s){}{}{}", name, f.doc.entries.size(), unknown ? ", " + std::to_string(unknown) + " the game does not know" : std::string(),
						 f.doc.bom ? ", BOM" : "", !f.doc.endings.empty() && f.doc.endings.front() == "\r\n" ? ", CRLF" : "");
			g_files.push_back(std::move(f));
		}
		for (std::size_t i = 0; i < g_files.size(); ++i)
		{
			File& f = g_files[i];
			f.registered = registry::RegisterFn(kEntry, f.tab.c_str(), [i]() { DrawPage(i); }, true);
			if (!f.registered) { logger::warn("gso: tab \"{}\" of \"{}\" NOT registered (the name is taken)", f.tab, kEntry); }
		}
		ApplyVisibilityLocked();
		const std::set<std::string> on = gsocore::ParseIdList(settings::Get().gsoFilesOn);
		std::size_t shown = 0;
		for (const File& f : g_files) { shown += f.registered && FileOn(f, on) ? 1 : 0; }
		logger::info("gso: {} file(s) with a tab, {} not shown; {} switched on{}; Game Settings Override {}", g_files.size(), g_skipped.size(), shown,
					 settings::Get().gsoMenus ? "" : " (the page is off)", GsoLoaded() ? "loaded - changes apply live" : "NOT loaded");
	}

	void ApplyVisibility()
	{
		std::scoped_lock l(g_lock);
		ApplyVisibilityLocked();
		const std::set<std::string> on = gsocore::ParseIdList(settings::Get().gsoFilesOn);
		std::size_t shown = 0;
		for (const File& f : g_files) { shown += f.registered && FileOn(f, on) ? 1 : 0; }
		logger::info("gso: {} of {} file tab(s) shown{}", shown, g_files.size(), settings::Get().gsoMenus ? "" : " (the page is off)");
	}

	void DrawSettingsList()
	{
		std::scoped_lock l(g_lock);
		const std::set<std::string> on = gsocore::ParseIdList(settings::Get().gsoFilesOn);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::TextWrapped("%s", GsoLoaded() ? strings::TR("AMF_GsoLoaded", "Game Settings Override is loaded: a change applies in the game at once.")
											 : strings::TR("AMF_GsoNotLoadedShort", "Game Settings Override is not loaded (ccld_GameSettingsOverride.dll): "
														   "its files do nothing until it is installed."));
		if (g_files.empty())
		{
			ImGui::TextWrapped("%s", strings::TR("AMF_GsoNone", "No Game Settings Override files were found (Data/SKSE/Plugins/ccld_GameSettingsOverride/*.toml)."));
		}
		ImGui::PopStyleColor();
		if (!g_files.empty())
		{
			ImGui::Spacing();
			ImGui::SeparatorText(strings::TR("AMF_GsoFiles", "Files, in the order Game Settings Override loads them"));
			for (File& f : g_files)
			{
				bool fileOn = on.contains(f.id);
				const std::string label = f.name + "##gso-" + f.id;
				if (widgets::Toggle(label.c_str(), &fileOn))
				{
					settings::Get().gsoFilesOn = gsocore::WithId(settings::Get().gsoFilesOn, f.id, fileOn);
					logger::info("settings page: GSO file {} -> {}", f.id, fileOn ? "on" : "off");
					settings::Save();
					ApplyVisibilityLocked();
				}
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::Indent();
				char count[96];
				std::snprintf(count, sizeof(count), strings::TRN("AMF_IniSettings", static_cast<long long>(f.doc.entries.size()), "%d setting", "%d settings"),
							  static_cast<int>(f.doc.entries.size()));
				ImGui::TextUnformatted(count);
				ImGui::Unindent();
				ImGui::PopStyleColor();
			}
		}
		if (!g_skipped.empty())
		{
			ImGui::Spacing();
			ImGui::SeparatorText(strings::TR("AMF_IniSkipped", "Not shown"));
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			for (const Skipped& s : g_skipped) { ImGui::BulletText("%s: %s", s.file.c_str(), s.reason.c_str()); }
			ImGui::PopStyleColor();
		}
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		auto findFile = [](const std::string& a_name) -> int {
			std::string want = Lower(a_name);
			if (!want.ends_with(".toml")) { want += ".toml"; }
			for (std::size_t i = 0; i < g_files.size(); ++i) { if (g_files[i].id == want) { return static_cast<int>(i); } }
			return -1;
		};
		if (what == "list")
		{
			std::scoped_lock l(g_lock);
			const std::set<std::string> on = gsocore::ParseIdList(settings::Get().gsoFilesOn);
			std::string files;
			for (const File& f : g_files)
			{
				files += (files.empty() ? "" : ",") + std::string("{\"file\":") + J(f.name) + ",\"path\":" + J(f.path) + ",\"settings\":" +
						 std::to_string(f.doc.entries.size()) + ",\"bom\":" + (f.doc.bom ? "true" : "false") + ",\"switch\":" + (on.contains(f.id) ? "true" : "false") +
						 ",\"shown\":" + (f.registered && FileOn(f, on) ? "true" : "false") + ",\"registered\":" + (f.registered ? "true" : "false") +
						 ",\"writes\":" + std::to_string(f.writes) + ",\"lastError\":" + J(f.lastError) + ",\"lastApply\":" + J(f.lastApply) + "}";
			}
			std::string skipped;
			for (const Skipped& s : g_skipped) { skipped += (skipped.empty() ? "" : ",") + std::string("{\"file\":") + J(s.file) + ",\"reason\":" + J(s.reason) + "}"; }
			return std::string("{\"ok\":true,\"op\":\"gso\",\"do\":\"list\",\"enabled\":") + (settings::Get().gsoMenus ? "true" : "false") +
				   ",\"gsoLoaded\":" + (GsoLoaded() ? "true" : "false") + ",\"folder\":" + J(kFolder) + ",\"entry\":" + J(kEntry) +
				   ",\"count\":" + std::to_string(g_files.size()) + ",\"files\":[" + files + "],\"skipped\":[" + skipped + "]}";
		}
		if (what == "get")
		{
			std::scoped_lock l(g_lock);
			const int fi = findFile(str("file"));
			if (fi < 0) { return "{\"ok\":false,\"op\":\"gso\",\"error\":\"no such file (do=list names them)\"}"; }
			const File& f = g_files[static_cast<std::size_t>(fi)];
			const auto docs = Docs();
			std::string out;
			for (const gsocore::Entry& e : f.doc.entries)
			{
				const Live live = LiveOf(e.key);
				const int winner = gsocore::Winner(docs, e.key);
				out += (out.empty() ? "" : ",") + std::string("{\"name\":") + J(e.key) + ",\"file\":" +
					   J(f.doc.lines[e.line].substr(e.valueStart, e.valueEnd - e.valueStart)) + ",\"toml\":\"" + gsocore::TomlTypeName(e.value.type) +
					   "\",\"game\":\"" + gsocore::GameTypeName(live.type) + "\",\"known\":" + (live.setting ? "true" : "false") +
					   ",\"live\":" + J(LiveText(live, ShownDecimals(e.value))) + ",\"control\":" + J(gsocore::ControlName(gsocore::ControlFor(live.type, e.value.type))) +
					   ",\"matches\":" + (gsocore::Matches(live.type, e.value.type) ? "true" : "false") +
					   ",\"winner\":" + J(winner >= 0 ? g_files[static_cast<std::size_t>(winner)].name : std::string()) + ",\"help\":" + J(e.help) + "}";
			}
			return "{\"ok\":true,\"op\":\"gso\",\"do\":\"get\",\"file\":" + J(f.name) + ",\"settings\":[" + out + "]}";
		}
		if (what == "set")
		{
			const std::string file = str("file"), name = str("name");
			std::string value = str("value");
			if (args.is_object() && args.contains("value") && args["value"].is_number()) { value = args["value"].dump(); }
			if (args.is_object() && args.contains("value") && args["value"].is_boolean()) { value = args["value"].get<bool>() ? "true" : "false"; }
			{
				std::scoped_lock l(g_lock);
				const int fi = findFile(file);
				if (fi < 0) { return "{\"ok\":false,\"op\":\"gso\",\"error\":\"no such file (do=list names them)\"}"; }
				if (!gsocore::Find(g_files[static_cast<std::size_t>(fi)].doc, name)) { return "{\"ok\":false,\"op\":\"gso\",\"error\":\"no such name in the file (do=get lists them)\"}"; }
			}
			// written and applied on the render thread, as the page does (GSO's ReloadConfig is never called from this thread);
			// the result comes back through a shared block, so a job that runs after the wait gave up writes into nothing gone
			auto result = std::make_shared<Outcome>();
			auto done = std::make_shared<std::atomic<bool>>(false);
			const bool ran = renderer::RunOnRenderThreadWait([file, name, value, result, done, findFile]() {
				std::scoped_lock l(g_lock);
				const int fi = findFile(file);
				if (fi >= 0)
				{
					*result = Commit(static_cast<std::size_t>(fi), name, value);
					File& f = g_files[static_cast<std::size_t>(fi)];
					f.lastError = result->ok ? std::string() : result->error;
					if (result->ok && result->changed) { f.lastApply = result->shown; }
				}
				else { result->error = "no such file"; }
				done->store(true);
			}, 5000);
			if (!ran || !done->load())
			{
				return "{\"ok\":false,\"op\":\"gso\",\"do\":\"set\",\"error\":\"no frame took the call within 5 s (is the game drawing?) - it runs at the next frame\"}";
			}
			const Outcome o = *result;
			return std::string("{\"ok\":") + (o.ok ? "true" : "false") + ",\"op\":\"gso\",\"do\":\"set\",\"file\":" + J(file) + ",\"name\":" + J(name) +
				   ",\"written\":" + J(o.text) + ",\"changed\":" + (o.changed ? "true" : "false") + ",\"engineSet\":" + (o.engineSet ? "true" : "false") +
				   ",\"engine\":" + J(o.engineWhy) + ",\"reloaded\":" + (o.reloaded ? "true" : "false") + ",\"reload\":" + J(o.reloadWhy) +
				   ",\"gsoMessage\":" + J(o.gsoMessage) + ",\"shown\":" + J(o.shown) + (o.error.empty() ? std::string() : ",\"error\":" + J(o.error)) + "}";
		}
		return "{\"ok\":false,\"op\":\"gso\",\"error\":\"do = list | get {file} | set {file, name, value}\"}";
	}
}
