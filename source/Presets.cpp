#include "Presets.h"

#include "AmfIcons.h"
#include "IniPages.h"
#include "Keyboard.h"
#include "McmLoader.h"
#include "McmScripts.h"
#include "PresetsCore.h"
#include "PrismaRedux.h"
#include "RememberedSettings.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "TomlPages.h"
#include "utils/Logger.h"
#include "utils/NullGuard.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// See Presets.h and PresetsCore.h. Plan: 4. plans\amf-settings-presets\PLAN.md.

namespace presets
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;
		namespace rs = rememberedsettings;
		namespace pc = presetscore;
		using strings::TR;

		constexpr const char* kShippedDir = "Data/SKSE/Plugins/ApocryphaMenuFramework/Presets";

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::tm LocalNow()
		{
			const std::time_t t = std::time(nullptr);
			std::tm tm{};
			localtime_s(&tm, &t);
			return tm;
		}

		std::string AmfVersion()
		{
			// 2.3.0: a null declaration (looked up by export name) gives "?" instead of a crash
			static const std::string v = [] {
				const auto* decl = SKSE::PluginDeclaration::GetSingleton();
				return decl ? decl->GetVersion().string(".") : std::string("?");
			}();
			return v;
		}

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Fmt(const char* a_format, ...)
		{
			char buf[1024];
			va_list args;
			va_start(args, a_format);
			std::vsnprintf(buf, sizeof(buf), a_format, args);
			va_end(args);
			return buf;
		}

		// ------------------------------------------------------------------------------------------- state

		std::mutex g_lock;
		std::atomic<bool> g_busy{ false };
		std::atomic<long long> g_busySinceMs{ 0 };
		std::string g_phase;   // what runs now
		std::string g_last;    // the last result, one line or a few

		struct Comparison
		{
			bool ready = false;
			std::string name;
			bool shipped = false;
			pc::Preset preset;
			pc::Preset current;
			std::set<std::string> installed;
			pc::Diff diff;
			std::set<std::string> ticked;   // the groups a load applies
		};
		Comparison g_cmp;

		bool Busy()
		{
			if (g_busy.load() && NowMs() - g_busySinceMs.load() > 5 * 60 * 1000)
			{
				logger::warn("presets: a save / compare / load has run for 5 minutes - no longer waiting for it");
				g_busy = false;
			}
			return g_busy.load();
		}

		bool Begin(const std::string& a_phase)
		{
			bool expected = false;
			if (Busy() || !g_busy.compare_exchange_strong(expected, true)) { return false; }
			g_busySinceMs = NowMs();
			std::scoped_lock l(g_lock);
			g_phase = a_phase;
			return true;
		}

		void End(const std::string& a_result)
		{
			{
				std::scoped_lock l(g_lock);
				g_phase.clear();
				if (!a_result.empty()) { g_last = a_result; }
			}
			g_busy = false;
		}

		// ------------------------------------------------------------------------------------------- records <-> text

		pc::MenuRecord ToCore(const rs::Record& a_r)
		{
			return { rs::RecordTypeName(a_r.type), a_r.page, a_r.text, a_r.nth, a_r.id, a_r.value, a_r.menuIndex };
		}
		rs::Record FromCore(const pc::MenuRecord& a_r)
		{
			rs::Record r;
			r.type = rs::RecordType(a_r.type);
			r.page = a_r.page;
			r.text = a_r.text;
			r.nth = a_r.nth;
			r.id = a_r.id;
			r.value = a_r.value;
			r.menuIndex = a_r.menuIndex;
			return r;
		}
		std::map<std::string, pc::MenuPart> ToCore(const std::map<std::string, rs::MenuSnapshot>& a_menus)
		{
			std::map<std::string, pc::MenuPart> out;
			for (const auto& [key, snap] : a_menus)
			{
				pc::MenuPart part;
				part.entry = snap.entry;
				for (const auto& r : snap.records) { part.settings.push_back(ToCore(r)); }
				out[key] = std::move(part);
			}
			return out;
		}
		std::map<std::string, rs::MenuSnapshot> FromCore(const std::map<std::string, pc::MenuPart>& a_menus)
		{
			std::map<std::string, rs::MenuSnapshot> out;
			for (const auto& [key, part] : a_menus)
			{
				rs::MenuSnapshot snap;
				snap.entry = part.entry;
				for (const auto& r : part.settings) { snap.records.push_back(FromCore(r)); }
				out[key] = std::move(snap);
			}
			return out;
		}

		// ------------------------------------------------------------------------------------------- what is here now

		// One file-backed source found in this game: an INI page's file, a TOML file, a Prisma config, an MCM Helper mod with
		// ModSetting values. shown: on in AMF (a page switched on, a menu imported) - what Save takes by default.
		struct Source
		{
			std::string key;
			std::string kind;
			std::string id;
			std::string entry;
			bool shown = false;
		};

		std::map<std::string, bool> ImportedMenus()
		{
			std::map<std::string, bool> out;
			for (const auto& row : mcmloader::ImportList()) { out[row.key] = row.imported; }
			return out;
		}

		std::vector<Source> FileSources()
		{
			std::vector<Source> out;
			const json ini = json::parse(inipages::ToolJson(R"({"do":"list"})"), nullptr, false);
			if (ini.is_object() && ini.contains("mods") && ini["mods"].is_array())
			{
				for (const auto& m : ini["mods"])
				{
					const std::string id = m.value("mod", std::string());
					if (id.empty()) { continue; }
					out.push_back({ pc::GroupKey("ini", id), "ini", id, m.value("entry", id), m.value("shown", false) });
				}
			}
			const json toml = json::parse(tomlpages::ToolJson(R"({"do":"list"})", false), nullptr, false);
			if (toml.is_object() && toml.contains("pages") && toml["pages"].is_array())
			{
				for (const auto& p : toml["pages"])
				{
					const auto& files = p.contains("files") && p["files"].is_array() ? p["files"] : json::array();
					for (const auto& f : files)
					{
						const std::string id = f.value("file", std::string());
						if (id.empty()) { continue; }
						std::string entry = p.value("entry", id);
						if (files.size() > 1) { entry += " - " + f.value("name", id); }
						out.push_back({ pc::GroupKey("toml", id), "toml", id, entry, p.value("shown", false) });
					}
				}
			}
			for (const auto& c : prisma::Configs())
			{
				out.push_back({ pc::GroupKey("prisma", c.modId), "prisma", c.modId, c.entry, c.imported && !c.duplicate });
			}
			const auto imported = ImportedMenus();
			for (const auto& m : mcmloader::FileMenus())
			{
				const auto it = imported.find(m.key);
				out.push_back({ pc::GroupKey("mcmhelper", m.modName), "mcmhelper", m.modName, m.entry, it == imported.end() || it->second });
			}
			return out;
		}

		// Every group found in this game: file sources and the menus Remembered settings can read.
		std::set<std::string> Installed(const std::vector<Source>& a_sources)
		{
			std::set<std::string> out;
			for (const auto& s : a_sources) { out.insert(s.key); }
			for (const auto& [key, entry] : rs::MenusPresent()) { out.insert(key); }
			return out;
		}

		// The values a source holds now, through its page's own get.
		pc::FilePart ReadSource(const Source& a_s)
		{
			pc::FilePart part{ a_s.kind, a_s.id, a_s.entry, {} };
			if (a_s.kind == "ini")
			{
				const json r = json::parse(inipages::ToolJson(json{ { "do", "get" }, { "mod", a_s.id } }.dump()), nullptr, false);
				if (r.is_object() && r.contains("settings") && r["settings"].is_array())
				{
					for (const auto& st : r["settings"]) { part.values.push_back({ st.value("section", std::string()), st.value("name", std::string()), st.value("value", std::string()) }); }
				}
			}
			else if (a_s.kind == "toml")
			{
				const json r = json::parse(tomlpages::ToolJson(json{ { "do", "get" }, { "file", a_s.id } }.dump(), false), nullptr, false);
				if (r.is_object() && r.contains("settings") && r["settings"].is_array())
				{
					for (const auto& st : r["settings"])
					{
						if (!st.value("editable", false)) { continue; }   // arrays, inline tables, multi-line text: shown, never written
						part.values.push_back({ {}, st.value("name", std::string()), st.value("input", st.value("value", std::string())) });
					}
				}
			}
			else if (a_s.kind == "prisma")
			{
				const json r = json::parse(prisma::ToolJson(json{ { "do", "get" }, { "mod", a_s.id } }.dump()), nullptr, false);
				if (r.is_object() && r.contains("values") && r["values"].is_object())
				{
					for (const auto& [k, v] : r["values"].items()) { part.values.push_back({ {}, k, v.is_string() ? v.get<std::string>() : v.dump() }); }
				}
			}
			else if (a_s.kind == "mcmhelper")
			{
				for (const auto& [k, v] : mcmloader::FileValues(a_s.id)) { part.values.push_back({ {}, k, v }); }
			}
			return part;
		}

		std::vector<pc::FilePart> ReadSources(const std::vector<Source>& a_sources, const std::function<bool(const Source&)>& a_want)
		{
			std::vector<pc::FilePart> out;
			AMF_REQUIRE(a_want, out);
			for (const auto& s : a_sources)
			{
				if (!a_want(s)) { continue; }
				auto part = ReadSource(s);
				if (!part.values.empty()) { out.push_back(std::move(part)); }
			}
			return out;
		}

		// ------------------------------------------------------------------------------------------- the files

		std::vector<std::string> NamesIn(const char* a_dir)
		{
			std::vector<std::string> out;
			std::error_code ec;
			for (const auto& f : fs::directory_iterator(a_dir, ec))
			{
				if (f.is_regular_file(ec) && Lower(f.path().extension().string()) == ".json") { out.push_back(f.path().stem().string()); }
			}
			return out;
		}

		std::vector<pc::Listed> List()
		{
			auto user = NamesIn(rs::FolderPath().c_str());
			const std::string inUse = rs::ActiveProfile();
			if (std::find(user.begin(), user.end(), inUse) == user.end()) { user.push_back(inUse); }   // the one in use, even unsaved
			return pc::MergeLists(user, NamesIn(kShippedDir));
		}

		fs::path PathOf(const std::string& a_name, bool a_shipped)
		{
			return fs::path(a_shipped ? std::string(kShippedDir) : rs::FolderPath()) / (rs::SafeProfileName(a_name) + ".json");
		}

		bool ReadPreset(const std::string& a_name, bool a_shipped, pc::Preset& a_out, std::string& a_error)
		{
			std::ifstream in(PathOf(a_name, a_shipped), std::ios::binary);
			if (!in)
			{
				if (!a_shipped && rs::SafeProfileName(a_name) == rs::ActiveProfile())
				{
					a_out = pc::Preset{};   // the preset in use before anything was saved into it: empty
					a_out.profile = a_name;
					return true;
				}
				a_error = "no such preset";
				return false;
			}
			std::stringstream ss;
			ss << in.rdbuf();
			return pc::Parse(ss.str(), a_out, a_error);
		}

		// ------------------------------------------------------------------------------------------- save

		std::string SaveResult(const std::string& a_name, const pc::Preset& a_p, bool a_liveRead)
		{
			int settings = 0;
			for (const auto& f : a_p.files) { settings += static_cast<int>(f.values.size()); }
			for (const auto& [k, m] : a_p.menus) { settings += static_cast<int>(m.settings.size()); }
			std::string line = Fmt(TR("AMF_SetPresetSaved", "Saved \"%s\": %d settings from %d mods. It is the preset in use now."), a_name.c_str(), settings,
				static_cast<int>(pc::Groups(a_p).size()));
			if (!a_liveRead)
			{
				line += "\n" + std::string(TR("AMF_SetPresetNotLive", "Script menus were not read now (no game loaded, an MCM page open, or a backup running): what was remembered of them went in."));
			}
			return line;
		}

		void WriteSaved(const std::string& a_name, pc::Preset a_p, bool a_liveRead)
		{
			const std::string text = pc::Write(a_p);
			if (!rs::WriteProfileFile(a_name, text, true))
			{
				End(Fmt(TR("AMF_SetPresetWriteFailed", "\"%s\" could not be written - see the log."), a_name.c_str()));
				return;
			}
			logger::info("presets: saved \"{}\" - {} file part(s), {} menu(s), live read {}", a_name, a_p.files.size(), a_p.menus.size(), a_liveRead);
			End(SaveResult(a_name, a_p, a_liveRead));
		}

		bool StartSave(const std::string& a_rawName, const std::set<std::string>& a_skip)
		{
			const std::string name = rs::SafeProfileName(a_rawName);
			if (!Begin(TR("AMF_SetPresetSaving", "Saving..."))) { return false; }
			pc::Preset p;
			p.profile = name;
			p.saved = pc::SavedStamp(LocalNow());
			p.amfVersion = AmfVersion();
			const auto sources = FileSources();
			p.files = ReadSources(sources, [&](const Source& s) { return s.shown && !a_skip.contains(s.key); });
			// the menus: what the preset in use remembers, with a live read of those found in this game on top
			p.menus = ToCore(rs::MenusInUse());
			std::erase_if(p.menus, [&](const auto& m) { return a_skip.contains(m.first) || m.second.settings.empty(); });
			const auto imported = ImportedMenus();
			std::vector<std::string> live;
			for (const auto& [key, entry] : rs::MenusPresent())
			{
				const auto it = imported.find(key);
				if (!a_skip.contains(key) && (it == imported.end() || it->second)) { live.push_back(key); }
			}
			const bool started = rs::ReadMenus(live, [name, p](std::map<std::string, rs::MenuSnapshot> a_read) mutable {
				pc::MergeMenus(p.menus, ToCore(a_read));
				WriteSaved(name, std::move(p), true);
			});
			if (!started) { WriteSaved(name, std::move(p), live.empty()); }
			return true;
		}

		// ------------------------------------------------------------------------------------------- compare

		// A script menu's record in words, as its page shows it (2.3.0: the first comparison showed raw "$AC_..." keys).
		std::string RowLabel(const std::string& a_group, const pc::MenuRecord& a_r)
		{
			if (a_r.type == "helper" || !a_group.starts_with("script|")) { return pc::RecordLabel(a_r); }
			pc::MenuRecord shown = a_r;
			shown.page = mcmloader::scripts::MemoryLabel(a_group, a_r.page);
			shown.text = mcmloader::scripts::MemoryLabel(a_group, a_r.text);
			return pc::RecordLabel(shown);
		}

		void FinishCompare(Comparison a_c)
		{
			a_c.diff = pc::Compare(a_c.preset, a_c.current, a_c.installed, RowLabel);
			for (const auto& g : pc::Groups(a_c.preset))
			{
				if (a_c.installed.contains(g.key)) { a_c.ticked.insert(g.key); }
			}
			a_c.ready = true;
			logger::info("presets: compared \"{}\"{} - {} setting(s) differ, {} not in this game, {} already the same, {} mod(s) not installed, {} mod(s) "
				"not read now ({} setting(s), not counted)", a_c.name, a_c.shipped ? " (shipped)" : "", a_c.diff.Differing(),
				a_c.diff.rows.size() - static_cast<std::size_t>(a_c.diff.Differing()), a_c.diff.same, a_c.diff.notInstalled.size(), a_c.diff.notRead.size(),
				a_c.diff.notReadSettings);
			{
				std::scoped_lock l(g_lock);
				g_cmp = std::move(a_c);
			}
			End({});
		}

		// The script menus are read BEFORE comparing (2.3.0, the 2026-10-25 run). The first comparison of that run came while a
		// script menu was still open on AMF's page (it had just been changed there), so the read was refused, nothing was
		// logged, and 1,730 settings were shown "(not read)" and counted as changes. A refused read now waits - once a second,
		// up to a minute - and only then compares, listing what still could not be read apart and counting none of it.
		constexpr int kReadWaitSeconds = 60;
		void ReadMenusThenFinish(std::shared_ptr<Comparison> a_c, std::vector<std::string> a_keys, int a_attempt)
		{
			if (!a_c)   // never null (make_shared in StartCompare); if it were, end the job so the page is not left busy
			{
				logger::warn("presets: the comparison was null - the compare was ended");
				End({});
				return;
			}
			if (a_keys.empty())
			{
				FinishCompare(std::move(*a_c));
				return;
			}
			const bool started = rs::ReadMenus(a_keys, [a_c](std::map<std::string, rs::MenuSnapshot> a_read) {
				a_c->current.menus = ToCore(a_read);
				FinishCompare(std::move(*a_c));
			});
			if (started)
			{
				if (a_attempt > 0) { logger::info("presets: the script menus could be read after {} s of waiting", a_attempt); }
				return;
			}
			if (a_attempt == 0)
			{
				logger::info("presets: the script menus cannot be read yet (an MCM page is open on this menu's page, or a backup / restore "
					"runs) - waiting up to {} s before comparing \"{}\"", kReadWaitSeconds, a_c->name);
				std::scoped_lock l(g_lock);
				g_phase = TR("AMF_SetPresetWaitRead", "Waiting until the script menus can be read - close the MCM page that is open...");
			}
			if (a_attempt >= kReadWaitSeconds)
			{
				logger::info("presets: the script menus still cannot be read after {} s - compared without them (listed apart, not counted)", kReadWaitSeconds);
				FinishCompare(std::move(*a_c));
				return;
			}
			std::thread([a_c, keys = std::move(a_keys), a_attempt]() mutable {
				std::this_thread::sleep_for(std::chrono::seconds(1));
				ReadMenusThenFinish(std::move(a_c), std::move(keys), a_attempt + 1);
			}).detach();
		}

		bool StartCompare(const std::string& a_name, bool a_shipped, std::string& a_error)
		{
			Comparison c;
			c.name = rs::SafeProfileName(a_name);
			c.shipped = a_shipped;
			if (!ReadPreset(c.name, a_shipped, c.preset, a_error))
			{
				logger::warn("presets: \"{}\" could not be read ({})", c.name, a_error);
				return false;
			}
			if (!Begin(TR("AMF_SetPresetReading", "Reading your current values..."))) { a_error = "busy"; return false; }
			{
				std::scoped_lock l(g_lock);
				g_cmp = Comparison{};
			}
			const auto sources = FileSources();
			c.installed = Installed(sources);
			std::set<std::string> fileGroups;
			for (const auto& f : c.preset.files) { fileGroups.insert(pc::GroupKey(f)); }
			c.current.files = ReadSources(sources, [&](const Source& s) { return fileGroups.contains(s.key); });
			// a file group that is here but read empty still counts as read: its settings are then "not in this game"
			for (const auto& s : sources)
			{
				if (fileGroups.contains(s.key) && std::none_of(c.current.files.begin(), c.current.files.end(), [&](const pc::FilePart& f) { return pc::GroupKey(f) == s.key; }))
				{
					c.current.files.push_back({ s.kind, s.id, s.entry, {} });
				}
			}
			std::vector<std::string> keys;
			for (const auto& [key, m] : c.preset.menus)
			{
				if (c.installed.contains(key) && !m.settings.empty()) { keys.push_back(key); }
			}
			ReadMenusThenFinish(std::make_shared<Comparison>(std::move(c)), std::move(keys), 0);
			return true;
		}

		// ------------------------------------------------------------------------------------------- load

		struct LoadRun
		{
			std::string name;
			bool shipped = false;
			std::string backup;
			pc::Preset selected;
			int set = 0, same = 0, notFound = 0, failed = 0;
			std::vector<std::string> skipped;   // entries not installed
			std::vector<std::string> errors;
			std::vector<pc::FilePart> helperParts;
			std::size_t nextHelper = 0;
			std::string menusNote;
		};

		void ApplyFilePart(const pc::FilePart& a_want, const pc::FilePart* a_now, LoadRun& a_run)
		{
			bool prismaChanged = false;
			for (const auto& v : a_want.values)
			{
				const pc::Value* now = nullptr;
				if (a_now)
				{
					const std::string id = pc::ValueIdentity(v);
					for (const auto& x : a_now->values) { if (pc::ValueIdentity(x) == id) { now = &x; break; } }
				}
				if (!now) { ++a_run.notFound; continue; }
				if (now->value == v.value) { ++a_run.same; continue; }
				json reply;
				if (a_want.kind == "ini")
				{
					reply = json::parse(inipages::ToolJson(json{ { "do", "set" }, { "mod", a_want.id }, { "section", v.section }, { "name", v.name }, { "value", v.value } }.dump()), nullptr, false);
				}
				else if (a_want.kind == "toml")
				{
					reply = json::parse(tomlpages::ToolJson(json{ { "do", "set" }, { "file", a_want.id }, { "name", v.name }, { "value", v.value } }.dump(), false), nullptr, false);
				}
				else if (a_want.kind == "prisma")
				{
					reply = json::parse(prisma::ToolJson(json{ { "do", "set" }, { "mod", a_want.id }, { "id", v.name }, { "value", v.value } }.dump()), nullptr, false);
					prismaChanged = true;
				}
				const bool ok = reply.is_object() && reply.value("ok", false);
				if (ok) { ++a_run.set; }
				else
				{
					++a_run.failed;
					const std::string why = reply.is_object() ? reply.value("error", std::string("?")) : std::string("?");
					a_run.errors.push_back(a_want.entry + " / " + pc::ValueLabel(v) + ": " + why);
					logger::warn("presets: {} {} {} = {} not written ({})", a_want.kind, a_want.id, pc::ValueLabel(v), v.value, why);
				}
			}
			if (prismaChanged) { prisma::ToolJson(json{ { "do", "apply" }, { "mod", a_want.id } }.dump()); }   // Redux's Apply: written, the mod told
		}

		void FinishLoad(const std::shared_ptr<LoadRun>& a_run)
		{
			if (!a_run)   // never null (make_shared in StartLoad); if it were, end the job so the page is not left busy
			{
				logger::warn("presets: the load run was null - the load was ended");
				End({});
				return;
			}
			std::string skipped;
			for (std::size_t i = 0; i < a_run->skipped.size() && i < 20; ++i) { skipped += (i ? ", " : "") + a_run->skipped[i]; }
			std::string line = Fmt(TR("AMF_SetPresetLoaded", "Loaded \"%s\": %d settings set, %d already the same, %d not found, %d not written."),
				a_run->name.c_str(), a_run->set, a_run->same, a_run->notFound, a_run->failed);
			if (!a_run->skipped.empty())
			{
				line += "\n" + Fmt(TR("AMF_SetPresetSkipped", "Skipped - not installed (%d): %s"), static_cast<int>(a_run->skipped.size()), skipped.c_str());
			}
			if (!a_run->menusNote.empty()) { line += "\n" + a_run->menusNote; }
			for (std::size_t i = 0; i < a_run->errors.size() && i < 5; ++i) { line += "\n" + a_run->errors[i]; }
			line += "\n" + Fmt(TR("AMF_SetPresetBackedUpAs", "Your values from before are kept as the preset \"%s\"."), a_run->backup.c_str());
			logger::info("presets: loaded \"{}\"{} - {} set, {} same, {} not found, {} failed, {} skipped; backup \"{}\"", a_run->name,
				a_run->shipped ? " (shipped)" : "", a_run->set, a_run->same, a_run->notFound, a_run->failed, a_run->skipped.size(), a_run->backup);
			{
				std::scoped_lock l(g_lock);
				g_cmp = Comparison{};   // the values changed: compare again to see them
			}
			End(line);
		}

		void ApplyMenusStep(const std::shared_ptr<LoadRun>& a_run)
		{
			if (!a_run)   // never null (make_shared in StartLoad); if it were, end the job so the page is not left busy
			{
				logger::warn("presets: the load run was null - the load was ended");
				End({});
				return;
			}
			if (a_run->selected.menus.empty())
			{
				FinishLoad(a_run);
				return;
			}
			auto menus = FromCore(a_run->selected.menus);
			const bool started = rs::ApplyMenus(menus, [a_run, menus](int a_set, int a_missing, int) {
				a_run->set += a_set;
				a_run->notFound += a_missing;
				rs::MergeIntoActive(menus);   // the game's values now: into the new-game record (never into a preset)
				FinishLoad(a_run);
			});
			if (!started)
			{
				a_run->menusNote = TR("AMF_SetPresetMenusWait", "Script menus were not set: close the MCM page that is open, or wait for the backup or restore that is running, then load again.");
				FinishLoad(a_run);
			}
		}

		void ApplyHelperStep(std::shared_ptr<LoadRun> a_run)
		{
			if (!a_run)   // never null (make_shared in StartLoad); if it were, end the job so the page is not left busy
			{
				logger::warn("presets: the load run was null - the load was ended");
				End({});
				return;
			}
			if (a_run->nextHelper >= a_run->helperParts.size())
			{
				ApplyMenusStep(a_run);
				return;
			}
			const pc::FilePart& part = a_run->helperParts[a_run->nextHelper++];
			std::vector<std::pair<std::string, std::string>> values;
			for (const auto& v : part.values) { values.emplace_back(v.name, v.value); }
			mcmloader::ApplyFileValues(part.id, std::move(values), [a_run](int a_set, int a_missing) {
				a_run->set += a_set;
				a_run->notFound += a_missing;
				ApplyHelperStep(a_run);
			});
		}

		bool StartLoad(const std::set<std::string>& a_keys, std::string& a_error)
		{
			Comparison c;
			{
				std::scoped_lock l(g_lock);
				c = g_cmp;
			}
			if (!c.ready) { a_error = "compare first (do=diff)"; return false; }
			if (!Begin(TR("AMF_SetPresetLoading", "Loading..."))) { a_error = "busy"; return false; }
			std::set<std::string> keys = a_keys.empty() ? c.ticked : a_keys;
			std::erase_if(keys, [&](const std::string& k) { return !c.installed.contains(k); });

			auto run = std::make_shared<LoadRun>();
			run->name = c.name;
			run->shipped = c.shipped;
			run->selected = pc::Select(c.preset, keys);
			if (keys.empty()) { run->selected = pc::Preset{}; }
			for (const auto& [group, entry] : c.diff.notInstalled) { run->skipped.push_back(entry); }

			// 1. the backup: the ticked mods' values now (files read again; script menus as the comparison read them)
			const auto sources = FileSources();
			std::set<std::string> fileGroups;
			for (const auto& f : run->selected.files) { fileGroups.insert(pc::GroupKey(f)); }
			pc::Preset backup;
			backup.profile = pc::BackupName(LocalNow());
			backup.saved = pc::SavedStamp(LocalNow());
			backup.amfVersion = AmfVersion();
			backup.files = ReadSources(sources, [&](const Source& s) { return fileGroups.contains(s.key); });
			const auto inUse = ToCore(rs::MenusInUse());
			for (const auto& [key, m] : run->selected.menus)
			{
				if (const auto it = c.current.menus.find(key); it != c.current.menus.end()) { backup.menus[key] = it->second; }
				else if (const auto r = inUse.find(key); r != inUse.end()) { backup.menus[key] = r->second; }
			}
			run->backup = backup.profile;
			if (!rs::WriteProfileFile(backup.profile, pc::Write(backup), false))
			{
				End(TR("AMF_SetPresetBackupFailed", "Nothing was loaded: the backup of your current values could not be written - see the log."));
				return true;
			}
			logger::info("presets: backup \"{}\" written before loading \"{}\"", backup.profile, c.name);

			// 2. a user preset becomes the preset in use - a name only: the preset file is not written by anything but Save, and
			//    what a new game sets again is the separate record its menus are merged into below
			if (!c.shipped && rs::ActiveProfile() != c.name) { rs::SwitchProfile(c.name); }

			// 3. the files, through each page's own set; MCM Helper's values in one visit per mod; then the script menus
			for (const auto& want : run->selected.files)
			{
				if (want.kind == "mcmhelper")
				{
					run->helperParts.push_back(want);
					continue;
				}
				const std::string key = pc::GroupKey(want);
				const auto now = std::find_if(backup.files.begin(), backup.files.end(), [&](const pc::FilePart& f) { return pc::GroupKey(f) == key; });
				ApplyFilePart(want, now != backup.files.end() ? &*now : nullptr, *run);
			}
			{
				std::scoped_lock l(g_lock);
				g_phase = TR("AMF_SetPresetLoadingMenus", "Setting MCM menus...");
			}
			ApplyHelperStep(run);
			return true;
		}

		bool Delete(const std::string& a_name, std::string& a_error)
		{
			if (Busy()) { a_error = "busy"; return false; }
			if (!rs::DeletePreset(a_name)) { a_error = "no such user preset (shipped presets are never deleted)"; return false; }
			std::scoped_lock l(g_lock);
			if (!g_cmp.shipped && g_cmp.name == rs::SafeProfileName(a_name)) { g_cmp = Comparison{}; }
			g_last = Fmt(TR("AMF_SetPresetDeleted", "Deleted \"%s\"."), a_name.c_str());
			return true;
		}

		// ------------------------------------------------------------------------------------------- the page

		// The pick is held by NAME (2.3.0, the 2026-10-25 run): an index into the list pointed at another preset as soon as a
		// load's backup or a delete changed the list ("Backup ..." showed where the preset in use had been). Empty = the preset
		// in use; dropped whenever the preset in use changes, whoever changed it (pc::DropdownPick - retest 453d, c0a3: a
		// DevBench delete of the preset in use left a shipped preset picked).
		pc::DropdownPick g_pick;
		char g_newName[64] = {};
		bool g_confirmDelete = false;
		std::set<std::string> g_skip;    // groups left out of Save

		const char* ValueShown(const pc::DiffRow& a_r)
		{
			if (a_r.state == 1) { return TR("AMF_SetPresetNotHere", "(not in this game)"); }
			return a_r.current.c_str();
		}
	}

	std::string InUse() { return rs::ActiveProfile(); }

	void DrawGeneralSection()
	{
		ImGui::SeparatorText(icons::Label(icons::kSave, TR("AMF_SetPresetTitle", "Presets")).c_str());
		ImGui::TextWrapped("%s", TR("AMF_SetPresetHelp", "A preset keeps the settings of every mod in this menu - INI and TOML pages, Prisma and MCM "
			"Helper menus and menus written in a mod's script - in one file. Keep several (easy, hard, ...) and load one any time: "
			"you see what changes first, and your values from before are kept as a backup preset."));
		ImGui::TextWrapped("%s", TR("AMF_SetPresetSnapshot", "A preset changes only when you save it. Changes you make in the menus are remembered for a new "
			"game (Converted menus > MCM > Remembered settings), never written into a preset - so loading a preset puts back what it held."));

		const bool busy = Busy();
		std::string phase, last;
		Comparison cmp;
		{
			std::scoped_lock l(g_lock);
			phase = g_phase;
			last = g_last;
			cmp = g_cmp;
		}
		const std::string inUse = rs::ActiveProfile();
		static std::vector<pc::Listed> s_list;
		static double s_listedAt = -10.0;
		if (ImGui::GetTime() - s_listedAt > 2.0 || s_list.empty())
		{
			s_list = List();
			s_listedAt = ImGui::GetTime();
		}
		if (pc::DropdownInUseChanged(g_pick, inUse))
		{
			// the preset in use changed - a save, a load, a delete, here or through DevBench: read the list again now
			s_list = List();
			s_listedAt = ImGui::GetTime();
			g_confirmDelete = false;
			logger::debug("presets: the preset in use is \"{}\" - the dropdown follows it", inUse);
		}
		int pick = pc::DropdownRow(s_list, g_pick, inUse);
		auto shownName = [&](const pc::Listed& a_l) {
			std::string s = a_l.name;
			if (a_l.shipped) { s += std::string(" ") + TR("AMF_SetPresetShipped", "(shipped)"); }
			else if (a_l.name == inUse) { s += std::string(" ") + TR("AMF_SetPresetInUseMark", "(in use)"); }
			return s;
		};

		ImGui::Text(TR("AMF_SetPresetInUse", "Preset in use: %s"), inUse.c_str());
		if (busy) { ImGui::BeginDisabled(); }
		const pc::Listed* picked = pick < 0 ? nullptr : &s_list[static_cast<std::size_t>(pick)];
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		// 2.3.0 (the 2026-10-25 run): the open list could not be scrolled to a preset below its eighth row - the list now opens
		// tall enough for every preset (up to the screen's height), so each one is a click away without scrolling
		{
			const float rows = static_cast<float>(s_list.size() < 1 ? 1 : s_list.size());
			const float itemH = ImGui::GetTextLineHeightWithSpacing();
			const float maxH = ImGui::GetIO().DisplaySize.y * 0.8f;
			const float wantH = rows * itemH + ImGui::GetStyle().WindowPadding.y * 2.0f + 2.0f;
			ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, wantH < maxH ? wantH : maxH));
		}
		const std::string preview = picked ? shownName(*picked) : inUse + " " + TR("AMF_SetPresetInUseMark", "(in use)");
		if (theme::BeginComboTight(TR("AMF_SetPresetPick", "Preset"), preview.c_str(), ImGuiComboFlags_HeightLargest))
		{
			for (int i = 0; i < static_cast<int>(s_list.size()); ++i)
			{
				const auto& item = s_list[static_cast<std::size_t>(i)];
				if (ImGui::Selectable(shownName(item).c_str(), i == pick))
				{
					pick = i;
					g_pick.name = item.name;
					g_pick.shipped = item.shipped;
					g_confirmDelete = false;
				}
				if (i == pick && ImGui::IsWindowAppearing()) { ImGui::SetItemDefaultFocus(); }
			}
			ImGui::EndCombo();
		}
		picked = pick < 0 ? nullptr : &s_list[static_cast<std::size_t>(pick)];
		ImGui::SameLine();
		if (ImGui::Button(icons::Label(icons::kFolderOpen, TR("AMF_SetPresetLoad", "Load...")).c_str()) && picked)
		{
			std::string error;
			if (!StartCompare(picked->name, picked->shipped, error))
			{
				std::scoped_lock l(g_lock);
				g_last = Fmt(TR("AMF_SetPresetCannotRead", "\"%s\" could not be read: %s"), picked->name.c_str(), error.c_str());
			}
			g_confirmDelete = false;
		}
		ImGui::SameLine();
		const bool canDelete = picked && !picked->shipped;
		if (!canDelete) { ImGui::BeginDisabled(); }
		if (!g_confirmDelete)
		{
			if (ImGui::Button((icons::Label(icons::kTrash, TR("AMF_SetPresetDelete", "Delete")) + "###presetdelete").c_str())) { g_confirmDelete = true; }
		}
		else
		{
			if (ImGui::Button((icons::Label(icons::kTrash, TR("AMF_SetPresetDeleteYes", "Delete it")) + "###presetdelete").c_str()) && picked)
			{
				std::string error;
				if (!Delete(picked->name, error))
				{
					std::scoped_lock l(g_lock);
					g_last = error;
				}
				g_confirmDelete = false;
				s_listedAt = -10.0;
				g_pick.name.clear();   // back to the preset in use
			}
			ImGui::SameLine();
			if (ImGui::Button((icons::Label(icons::kClear, TR("AMF_SetPresetDeleteNo", "Keep it")) + "###presetkeep").c_str())) { g_confirmDelete = false; }
		}
		if (!canDelete) { ImGui::EndDisabled(); }

		// save
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
		ImGui::InputTextWithHint("##presetname", TR("AMF_SetPresetNewName", "Name for the preset"), g_newName, sizeof(g_newName));
		keyboard::NoteTextField(ImGui::GetItemID());
		ImGui::SameLine();
		const bool named = g_newName[0] != '\0';
		if (!named) { ImGui::BeginDisabled(); }
		if (ImGui::Button(icons::Label(icons::kSave, TR("AMF_SetPresetSave", "Save current as")).c_str()))
		{
			if (StartSave(g_newName, g_skip)) { g_newName[0] = '\0'; }
			s_listedAt = -10.0;
			g_pick.name.clear();   // the saved preset is the one in use
		}
		if (!named) { ImGui::EndDisabled(); }

		// which mods Save takes (all that are on in this menu, by default)
		static std::vector<std::pair<std::string, std::string>> s_saveable;   // (group, entry)
		static double s_saveableAt = -10.0;
		char header[160];
		std::snprintf(header, sizeof(header), TR("AMF_SetPresetSaveMods", "Mods a save takes (%d of %d)"),
			static_cast<int>(s_saveable.size() - std::count_if(s_saveable.begin(), s_saveable.end(), [](const auto& g) { return g_skip.contains(g.first); })),
			static_cast<int>(s_saveable.size()));
		if (ImGui::CollapsingHeader((std::string(header) + "###presetsavemods").c_str()))   // a fixed id: the count is in the label
		{
			if (ImGui::GetTime() - s_saveableAt > 5.0)
			{
				s_saveable.clear();
				for (const auto& s : FileSources()) { if (s.shown) { s_saveable.emplace_back(s.key, s.entry); } }
				const auto imported = ImportedMenus();
				for (const auto& [key, entry] : rs::MenusPresent())
				{
					const auto it = imported.find(key);
					if (it == imported.end() || it->second) { s_saveable.emplace_back(key, entry); }
				}
				std::sort(s_saveable.begin(), s_saveable.end(), [](const auto& a, const auto& b) { return Lower(a.second) < Lower(b.second); });
				s_saveableAt = ImGui::GetTime();
			}
			for (const auto& [key, entry] : s_saveable)
			{
				ImGui::PushID(key.c_str());
				bool on = !g_skip.contains(key);
				if (widgets::Toggle(entry.c_str(), &on))
				{
					if (on) { g_skip.erase(key); }
					else { g_skip.insert(key); }
				}
				ImGui::PopID();
			}
		}
		else if (s_saveable.empty() && ImGui::GetTime() - s_saveableAt > 5.0)
		{
			s_saveableAt = ImGui::GetTime() - 4.0;   // counted once soon after the page opens, without walking the files every frame
			for (const auto& s : FileSources()) { if (s.shown) { s_saveable.emplace_back(s.key, s.entry); } }
			const auto imported = ImportedMenus();
			for (const auto& [key, entry] : rs::MenusPresent())
			{
				const auto it = imported.find(key);
				if (it == imported.end() || it->second) { s_saveable.emplace_back(key, entry); }
			}
		}
		if (busy) { ImGui::EndDisabled(); }

		// status
		if (busy && !phase.empty()) { ImGui::TextDisabled("%s", phase.c_str()); }
		else if (!last.empty())
		{
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextDisabled("%s", last.c_str());
			ImGui::PopTextWrapPos();
		}

		// the comparison: what a load changes, one switch per mod
		if (cmp.ready && !busy)
		{
			ImGui::Spacing();
			// only settings READ and found to differ are changes (2.3.0): "not in this game" rows and mods not read now count as none
			ImGui::TextWrapped(TR("AMF_SetPresetDiffHeader", "Loading \"%s\" changes %d settings in %d mods; %d are already the same."), cmp.name.c_str(),
				cmp.diff.Differing(), static_cast<int>(cmp.diff.changes.size()), cmp.diff.same);
			if (!cmp.diff.notRead.empty())
			{
				std::string names;
				for (std::size_t i = 0; i < cmp.diff.notRead.size() && i < 20; ++i) { names += (i ? ", " : "") + cmp.diff.notRead[i].second; }
				if (cmp.diff.notRead.size() > 20) { names += ", ..."; }
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled(TR("AMF_SetPresetNotReadList", "Not read now, so not compared or counted (%d mods): %s. Close the MCM page that is open and press Load... again; loading still sets them."),
					static_cast<int>(cmp.diff.notRead.size()), names.c_str());
				ImGui::PopTextWrapPos();
			}
			std::vector<std::pair<std::string, std::string>> groups;   // in the rows' order
			for (const auto& r : cmp.diff.rows)
			{
				if (groups.empty() || groups.back().first != r.group) { groups.emplace_back(r.group, r.entry); }
			}
			for (const auto& [group, entry] : groups)
			{
				ImGui::PushID(group.c_str());
				bool on = cmp.ticked.contains(group);
				if (widgets::Toggle(entry.c_str(), &on))
				{
					std::scoped_lock l(g_lock);
					if (on) { g_cmp.ticked.insert(group); }
					else { g_cmp.ticked.erase(group); }
				}
				if (ImGui::BeginTable("##rows", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn(TR("AMF_SetPresetColSetting", "Setting"));
					ImGui::TableSetupColumn(TR("AMF_SetPresetColCurrent", "Now"));
					ImGui::TableSetupColumn(TR("AMF_SetPresetColPreset", "Preset"));
					ImGui::TableHeadersRow();
					int shown = 0, more = 0;
					for (const auto& r : cmp.diff.rows)
					{
						if (r.group != group) { continue; }
						if (shown >= 100) { ++more; continue; }
						++shown;
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextWrapped("%s", r.setting.c_str());   // wrapped in its cell: a long label never widens the page
						ImGui::TableSetColumnIndex(1);
						if (r.state != 0) { ImGui::TextDisabled("%s", ValueShown(r)); }
						else { ImGui::TextWrapped("%s", r.current.c_str()); }
						ImGui::TableSetColumnIndex(2);
						ImGui::TextWrapped("%s", r.preset.c_str());
					}
					ImGui::EndTable();
					if (more > 0) { ImGui::TextDisabled(TR("AMF_SetPresetMoreRows", "... and %d more"), more); }
				}
				ImGui::PopID();
			}
			if (!cmp.diff.notInstalled.empty())
			{
				std::string names;
				for (std::size_t i = 0; i < cmp.diff.notInstalled.size(); ++i) { names += (i ? ", " : "") + cmp.diff.notInstalled[i].second; }
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextDisabled(TR("AMF_SetPresetSkipped", "Skipped - not installed (%d): %s"), static_cast<int>(cmp.diff.notInstalled.size()), names.c_str());
				ImGui::PopTextWrapPos();
			}
			if (ImGui::Button(icons::Label(icons::kCheck, TR("AMF_SetPresetLoadTicked", "Load the mods switched on")).c_str()))
			{
				std::string error;
				if (!StartLoad({}, error))
				{
					std::scoped_lock l(g_lock);
					g_last = error;
				}
				s_listedAt = -10.0;
			}
			ImGui::SameLine();
			if (ImGui::Button(icons::Label(icons::kClear, TR("AMF_SetPresetCancel", "Cancel")).c_str()))
			{
				std::scoped_lock l(g_lock);
				g_cmp = Comparison{};
			}
		}

		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_SetPresetFolders", "Your presets are files in Data\\SKSE\\Plugins\\ApocryphaMenuFramework\\RememberedSettings "
			"(under Mod Organizer, in Overwrite). Presets that come with a modlist are in Data\\SKSE\\Plugins\\ApocryphaMenuFramework\\Presets "
			"and are read only. FLICK pages keep their own values and are not in presets."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Spacing();
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const json args = json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* k) { return args.is_object() && args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string(); };
		auto keys = [&](const char* k) {
			std::set<std::string> out;
			if (args.is_object() && args.contains(k) && args[k].is_array())
			{
				for (const auto& v : args[k]) { if (v.is_string()) { out.insert(v.get<std::string>()); } }
			}
			return out;
		};
		const std::string what = str("do").empty() ? "status" : str("do");
		json out{ { "ok", true }, { "op", "presets" }, { "do", what } };
		if (what == "list")
		{
			json list = json::array();
			for (const auto& l : List()) { list.push_back({ { "name", l.name }, { "shipped", l.shipped } }); }
			out["presets"] = list;
			out["inUse"] = rs::ActiveProfile();
			out["userFolder"] = rs::FolderPath();
			out["shippedFolder"] = kShippedDir;
			return out.dump();
		}
		if (what == "save")
		{
			if (str("name").empty()) { return R"({"ok":false,"op":"presets","error":"save needs a name"})"; }
			out["ok"] = out["started"] = StartSave(str("name"), keys("skip"));
			return out.dump();
		}
		if (what == "diff")
		{
			std::string error;
			const bool shipped = args.is_object() && args.contains("shipped") && args["shipped"].is_boolean() && args["shipped"].get<bool>();
			out["ok"] = out["started"] = StartCompare(str("name"), shipped, error);
			if (!error.empty()) { out["error"] = error; }
			return out.dump();
		}
		if (what == "load")
		{
			std::string error;
			out["ok"] = out["started"] = StartLoad(keys("keys"), error);
			if (!error.empty()) { out["error"] = error; }
			return out.dump();
		}
		if (what == "delete")
		{
			std::string error;
			out["ok"] = Delete(str("name"), error);
			if (!error.empty()) { out["error"] = error; }
			return out.dump();
		}
		if (what != "status") { return R"({"ok":false,"op":"presets","error":"do = list | save {name, skip} | diff {name, shipped} | load {keys} | delete {name} | status"})"; }

		Comparison cmp;
		{
			std::scoped_lock l(g_lock);
			out["phase"] = g_phase;
			out["last"] = g_last;
			cmp = g_cmp;
		}
		out["busy"] = Busy();
		out["inUse"] = rs::ActiveProfile();
		json c{ { "ready", cmp.ready }, { "name", cmp.name }, { "shipped", cmp.shipped }, { "same", cmp.diff.same }, { "differs", cmp.diff.Differing() } };
		json notRead = json::array();
		for (const auto& [g, e] : cmp.diff.notRead) { notRead.push_back({ { "group", g }, { "entry", e } }); }
		c["notRead"] = notRead;
		c["notReadSettings"] = cmp.diff.notReadSettings;
		json rows = json::array();
		for (const auto& r : cmp.diff.rows)
		{
			rows.push_back({ { "group", r.group }, { "entry", r.entry }, { "setting", r.setting }, { "current", r.current }, { "preset", r.preset },
				{ "state", r.state == 0 ? "differs" : "not found" } });
		}
		c["rows"] = rows;
		json missing = json::array();
		for (const auto& [g, e] : cmp.diff.notInstalled) { missing.push_back({ { "group", g }, { "entry", e } }); }
		c["notInstalled"] = missing;
		c["ticked"] = std::vector<std::string>(cmp.ticked.begin(), cmp.ticked.end());
		json groups = json::array();
		for (const auto& g : pc::Groups(cmp.preset)) { groups.push_back({ { "key", g.key }, { "entry", g.entry }, { "kind", g.kind }, { "settings", g.settings } }); }
		c["groups"] = groups;
		out["compare"] = c;
		return out.dump();
	}
}
