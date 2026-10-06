#include "McmMemory.h"

#include "McmLoader.h"
#include "McmScripts.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

// See McmMemory.h. The menus are driven by the MCM loader's own paths (McmScripts' call queue, McmLoader's Apply); this
// file keeps the profile, decides what runs when, and runs one menu at a time.

namespace mcmmemory
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;
		using Clock = std::chrono::steady_clock;
		using strings::TR;
		namespace scripts = mcmloader::scripts;

		constexpr const char* kDir = "Data/SKSE/Plugins/ApocryphaMenuFramework/McmMemory";

		struct MenuData
		{
			std::string entry;
			bool autoRestore = true;
			std::vector<Record> records;  // in the order each setting was first changed
		};

		std::mutex g_mutex;
		std::map<std::string, MenuData> g_menus;  // the active profile: import key -> its settings
		std::string g_profile;
		bool g_loaded = false;
		std::string g_lastResult;
		std::set<std::string> g_restoredThisGame;  // automatic restore: menus already done after this new game

		std::atomic<bool> g_busy{ false };
		std::atomic<long long> g_busySinceMs{ 0 };
		std::atomic<unsigned> g_gameGeneration{ 0 };  // +1 per new game / loaded save: a waiting automatic pass stops

		long long NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
		}

		const char* TypeName(Type a_t)
		{
			switch (a_t)
			{
			case Type::kToggle: return "toggle";
			case Type::kSlider: return "slider";
			case Type::kMenu: return "menu";
			case Type::kColor: return "color";
			case Type::kKeymap: return "keymap";
			case Type::kInput: return "input";
			default: return "helper";
			}
		}

		Type ParseType(const std::string& a_s)
		{
			static const std::map<std::string, Type> types{ { "toggle", Type::kToggle }, { "slider", Type::kSlider }, { "menu", Type::kMenu },
				{ "color", Type::kColor }, { "keymap", Type::kKeymap }, { "input", Type::kInput }, { "helper", Type::kHelper } };
			const auto it = types.find(a_s);
			return it != types.end() ? it->second : Type::kHelper;
		}

		bool IsScriptKey(const std::string& a_key) { return a_key.rfind("script|", 0) == 0; }

		// The same setting: a script option by page + label + nth + type; an MCM Helper control by its key.
		bool SameSetting(const Record& a, const Record& b)
		{
			if (a.type == Type::kHelper || b.type == Type::kHelper) { return a.type == b.type && a.id == b.id; }
			return a.type == b.type && a.page == b.page && a.text == b.text && a.nth == b.nth;
		}

		// A profile name made safe as a file name; never empty.
		std::string SafeName(const std::string& a_name)
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

		fs::path PathFor(const std::string& a_name) { return fs::path(kDir) / (SafeName(a_name) + ".json"); }

		void LoadLocked()
		{
			g_menus.clear();
			g_profile = SafeName(settings::Get().mcmMemoryProfile);
			g_loaded = true;
			std::ifstream in(PathFor(g_profile), std::ios::binary);
			if (!in) { return; }
			try
			{
				const json file = json::parse(in);
				for (const auto& [key, m] : file.value("menus", json::object()).items())
				{
					MenuData data;
					data.entry = m.value("entry", std::string());
					data.autoRestore = m.value("autoRestore", true);
					for (const auto& r : m.value("settings", json::array()))
					{
						Record rec;
						rec.type = ParseType(r.value("type", std::string()));
						rec.page = r.value("page", std::string());
						rec.text = r.value("text", std::string());
						rec.nth = r.value("nth", 0);
						rec.id = r.value("id", std::string());
						rec.value = r.value("value", std::string());
						rec.menuIndex = r.value("menuIndex", -1);
						data.records.push_back(std::move(rec));
					}
					g_menus[key] = std::move(data);
				}
				logger::info("MCM memory: profile \"{}\" read - {} menu(s)", g_profile, g_menus.size());
			}
			catch (const std::exception& e)
			{
				logger::error("MCM memory: profile \"{}\" could not be read ({}) - it starts empty; the file is left as it is", g_profile, e.what());
				g_menus.clear();
			}
		}

		void EnsureLoadedLocked()
		{
			if (!g_loaded || g_profile != SafeName(settings::Get().mcmMemoryProfile)) { LoadLocked(); }
		}

		// Written to a .tmp beside it and moved over, so a crash mid-write never leaves half a profile.
		void SaveLocked()
		{
			json file;
			file["profile"] = g_profile;
			file["menus"] = json::object();
			for (const auto& [key, m] : g_menus)
			{
				json list = json::array();
				for (const Record& r : m.records)
				{
					json j{ { "type", TypeName(r.type) }, { "value", r.value } };
					if (r.type == Type::kHelper) { j["id"] = r.id; }
					else
					{
						j["page"] = r.page;
						j["text"] = r.text;
						j["nth"] = r.nth;
						if (r.type == Type::kMenu) { j["menuIndex"] = r.menuIndex; }
					}
					list.push_back(std::move(j));
				}
				file["menus"][key] = { { "entry", m.entry }, { "autoRestore", m.autoRestore }, { "settings", std::move(list) } };
			}
			std::error_code ec;
			fs::create_directories(kDir, ec);
			const fs::path path = PathFor(g_profile);
			fs::path tmp = path;
			tmp += ".tmp";
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out)
				{
					logger::error("MCM memory: {} could not be written", tmp.string());
					return;
				}
				out << file.dump(1, '\t');
			}
			fs::rename(tmp, path, ec);
			if (ec) { logger::error("MCM memory: {} could not replace {} ({})", tmp.string(), path.string(), ec.message()); }
		}

		// Merge records into a menu: a setting already there takes the new value in its place, a new one goes last.
		int MergeLocked(const std::string& a_key, const std::string& a_entry, const std::vector<Record>& a_records)
		{
			MenuData& m = g_menus[a_key];
			if (!a_entry.empty()) { m.entry = a_entry; }
			int changed = 0;
			for (const Record& r : a_records)
			{
				const auto it = std::find_if(m.records.begin(), m.records.end(), [&](const Record& x) { return SameSetting(x, r); });
				if (it != m.records.end())
				{
					if (it->value != r.value || it->menuIndex != r.menuIndex) { ++changed; }
					*it = r;
				}
				else
				{
					m.records.push_back(r);
					++changed;
				}
			}
			return changed;
		}

		// ------------------------------------------------------------------------------ one menu at a time

		// Every menu AMF can read in this game: script menus found, MCM Helper menus with save-kept values.
		std::vector<std::pair<std::string, std::string>> PresentMenus()
		{
			auto out = scripts::MemoryMenus();
			for (const auto& m : mcmloader::MemoryMenus()) { out.emplace_back(m.key, m.entry); }
			return out;
		}

		struct Job
		{
			bool restore = false;
			std::vector<std::pair<std::string, std::string>> targets;  // (key, entry)
			std::size_t next = 0;
			int menus = 0;
			int settings = 0;
			int missing = 0;
			int skipped = 0;  // menus that could not be driven (one was open on the page, or it went away)
		};

		void SetResult(const std::string& a_text)
		{
			std::scoped_lock lock(g_mutex);
			g_lastResult = a_text;
		}

		void Finish(const std::shared_ptr<Job>& a_job)
		{
			char line[256];
			if (a_job->restore)
			{
				snprintf(line, sizeof(line), TR("AMF_McmMemRestored", "Restored: %d settings set in %d menus (%d not found)."),
					a_job->settings, a_job->menus, a_job->missing);
			}
			else
			{
				snprintf(line, sizeof(line), TR("AMF_McmMemBackedUp", "Backed up: %d settings from %d menus."), a_job->settings, a_job->menus);
			}
			SetResult(line);
			logger::info("MCM memory: {} - {} menu(s), {} setting(s), {} not found, {} skipped", a_job->restore ? "restore" : "backup",
				a_job->menus, a_job->settings, a_job->missing, a_job->skipped);
			g_busy = false;
		}

		void Step(std::shared_ptr<Job> a_job);

		// Any thread: the next menu (a callback from the menu just done arrives on a VM thread or the main thread).
		void Continue(std::shared_ptr<Job> a_job)
		{
			if (const auto tasks = SKSE::GetTaskInterface()) { tasks->AddTask([a_job]() { Step(a_job); }); }
			else { Finish(a_job); }
		}

		void Step(std::shared_ptr<Job> a_job)
		{
			if (a_job->next >= a_job->targets.size())
			{
				Finish(a_job);
				return;
			}
			const auto [key, entry] = a_job->targets[a_job->next++];

			if (!a_job->restore)
			{
				if (IsScriptKey(key))
				{
					const bool started = scripts::MemorySnapshot(key, [a_job, key, entry](std::vector<scripts::MemoryOption> a_read) {
						std::vector<Record> records;
						for (const auto& o : a_read)
						{
							Record r;
							r.type = static_cast<Type>(o.type);
							r.page = o.page;
							r.text = o.text;
							r.nth = o.nth;
							r.value = o.value;
							r.menuIndex = o.menuIndex;
							records.push_back(std::move(r));
						}
						{
							std::scoped_lock lock(g_mutex);
							EnsureLoadedLocked();
							MergeLocked(key, entry, records);
							SaveLocked();
						}
						++a_job->menus;
						a_job->settings += static_cast<int>(records.size());
						Continue(a_job);
					});
					if (!started)
					{
						++a_job->skipped;
						Continue(a_job);
					}
					return;
				}
				mcmloader::MemorySnapshot(key, [a_job, key, entry](std::vector<std::pair<std::string, std::string>> a_values) {
					std::vector<Record> records;
					for (auto& [id, value] : a_values)
					{
						Record r;
						r.type = Type::kHelper;
						r.id = id;
						r.value = value;
						records.push_back(std::move(r));
					}
					if (!records.empty())
					{
						std::scoped_lock lock(g_mutex);
						EnsureLoadedLocked();
						MergeLocked(key, entry, records);
						SaveLocked();
						++a_job->menus;
					}
					a_job->settings += static_cast<int>(records.size());
					Continue(a_job);
				});
				return;
			}

			// restore
			std::vector<Record> records;
			{
				std::scoped_lock lock(g_mutex);
				EnsureLoadedLocked();
				const auto it = g_menus.find(key);
				if (it != g_menus.end()) { records = it->second.records; }
				g_restoredThisGame.insert(key);
			}
			if (records.empty())
			{
				Continue(a_job);
				return;
			}
			auto done = [a_job](int a_applied, int a_missing) {
				++a_job->menus;
				a_job->settings += a_applied;
				a_job->missing += a_missing;
				Continue(a_job);
			};
			if (IsScriptKey(key))
			{
				std::vector<scripts::MemoryOption> options;
				for (const Record& r : records)
				{
					options.push_back({ static_cast<int>(r.type), r.page, r.text, r.nth, r.value, r.menuIndex });
				}
				if (!scripts::MemoryRestore(key, std::move(options), done))
				{
					std::scoped_lock lock(g_mutex);
					g_restoredThisGame.erase(key);  // try again on the next automatic pass
					++a_job->skipped;
					Continue(a_job);
				}
				return;
			}
			std::vector<std::pair<std::string, std::string>> values;
			for (const Record& r : records) { values.emplace_back(r.id, r.value); }
			mcmloader::MemoryRestore(key, std::move(values), done);
		}

		bool Start(bool a_restore, std::vector<std::pair<std::string, std::string>> a_targets)
		{
			if (Busy() || !scripts::MemoryIdle()) { return false; }
			g_busy = true;
			g_busySinceMs = NowMs();
			auto job = std::make_shared<Job>();
			job->restore = a_restore;
			job->targets = std::move(a_targets);
			SetResult(a_restore ? TR("AMF_McmMemRestoring", "Restoring...") : TR("AMF_McmMemBackingUp", "Backing up..."));
			logger::info("MCM memory: {} started for {} menu(s)", a_restore ? "restore" : "backup", job->targets.size());
			Continue(job);
			return true;
		}

		// The menus a restore covers: saved in the profile, present in this game, and (automatic) switched to restore and
		// not done yet after this new game.
		std::vector<std::pair<std::string, std::string>> RestoreTargets(const std::vector<std::string>& a_keys, bool a_automatic)
		{
			std::vector<std::pair<std::string, std::string>> out;
			const auto present = PresentMenus();
			std::scoped_lock lock(g_mutex);
			EnsureLoadedLocked();
			for (const auto& [key, m] : g_menus)
			{
				if (m.records.empty()) { continue; }
				if (!a_keys.empty() && std::find(a_keys.begin(), a_keys.end(), key) == a_keys.end()) { continue; }
				if (a_automatic && (!m.autoRestore || g_restoredThisGame.contains(key))) { continue; }
				const bool here = std::any_of(present.begin(), present.end(), [&](const auto& p) { return p.first == key; });
				if (here) { out.emplace_back(key, m.entry); }
			}
			return out;
		}
	}

	// ------------------------------------------------------------------------------------------- the API

	void Init()
	{
		std::scoped_lock lock(g_mutex);
		LoadLocked();
	}

	void OnNewGame()
	{
		const unsigned generation = ++g_gameGeneration;
		{
			std::scoped_lock lock(g_mutex);
			g_restoredThisGame.clear();
		}
		g_busy = false;  // a drive of the game just left never finishes
		if (!settings::Get().mcmRestoreOnNewGame) { return; }
		logger::info("MCM memory: new game - the profile is applied again once the menus appear (40 s to 4 min)");
		std::thread([generation]() {
			// the menus set themselves up over the first minutes of a new game (discovery looks at 2 s ... 8 min)
			std::this_thread::sleep_for(std::chrono::seconds(40));
			for (int pass = 0; pass < 12; ++pass)
			{
				if (g_gameGeneration.load() != generation || !settings::Get().mcmRestoreOnNewGame) { return; }
				if (const auto tasks = SKSE::GetTaskInterface())
				{
					tasks->AddTask([generation]() {
						if (g_gameGeneration.load() != generation) { return; }
						auto targets = RestoreTargets({}, true);
						if (!targets.empty()) { Start(true, std::move(targets)); }
					});
				}
				std::this_thread::sleep_for(std::chrono::seconds(20));
			}
		}).detach();
	}

	void OnSaveLoaded()
	{
		++g_gameGeneration;  // a loaded save holds its own settings: no automatic restore
		g_busy = false;
	}

	void Remember(const std::string& a_key, const std::string& a_entry, const Record& a_record)
	{
		if (!settings::Get().mcmAutoBackup) { return; }
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		if (MergeLocked(a_key, a_entry, { a_record }) > 0)
		{
			SaveLocked();
			logger::debug("MCM memory: {} - {} \"{}\" = {}", a_entry, TypeName(a_record.type),
				a_record.type == Type::kHelper ? a_record.id : a_record.text, a_record.value);
		}
	}

	std::vector<MenuRow> Menus()
	{
		const auto present = PresentMenus();
		std::vector<MenuRow> rows;
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		for (const auto& [key, m] : g_menus)
		{
			const bool here = std::any_of(present.begin(), present.end(), [&](const auto& p) { return p.first == key; });
			rows.push_back({ key, m.entry, static_cast<int>(m.records.size()), m.autoRestore, here });
		}
		for (const auto& [key, entry] : present)
		{
			if (!g_menus.contains(key)) { rows.push_back({ key, entry, 0, true, true }); }
		}
		std::sort(rows.begin(), rows.end(), [](const MenuRow& a, const MenuRow& b) { return a.entry < b.entry; });
		return rows;
	}

	void SetAutoRestore(const std::string& a_key, bool a_on)
	{
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		const auto it = g_menus.find(a_key);
		if (it == g_menus.end()) { return; }
		it->second.autoRestore = a_on;
		SaveLocked();
	}

	void Forget(const std::string& a_key)
	{
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		if (g_menus.erase(a_key) > 0)
		{
			SaveLocked();
			logger::info("MCM memory: {} forgotten in profile \"{}\"", a_key, g_profile);
		}
	}

	std::vector<std::string> Profiles()
	{
		std::vector<std::string> out;
		std::error_code ec;
		for (const auto& file : fs::directory_iterator(kDir, ec))
		{
			if (file.path().extension() == ".json") { out.push_back(file.path().stem().string()); }
		}
		const std::string active = SafeName(settings::Get().mcmMemoryProfile);
		if (std::find(out.begin(), out.end(), active) == out.end()) { out.push_back(active); }
		std::sort(out.begin(), out.end());
		return out;
	}

	std::string ActiveProfile()
	{
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		return g_profile;
	}

	bool SwitchProfile(const std::string& a_name)
	{
		if (Busy()) { return false; }
		std::scoped_lock lock(g_mutex);
		settings::Get().mcmMemoryProfile = SafeName(a_name);
		settings::Save();
		LoadLocked();
		logger::info("MCM memory: profile \"{}\" in use", g_profile);
		return true;
	}

	bool CreateProfile(const std::string& a_name, bool a_copyActive)
	{
		if (Busy()) { return false; }
		const std::string name = SafeName(a_name);
		std::error_code ec;
		if (fs::exists(PathFor(name), ec)) { return false; }
		std::scoped_lock lock(g_mutex);
		EnsureLoadedLocked();
		auto copied = a_copyActive ? g_menus : std::map<std::string, MenuData>{};
		settings::Get().mcmMemoryProfile = name;
		settings::Save();
		g_profile = name;
		g_menus = std::move(copied);
		SaveLocked();
		logger::info("MCM memory: profile \"{}\" made{}", name, a_copyActive ? " as a copy" : "");
		return true;
	}

	bool DeleteProfile(const std::string& a_name)
	{
		const std::string name = SafeName(a_name);
		if (name == SafeName(settings::Get().mcmMemoryProfile)) { return false; }
		std::error_code ec;
		const bool removed = fs::remove(PathFor(name), ec);
		if (removed) { logger::info("MCM memory: profile \"{}\" deleted", name); }
		return removed;
	}

	bool BackUpAll(const std::vector<std::string>& a_keys)
	{
		auto targets = PresentMenus();
		if (!a_keys.empty())
		{
			std::erase_if(targets, [&](const auto& t) { return std::find(a_keys.begin(), a_keys.end(), t.first) == a_keys.end(); });
		}
		return Start(false, std::move(targets));
	}

	bool RestoreNow(const std::vector<std::string>& a_keys)
	{
		return Start(true, RestoreTargets(a_keys, false));
	}

	bool Busy()
	{
		// a drive whose game was left, or whose callback never came, does not hold the memory for good
		if (g_busy.load() && NowMs() - g_busySinceMs.load() > 5 * 60 * 1000)
		{
			logger::warn("MCM memory: a backup or restore has run for 5 minutes - no longer waiting for it");
			g_busy = false;
		}
		return g_busy.load();
	}

	std::string LastResult()
	{
		std::scoped_lock lock(g_mutex);
		return g_lastResult;
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		json args;
		try { args = json::parse(a_argsJson.empty() ? "{}" : a_argsJson); }
		catch (...) { return R"({"ok":false,"error":"arguments are not JSON"})"; }
		auto str = [&](const char* k) { const auto it = args.find(k); return it != args.end() && it->is_string() ? it->get<std::string>() : std::string(); };
		auto keys = [&]() {
			std::vector<std::string> out;
			if (const auto it = args.find("keys"); it != args.end() && it->is_array())
			{
				for (const auto& k : *it) { if (k.is_string()) { out.push_back(k.get<std::string>()); } }
			}
			if (!str("key").empty()) { out.push_back(str("key")); }
			return out;
		};
		const std::string action = str("action").empty() ? std::string("status") : str("action");

		if (action == "backup" || action == "restore")
		{
			const bool started = action == "backup" ? BackUpAll(keys()) : RestoreNow(keys());
			return json{ { "ok", started }, { "started", started }, { "busy", Busy() }, { "idle", scripts::MemoryIdle() } }.dump();
		}
		if (action == "records")
		{
			std::scoped_lock lock(g_mutex);
			EnsureLoadedLocked();
			const auto it = g_menus.find(str("key"));
			if (it == g_menus.end()) { return json{ { "ok", false }, { "error", "no saved settings for '" + str("key") + "'" } }.dump(); }
			json list = json::array();
			for (const Record& r : it->second.records)
			{
				list.push_back({ { "type", TypeName(r.type) }, { "page", r.page }, { "text", r.text }, { "nth", r.nth }, { "id", r.id },
					{ "value", r.value }, { "menuIndex", r.menuIndex } });
			}
			return json{ { "ok", true }, { "key", it->first }, { "entry", it->second.entry }, { "autoRestore", it->second.autoRestore }, { "settings", list } }.dump();
		}
		if (action == "forget")
		{
			Forget(str("key"));
			return R"({"ok":true})";
		}
		if (action == "auto")
		{
			const auto on = args.find("on");
			if (on == args.end() || !on->is_boolean()) { return R"({"ok":false,"error":"auto needs key and on"})"; }
			SetAutoRestore(str("key"), on->get<bool>());
			return R"({"ok":true})";
		}
		if (action == "profile") { return json{ { "ok", SwitchProfile(str("name")) }, { "profile", ActiveProfile() } }.dump(); }
		if (action == "create")
		{
			const auto copy = args.find("copy");
			const bool made = CreateProfile(str("name"), copy != args.end() && copy->is_boolean() && copy->get<bool>());
			return json{ { "ok", made }, { "profile", ActiveProfile() } }.dump();
		}
		if (action == "delete") { return json{ { "ok", DeleteProfile(str("name")) } }.dump(); }
		if (action == "newgame")  // the automatic restore, as after kNewGame (a test drive without starting a new game)
		{
			OnNewGame();
			return R"({"ok":true,"armed":true})";
		}
		if (action != "status") { return R"({"ok":false,"error":"action: status | backup | restore | records | forget | auto | profile | create | delete | newgame"})"; }

		json out{ { "ok", true }, { "profile", ActiveProfile() }, { "profiles", Profiles() }, { "autoBackup", settings::Get().mcmAutoBackup },
			{ "restoreOnNewGame", settings::Get().mcmRestoreOnNewGame }, { "busy", Busy() }, { "idle", scripts::MemoryIdle() }, { "last", LastResult() } };
		out["menus"] = json::array();
		for (const auto& row : Menus())
		{
			out["menus"].push_back({ { "key", row.key }, { "entry", row.entry }, { "saved", row.saved }, { "autoRestore", row.autoRestore },
				{ "present", row.present } });
		}
		return out.dump();
	}
}
