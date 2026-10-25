#pragma once

// ============================================================================================
// SETTINGS PRESETS - the game-free part (2.3.0; the owner, 2026-10-25: "save all of AMF's current settings across mods,
// like MCM normally does", so players can keep presets - easy / hard, profiles - and a modlist author can ship one).
// Plan: D:\Claude output\4. plans\amf-settings-presets\PLAN.md. Presets.h is the engine and the page; this header is the
// file format, the selection, the comparison and the names, checked off-game by tools\presets_check.bat.
//
// A preset IS a Remembered settings profile, extended (RememberedSettings.h): the same file, the same "menus" block
// (script menus and MCM Helper save-kept values, one record per setting), plus a "files" list - the values AMF writes into
// files (INI pages, TOML pages, Prisma MCM Redux, MCM Helper ModSetting values). Keys this version does not know are kept.
// ============================================================================================

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace presetscore
{
	using json = nlohmann::json;

	inline constexpr int kVersion = 1;

	// One value in a file: INI keeps a section, the others a name only (TOML: the key's id "Table.key"; Prisma: the setting
	// id; MCM Helper: the control key). The value is text, as the page's own control takes it.
	struct Value
	{
		std::string section;
		std::string name;
		std::string value;
	};

	// The values of one file-backed source. kind: ini | toml | prisma | mcmhelper.
	struct FilePart
	{
		std::string kind;
		std::string id;
		std::string entry;
		std::vector<Value> values;
	};

	// One Remembered settings record (RememberedSettings.h, Record), as text.
	struct MenuRecord
	{
		std::string type;   // toggle | slider | menu | color | keymap | input | helper
		std::string page;
		std::string text;
		int nth = 0;
		std::string id;
		std::string value;
		int menuIndex = -1;
	};

	struct MenuPart
	{
		std::string entry;
		bool autoRestore = true;
		std::vector<MenuRecord> settings;
	};

	struct Preset
	{
		int version = kVersion;
		std::string profile;
		std::string saved;
		std::string amfVersion;
		std::map<std::string, MenuPart> menus;   // import key ("script|<plugin>|<ModName>", "mcmhelper|<mod>") -> records
		std::vector<FilePart> files;
		json extra = json::object();             // top-level keys this version does not know, written back as they were
	};

	namespace detail
	{
		inline std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}
		inline std::string Str(const json& a_j, const char* a_k)
		{
			const auto it = a_j.find(a_k);
			if (it == a_j.end()) { return {}; }
			if (it->is_string()) { return it->get<std::string>(); }
			if (it->is_boolean()) { return it->get<bool>() ? "1" : "0"; }
			if (it->is_number()) { return it->dump(); }
			return {};
		}
		inline int Int(const json& a_j, const char* a_k, int a_default)
		{
			const auto it = a_j.find(a_k);
			return it != a_j.end() && it->is_number_integer() ? it->get<int>() : a_default;
		}
	}

	// The group a part belongs to - one tick in the comparison, one row in "not installed". Files: "<kind>|<id lower>";
	// menus: their import key.
	// MCM Helper's file values take "mcmhelper-ini|", so they never share a key with the same mod's save-kept values
	// ("mcmhelper|<mod>", a Remembered settings import key).
	inline std::string GroupKey(const std::string& a_kind, const std::string& a_id)
	{
		return (a_kind == "mcmhelper" ? std::string("mcmhelper-ini") : a_kind) + "|" + detail::Lower(a_id);
	}
	inline std::string GroupKey(const FilePart& a_f) { return GroupKey(a_f.kind, a_f.id); }

	// How a setting is named in the comparison, and what makes two records the same setting.
	inline std::string RecordIdentity(const MenuRecord& a_r)
	{
		if (a_r.type == "helper") { return "helper\x1f" + a_r.id; }
		return a_r.type + "\x1f" + a_r.page + "\x1f" + a_r.text + "\x1f" + std::to_string(a_r.nth);
	}
	inline std::string RecordLabel(const MenuRecord& a_r)
	{
		if (a_r.type == "helper") { return a_r.id; }
		std::string out = a_r.page.empty() ? a_r.text : a_r.page + " > " + a_r.text;
		if (a_r.nth > 0) { out += " (" + std::to_string(a_r.nth + 1) + ")"; }
		return out;
	}
	inline std::string ValueIdentity(const Value& a_v) { return detail::Lower(a_v.section) + "\x1f" + detail::Lower(a_v.name); }
	inline std::string ValueLabel(const Value& a_v) { return a_v.section.empty() ? a_v.name : a_v.section + " > " + a_v.name; }

	// ------------------------------------------------------------------------------------------- the file

	// False with a reason for text that is not a preset: not JSON, not an object, or a presetVersion newer than this build
	// reads. A Remembered settings profile from before 2.3.0 (no presetVersion, no files) is a preset with only menus.
	inline bool Parse(const std::string& a_text, Preset& a_out, std::string& a_error)
	{
		a_out = Preset{};
		const json file = json::parse(a_text, nullptr, false);
		if (file.is_discarded() || !file.is_object())
		{
			a_error = "not a JSON object";
			return false;
		}
		const int version = detail::Int(file, "presetVersion", kVersion);
		if (version > kVersion)
		{
			a_error = "presetVersion " + std::to_string(version) + " is newer than this AMF reads (" + std::to_string(kVersion) + ")";
			return false;
		}
		a_out.version = version;
		a_out.profile = detail::Str(file, "profile");
		a_out.saved = detail::Str(file, "saved");
		a_out.amfVersion = detail::Str(file, "amfVersion");
		if (const auto m = file.find("menus"); m != file.end() && m->is_object())
		{
			for (const auto& [key, menu] : m->items())
			{
				if (!menu.is_object()) { continue; }
				MenuPart part;
				part.entry = detail::Str(menu, "entry");
				part.autoRestore = !menu.contains("autoRestore") || !menu["autoRestore"].is_boolean() || menu["autoRestore"].get<bool>();
				if (const auto s = menu.find("settings"); s != menu.end() && s->is_array())
				{
					for (const auto& r : *s)
					{
						if (!r.is_object()) { continue; }
						MenuRecord rec;
						rec.type = detail::Str(r, "type");
						rec.page = detail::Str(r, "page");
						rec.text = detail::Str(r, "text");
						rec.nth = detail::Int(r, "nth", 0);
						rec.id = detail::Str(r, "id");
						rec.value = detail::Str(r, "value");
						rec.menuIndex = detail::Int(r, "menuIndex", -1);
						if (rec.type.empty()) { rec.type = "helper"; }
						part.settings.push_back(std::move(rec));
					}
				}
				a_out.menus[key] = std::move(part);
			}
		}
		if (const auto f = file.find("files"); f != file.end() && f->is_array())
		{
			for (const auto& p : *f)
			{
				if (!p.is_object()) { continue; }
				FilePart part;
				part.kind = detail::Lower(detail::Str(p, "kind"));
				part.id = detail::Str(p, "id");
				part.entry = detail::Str(p, "entry");
				if (part.kind != "ini" && part.kind != "toml" && part.kind != "prisma" && part.kind != "mcmhelper") { continue; }
				if (part.id.empty()) { continue; }
				if (const auto v = p.find("values"); v != p.end() && v->is_array())
				{
					for (const auto& e : *v)
					{
						if (!e.is_object() || detail::Str(e, "name").empty()) { continue; }
						part.values.push_back({ detail::Str(e, "section"), detail::Str(e, "name"), detail::Str(e, "value") });
					}
				}
				a_out.files.push_back(std::move(part));
			}
		}
		for (const auto& [k, v] : file.items())
		{
			if (k != "presetVersion" && k != "profile" && k != "saved" && k != "amfVersion" && k != "menus" && k != "files") { a_out.extra[k] = v; }
		}
		return true;
	}

	// The menus block exactly as Remembered settings writes it (RememberedSettings.cpp, SaveLocked).
	inline json MenusJson(const std::map<std::string, MenuPart>& a_menus)
	{
		json menus = json::object();
		for (const auto& [key, m] : a_menus)
		{
			json list = json::array();
			for (const MenuRecord& r : m.settings)
			{
				json j{ { "type", r.type }, { "value", r.value } };
				if (r.type == "helper") { j["id"] = r.id; }
				else
				{
					j["page"] = r.page;
					j["text"] = r.text;
					j["nth"] = r.nth;
					if (r.type == "menu") { j["menuIndex"] = r.menuIndex; }
				}
				list.push_back(std::move(j));
			}
			menus[key] = { { "entry", m.entry }, { "autoRestore", m.autoRestore }, { "settings", std::move(list) } };
		}
		return menus;
	}

	inline json FilesJson(const std::vector<FilePart>& a_files)
	{
		json files = json::array();
		for (const FilePart& f : a_files)
		{
			json values = json::array();
			for (const Value& v : f.values)
			{
				json e{ { "name", v.name }, { "value", v.value } };
				if (!v.section.empty()) { e["section"] = v.section; }
				values.push_back(std::move(e));
			}
			files.push_back({ { "kind", f.kind }, { "id", f.id }, { "entry", f.entry }, { "values", std::move(values) } });
		}
		return files;
	}

	inline std::string Write(const Preset& a_p)
	{
		json file = a_p.extra.is_object() ? a_p.extra : json::object();
		file["presetVersion"] = kVersion;
		file["profile"] = a_p.profile;
		if (!a_p.saved.empty()) { file["saved"] = a_p.saved; }
		if (!a_p.amfVersion.empty()) { file["amfVersion"] = a_p.amfVersion; }
		file["menus"] = MenusJson(a_p.menus);
		file["files"] = FilesJson(a_p.files);
		return file.dump(1, '\t');
	}

	// ------------------------------------------------------------------------------------------- groups and selection

	struct Group
	{
		std::string key;
		std::string entry;
		std::string kind;   // ini | toml | prisma | mcmhelper | script | mcmhelper-save
		int settings = 0;
	};

	inline std::string MenuKind(const std::string& a_key) { return a_key.rfind("script|", 0) == 0 ? "script" : "mcmhelper-save"; }

	// Every group in a preset, sorted by entry. A mod with values in both a file and its menus block (MCM Helper: ModSetting
	// values and save-kept ones) is two groups, both named after the mod.
	inline std::vector<Group> Groups(const Preset& a_p)
	{
		std::vector<Group> out;
		for (const FilePart& f : a_p.files) { out.push_back({ GroupKey(f), f.entry.empty() ? f.id : f.entry, f.kind, static_cast<int>(f.values.size()) }); }
		for (const auto& [key, m] : a_p.menus)
		{
			if (m.settings.empty()) { continue; }
			out.push_back({ key, m.entry.empty() ? key : m.entry, MenuKind(key), static_cast<int>(m.settings.size()) });
		}
		std::stable_sort(out.begin(), out.end(), [](const Group& a, const Group& b) { return detail::Lower(a.entry) < detail::Lower(b.entry); });
		return out;
	}

	// Only the groups named (an empty set keeps every group).
	inline Preset Select(const Preset& a_p, const std::set<std::string>& a_keys)
	{
		if (a_keys.empty()) { return a_p; }
		Preset out = a_p;
		out.files.clear();
		out.menus.clear();
		for (const FilePart& f : a_p.files) { if (a_keys.contains(GroupKey(f))) { out.files.push_back(f); } }
		for (const auto& [key, m] : a_p.menus) { if (a_keys.contains(key)) { out.menus[key] = m; } }
		return out;
	}

	// a_over's records over a_base's, by setting: a setting in both takes a_over's value in place, a new one goes last
	// (Remembered settings' merge - an "enable" switch first changed stays first).
	inline void MergeMenus(std::map<std::string, MenuPart>& a_base, const std::map<std::string, MenuPart>& a_over)
	{
		for (const auto& [key, m] : a_over)
		{
			MenuPart& into = a_base[key];
			if (!m.entry.empty()) { into.entry = m.entry; }
			for (const MenuRecord& r : m.settings)
			{
				const std::string id = RecordIdentity(r);
				const auto it = std::find_if(into.settings.begin(), into.settings.end(), [&](const MenuRecord& x) { return RecordIdentity(x) == id; });
				if (it != into.settings.end()) { *it = r; }
				else { into.settings.push_back(r); }
			}
		}
	}

	// ------------------------------------------------------------------------------------------- the comparison

	struct DiffRow
	{
		std::string group;
		std::string entry;
		std::string setting;
		std::string current;
		std::string preset;
		int state = 0;   // 0 differs, 1 not found in this game (the mod is there, the setting is not)
	};

	struct Diff
	{
		std::vector<DiffRow> rows;                                   // only what a load would change (or cannot)
		std::vector<std::pair<std::string, std::string>> notInstalled;   // (group, entry): skipped, never written
		// 2.3.0 (the 2026-10-25 run): a mod that is installed but could not be read now (a script menu while an MCM page is
		// open or a backup runs). It has NO rows and counts as nothing - its settings are not "changes" and not "the same";
		// the 1,730 "(not read)" rows of the first comparison were counted as changes. Listed apart; a load still sets it.
		std::vector<std::pair<std::string, std::string>> notRead;    // (group, entry)
		int notReadSettings = 0;                                     // the preset's settings in those mods
		std::map<std::string, int> changes;                          // group -> rows that differ
		int same = 0;                                                // settings already at the preset's value
		int Differing() const
		{
			int n = 0;
			for (const auto& [g, c] : changes) { n += c; }
			return n;
		}
	};

	// How a menu record is named in a row. The default is RecordLabel (raw text); the game passes one that shows a script
	// menu's "$KEY" labels in words, through that mod's own translation file, as its page does.
	using RecordLabeler = std::function<std::string(const std::string& a_group, const MenuRecord& a_record)>;

	// a_preset against a_current - the same groups read from the game now. a_installed: the groups found in this game (a
	// group installed but missing from a_current was not read: it goes to notRead, never into the rows or the counts).
	// A setting the preset holds twice (an MCM Helper menu with a visible and a hidden control of one key - TrueHUD) is
	// compared once.
	inline Diff Compare(const Preset& a_preset, const Preset& a_current, const std::set<std::string>& a_installed, const RecordLabeler& a_label = {})
	{
		Diff d;
		std::map<std::string, const FilePart*> curFiles;
		for (const FilePart& f : a_current.files) { curFiles[GroupKey(f)] = &f; }
		for (const FilePart& f : a_preset.files)
		{
			const std::string key = GroupKey(f);
			const std::string entry = f.entry.empty() ? f.id : f.entry;
			if (!a_installed.contains(key))
			{
				d.notInstalled.emplace_back(key, entry);
				continue;
			}
			const auto cur = curFiles.find(key);
			std::set<std::string> seen;
			int settings = 0;
			for (const Value& v : f.values)
			{
				const std::string id = ValueIdentity(v);
				if (!seen.insert(id).second) { continue; }
				++settings;
				if (cur == curFiles.end()) { continue; }
				DiffRow row{ key, entry, ValueLabel(v), {}, v.value, 0 };
				const auto it = std::find_if(cur->second->values.begin(), cur->second->values.end(), [&](const Value& x) { return ValueIdentity(x) == id; });
				if (it == cur->second->values.end()) { row.state = 1; }
				else if (it->value == v.value) { ++d.same; continue; }
				else { row.current = it->value; }
				if (row.state == 0) { ++d.changes[key]; }
				d.rows.push_back(std::move(row));
			}
			if (cur == curFiles.end() && settings > 0)
			{
				d.notRead.emplace_back(key, entry);
				d.notReadSettings += settings;
			}
		}
		for (const auto& [key, m] : a_preset.menus)
		{
			if (m.settings.empty()) { continue; }
			const std::string entry = m.entry.empty() ? key : m.entry;
			if (!a_installed.contains(key))
			{
				d.notInstalled.emplace_back(key, entry);
				continue;
			}
			const auto cur = a_current.menus.find(key);
			std::set<std::string> seen;
			int settings = 0;
			for (const MenuRecord& r : m.settings)
			{
				const std::string id = RecordIdentity(r);
				if (!seen.insert(id).second) { continue; }
				++settings;
				if (cur == a_current.menus.end()) { continue; }
				DiffRow row{ key, entry, a_label ? a_label(key, r) : RecordLabel(r), {}, r.value, 0 };
				const auto it = std::find_if(cur->second.settings.begin(), cur->second.settings.end(), [&](const MenuRecord& x) { return RecordIdentity(x) == id; });
				if (it == cur->second.settings.end()) { row.state = 1; }
				else if (it->value == r.value) { ++d.same; continue; }
				else { row.current = it->value; }
				if (row.state == 0) { ++d.changes[key]; }
				d.rows.push_back(std::move(row));
			}
			if (cur == a_current.menus.end() && settings > 0)
			{
				d.notRead.emplace_back(key, entry);
				d.notReadSettings += settings;
			}
		}
		std::stable_sort(d.rows.begin(), d.rows.end(), [](const DiffRow& a, const DiffRow& b) { return detail::Lower(a.entry) < detail::Lower(b.entry); });
		std::stable_sort(d.notInstalled.begin(), d.notInstalled.end(), [](const auto& a, const auto& b) { return detail::Lower(a.second) < detail::Lower(b.second); });
		std::stable_sort(d.notRead.begin(), d.notRead.end(), [](const auto& a, const auto& b) { return detail::Lower(a.second) < detail::Lower(b.second); });
		return d;
	}

	// ------------------------------------------------------------------------------------------- names

	// A preset name made safe as a file name; never empty (Remembered settings' SafeName - the same files).
	inline std::string SafeName(const std::string& a_name)
	{
		std::string out;
		for (const char c : a_name)
		{
			if (std::string_view("<>:\"/\\|?*").find(c) == std::string_view::npos && static_cast<unsigned char>(c) >= 0x20) { out += c; }
		}
		while (!out.empty() && (out.back() == ' ' || out.back() == '.')) { out.pop_back(); }
		while (!out.empty() && out.front() == ' ') { out.erase(out.begin()); }
		return out.empty() ? std::string("Default") : out;
	}

	// The backup taken before a load: "Backup 2026-10-25 01-30-12" (a file name cannot carry ':').
	inline std::string BackupName(const std::tm& a_t)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "Backup %04d-%02d-%02d %02d-%02d-%02d", a_t.tm_year + 1900, a_t.tm_mon + 1, a_t.tm_mday, a_t.tm_hour, a_t.tm_min, a_t.tm_sec);
		return buf;
	}
	inline std::string SavedStamp(const std::tm& a_t)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", a_t.tm_year + 1900, a_t.tm_mon + 1, a_t.tm_mday, a_t.tm_hour, a_t.tm_min);
		return buf;
	}

	// Which presets the list shows: the user's (writable) and the shipped ones (read only), each once by name; a user preset
	// and a shipped one of the same name are both listed.
	struct Listed
	{
		std::string name;
		bool shipped = false;
	};
	inline std::vector<Listed> MergeLists(std::vector<std::string> a_user, std::vector<std::string> a_shipped)
	{
		std::vector<Listed> out;
		std::sort(a_user.begin(), a_user.end(), [](const auto& a, const auto& b) { return detail::Lower(a) < detail::Lower(b); });
		std::sort(a_shipped.begin(), a_shipped.end(), [](const auto& a, const auto& b) { return detail::Lower(a) < detail::Lower(b); });
		a_user.erase(std::unique(a_user.begin(), a_user.end()), a_user.end());
		a_shipped.erase(std::unique(a_shipped.begin(), a_shipped.end()), a_shipped.end());
		for (const auto& n : a_shipped) { out.push_back({ n, true }); }
		for (const auto& n : a_user) { out.push_back({ n, false }); }
		return out;
	}
}
