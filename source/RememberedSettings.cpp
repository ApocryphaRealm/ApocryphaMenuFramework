#include "RememberedSettings.h"

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

// See RememberedSettings.h. The menus are driven by the MCM loader's own paths (McmScripts' call queue, McmLoader's Apply); this
// file keeps the profile, decides what runs when, and runs one menu at a time.

namespace rememberedsettings
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;
		using Clock = std::chrono::steady_clock;
		using strings::TR;
		namespace scripts = mcmloader::scripts;

		constexpr const char* kDir = "Data/SKSE/Plugins/ApocryphaMenuFramework/RememberedSettings";

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
			g_profile = SafeName(settings::Get().rememberedProfile);
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
				logger::info("Remembered settings: profile \"{}\" read - {} menu(s)", g_profile, g_menus.size());
			}
			catch (const std::exception& e)
			{
				logger::error("Remembered settings: profile \"{}\" could not be read ({}) - it starts empty; the file is left as it is", g_profile, e.what());
				g_menus.clear();
			}
		}

		void EnsureLoadedLocked()
		{
			if (!g_loaded || g_profile != SafeName(settings::Get().rememberedProfile)) { LoadLocked(); }
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
					logger::error("Remembered settings: {} could not be written", tmp.string());
					return;
				}
				out << file.dump(1, '\t');
			}
			fs::rename(tmp, path, ec);
			if (ec) { logger::error("Remembered settings: {} could not replace {} ({})", tmp.string(), path.string(), ec.message()); }
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
			logger::info("Remembered settings: {} - {} menu(s), {} setting(s), {} not found, {} skipped", a_job->restore ? "restore" : "backup",
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
			logger::info("Remembered settings: {} started for {} menu(s)", a_restore ? "restore" : "backup", job->targets.size());
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
		logger::info("Remembered settings: new game - the profile is applied again once the menus appear (40 s to 4 min)");
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
			logger::debug("Remembered settings: {} - {} \"{}\" = {}", a_entry, TypeName(a_record.type),
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
			logger::info("Remembered settings: {} forgotten in profile \"{}\"", a_key, g_profile);
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
		const std::string active = SafeName(settings::Get().rememberedProfile);
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
		settings::Get().rememberedProfile = SafeName(a_name);
		settings::Save();
		LoadLocked();
		logger::info("Remembered settings: profile \"{}\" in use", g_profile);
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
		settings::Get().rememberedProfile = name;
		settings::Save();
		g_profile = name;
		g_menus = std::move(copied);
		SaveLocked();
		logger::info("Remembered settings: profile \"{}\" made{}", name, a_copyActive ? " as a copy" : "");
		return true;
	}

	bool DeleteProfile(const std::string& a_name)
	{
		const std::string name = SafeName(a_name);
		if (name == SafeName(settings::Get().rememberedProfile)) { return false; }
		std::error_code ec;
		const bool removed = fs::remove(PathFor(name), ec);
		if (removed) { logger::info("Remembered settings: profile \"{}\" deleted", name); }
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
			logger::warn("Remembered settings: a backup or restore has run for 5 minutes - no longer waiting for it");
			g_busy = false;
		}
		return g_busy.load();
	}

	std::string LastResult()
	{
		std::scoped_lock lock(g_mutex);
		return g_lastResult;
	}

	// ------------------------------------------------------------------------------ import from MCM Memory (2.1.5)
	// File format read from MCM Memory 1.5.6's own source (github.com/legendman89/MCMMemory @ ceca293, GPL-3.0; only its
	// format is read here, no code taken): Profiles\<name>.json, formatVersion 2 - "settings" (modID "<Script>::<ModName>",
	// pageName, pageIndex, optionIndex, controlType, optionLabel, value, valueText, command, ...), "activations" (an enable
	// switch that must come back first), "pageExclusions"; Settings.json - activeProfile, autoRestore,
	// autoRestoreExcludedMCMs. Text that is not UTF-8 is stored as "\0MCMMemoryBytes:" + hex. Plan and mapping:
	// 4. plans\amf-2.1.5\mcm-memory-move-over-research.md.
	namespace
	{
		constexpr const char* kMcmMemoryDir = "Data/SKSE/Plugins/MCMMemory";

		std::string DecodeMcmMemoryText(const std::string& a_s)
		{
			static const std::string kPrefix = std::string("\0MCMMemoryBytes:", 16);
			if (a_s.rfind(kPrefix, 0) != 0) { return a_s; }
			std::string out;
			for (std::size_t i = kPrefix.size(); i + 1 < a_s.size(); i += 2)
			{
				out += static_cast<char>(std::stoi(a_s.substr(i, 2), nullptr, 16));
			}
			return out;
		}

		json ReadJsonFile(const fs::path& a_path)
		{
			try
			{
				std::ifstream in(a_path, std::ios::binary);
				if (!in) { return json(); }
				return json::parse(in, nullptr, true, true);
			}
			catch (const std::exception& e)
			{
				logger::warn("Remembered settings: import - {} could not be read ({})", a_path.string(), e.what());
				return json();
			}
		}

		std::string JStr(const json& a_j, const char* a_k)
		{
			const auto it = a_j.find(a_k);
			if (it == a_j.end()) { return {}; }
			if (it->is_string()) { return DecodeMcmMemoryText(it->get<std::string>()); }
			if (it->is_boolean()) { return it->get<bool>() ? "true" : "false"; }
			if (it->is_number_integer()) { return std::to_string(it->get<long long>()); }
			if (it->is_number()) { char b[32]; std::snprintf(b, sizeof(b), "%.6g", it->get<double>()); return b; }
			return {};
		}
		int JInt(const json& a_j, const char* a_k, int a_default)
		{
			const auto it = a_j.find(a_k);
			return it != a_j.end() && it->is_number() ? it->get<int>() : a_default;
		}
		bool JBool(const json& a_j, const char* a_k)
		{
			const auto it = a_j.find(a_k);
			return it != a_j.end() && it->is_boolean() && it->get<bool>();
		}
	}

	std::vector<std::string> McmMemoryProfiles()
	{
		std::vector<std::string> out;
		std::error_code ec;
		const fs::path dir = fs::path(kMcmMemoryDir) / "Profiles";
		if (!fs::is_directory(dir, ec)) { return out; }
		for (const auto& f : fs::directory_iterator(dir, ec))
		{
			if (f.path().extension() == ".json") { out.push_back(f.path().stem().string()); }
		}
		const json s = ReadJsonFile(fs::path(kMcmMemoryDir) / "Settings.json");
		const std::string active = s.is_object() ? JStr(s, "activeProfile") : std::string();
		std::sort(out.begin(), out.end(), [&](const std::string& a, const std::string& b) {
			if ((a == active) != (b == active)) { return a == active; }
			return a < b;
		});
		return out;
	}

	bool McmMemoryAutoRestoreOn()
	{
		if (!::GetModuleHandleW(L"MCMMemory.dll")) { return false; }
		const json s = ReadJsonFile(fs::path(kMcmMemoryDir) / "Settings.json");
		const auto it = s.is_object() ? s.find("autoRestore") : s.end();
		return !s.is_object() || it == s.end() || !it->is_boolean() || it->get<bool>();   // missing = its default, on
	}

	std::string ImportFromMcmMemory(const std::string& a_profile)
	{
		const fs::path file = fs::path(kMcmMemoryDir) / "Profiles" / (a_profile + ".json");
		const json profile = ReadJsonFile(file);
		const auto settingsIt = profile.is_object() ? profile.find("settings") : profile.end();
		if (!profile.is_object() || settingsIt == profile.end() || !settingsIt->is_array())
		{
			const std::string msg = TR("AMF_McmImportUnreadable", "That profile could not be read.");
			std::scoped_lock lock(g_mutex);
			g_lastResult = msg;
			return msg;
		}
		const json other = ReadJsonFile(fs::path(kMcmMemoryDir) / "Settings.json");

		// pages the other mod was told not to restore stay out
		std::set<std::string> excludedPages;   // "<modID>\x1f<pageName>"
		if (const auto ex = profile.find("pageExclusions"); ex != profile.end() && ex->is_object())
		{
			for (const auto& [modId, list] : ex->items())
			{
				if (!list.is_array()) { continue; }
				for (const auto& e : list)
				{
					const std::string mode = JStr(e, "mode");
					if (mode == "restore" || mode == "all") { excludedPages.insert(DecodeMcmMemoryText(modId) + "\x1f" + JStr(e, "pageName")); }
				}
			}
		}

		// every saved setting, grouped by menu (modID), in the order the other mod keeps them; activations first
		struct Menu
		{
			std::string modName;
			std::vector<ForeignSetting> settings;
		};
		std::map<std::string, Menu> menus;
		std::vector<std::string> leftOut;   // "<menu>: <row>" for what AMF has no record for
		int leftOutCount = 0;
		const auto take = [&](const json& a_s, bool a_activation) {
			const std::string modId = JStr(a_s, "modID");
			const auto sep = modId.find("::");
			const std::string modName = sep == std::string::npos ? JStr(a_s, "modName") : modId.substr(sep + 2);
			ForeignSetting f;
			f.page = JStr(a_s, "pageName");
			f.label = JStr(a_s, "optionLabel");
			f.kind = a_activation ? std::string("option") : JStr(a_s, "controlType");
			f.value = a_activation ? std::string("true") : JStr(a_s, "value");
			f.valueText = JStr(a_s, "valueText");
			f.optionIndex = JInt(a_s, "optionIndex", -1);
			if (f.kind == "menu") { f.index = JInt(a_s, "value", -1); }
			if (excludedPages.contains(modId + "\x1f" + f.page)) { return; }
			const bool usable = !f.label.empty() && !JBool(a_s, "command") && !JBool(a_s, "textControl") && !JBool(a_s, "startCommand") &&
								(f.kind == "option" || f.kind == "slider" || f.kind == "menu" || f.kind == "color" || f.kind == "keymap" || f.kind == "input");
			if (!usable)
			{
				++leftOutCount;
				const std::string row = !f.label.empty() ? f.label : JStr(a_s, "rowLabel");
				if (leftOut.size() < 40) { leftOut.push_back(modName + ": " + (row.empty() ? f.kind : row)); }
				return;
			}
			Menu& m = menus[modId];
			m.modName = modName;
			m.settings.push_back(f);
		};
		if (const auto act = profile.find("activations"); act != profile.end() && act->is_array())
		{
			for (const auto& a : *act) { take(a, true); }
		}
		for (const auto& s : *settingsIt) { take(s, false); }

		// the other mod's "leave this menu out of the automatic restore"
		std::set<std::string> excludedMenus;
		if (other.is_object())
		{
			if (const auto ex = other.find("autoRestoreExcludedMCMs"); ex != other.end() && ex->is_array())
			{
				for (const auto& e : *ex) { if (e.is_string()) { excludedMenus.insert(DecodeMcmMemoryText(e.get<std::string>())); } }
			}
		}

		// this game's script menus by ModName (the other mod keys a menu by "<Script>::<ModName>"; AMF by plugin + ModName)
		std::map<std::string, std::pair<std::string, std::string>> scriptByName;   // ModName -> (key, entry)
		for (const auto& m : scripts::Menus())
		{
			const auto bar = m.key.rfind('|');
			if (bar != std::string::npos && m.present) { scriptByName.emplace(m.key.substr(bar + 1), std::make_pair(m.key, m.entry)); }
		}

		int imported = 0, importedMenus = 0, keptByHelper = 0, notFound = 0;
		std::vector<std::string> missingMenus;
		{
			std::scoped_lock lock(g_mutex);
			EnsureLoadedLocked();
			for (auto& [modId, menu] : menus)
			{
				std::string key, entry;
				std::vector<Record> records;
				if (const auto it = scriptByName.find(menu.modName); it != scriptByName.end())
				{
					key = it->second.first;
					entry = it->second.second;
					// a script option: page + raw label + type + nth - the n-th row with that label and type on its page, counted
					// in SLOT order; the same slot seen twice (an activation and its setting) keeps one number
					std::vector<const ForeignSetting*> bySlot;
					for (const auto& s : menu.settings) { bySlot.push_back(&s); }
					std::stable_sort(bySlot.begin(), bySlot.end(), [](const auto* a, const auto* b) { return a->optionIndex < b->optionIndex; });
					std::map<const ForeignSetting*, int> nthOf;
					std::map<std::string, std::map<int, int>> slotsSeen;   // page+label+kind -> slot -> nth
					for (const auto* s : bySlot)
					{
						auto& slots = slotsSeen[s->page + "\x1f" + s->label + "\x1f" + s->kind];
						const auto known = slots.find(s->optionIndex);
						if (known != slots.end()) { nthOf[s] = known->second; continue; }
						const int next = static_cast<int>(slots.size());
						slots[s->optionIndex] = next;
						nthOf[s] = next;
					}
					// emitted in the other mod's order: its activations - the switches that turn a menu's rows on - first
					for (const auto& s : menu.settings)
					{
						Record r;
						r.type = s.kind == "option" ? Type::kToggle : s.kind == "slider" ? Type::kSlider : s.kind == "menu" ? Type::kMenu
							   : s.kind == "color" ? Type::kColor : s.kind == "keymap" ? Type::kKeymap : Type::kInput;
						r.page = s.page;
						r.text = s.label;
						r.nth = nthOf[&s];
						if (r.type == Type::kToggle) { r.value = (s.value == "true" || (s.value != "false" && s.value != "0")) ? "1" : "0"; }
						else if (r.type == Type::kMenu) { r.value = s.valueText; r.menuIndex = s.index; }
						else { r.value = s.value; }
						records.push_back(r);
					}
				}
				else
				{
					const mcmloader::HelperImport h = mcmloader::ImportHelperSettings(menu.modName, menu.settings);
					if (h.key.empty())
					{
						missingMenus.push_back(menu.modName);
						continue;
					}
					key = h.key;
					entry = h.entry;
					keptByHelper += h.keptByHelper;
					notFound += h.notFound;
					for (const auto& [id, value] : h.values)
					{
						Record r;
						r.type = Type::kHelper;
						r.id = id;
						r.value = value;
						records.push_back(r);
					}
				}
				if (!records.empty())
				{
					imported += static_cast<int>(records.size());
					++importedMenus;
					MergeLocked(key, entry, records);
				}
				if (excludedMenus.contains(modId) && g_menus.contains(key)) { g_menus[key].autoRestore = false; }
			}
			SaveLocked();
		}

		// the result, one line per part
		char line[512];
		std::snprintf(line, sizeof(line), TR("AMF_McmImportDone", "Imported %d settings from %d menus into the profile \"%s\". Once you are happy, switch MCM Memory off: AMF sets them after a new game."),
					  imported, importedMenus, ActiveProfile().c_str());
		std::string msg = line;
		if (keptByHelper > 0)
		{
			std::snprintf(line, sizeof(line), TR("AMF_McmImportHelperKeeps", "%d settings are kept by MCM Helper itself and need nothing."), keptByHelper);
			msg += std::string("\n") + line;
		}
		if (!missingMenus.empty())
		{
			std::string names;
			for (std::size_t i = 0; i < missingMenus.size() && i < 20; ++i) { names += (i ? ", " : "") + missingMenus[i]; }
			std::snprintf(line, sizeof(line), TR("AMF_McmImportMissing", "%d menus are not in this game, or not switched on here - import again once they are: %s"),
						  static_cast<int>(missingMenus.size()), names.c_str());
			msg += std::string("\n") + line;
		}
		if (leftOutCount + notFound > 0)
		{
			std::string rows;
			for (std::size_t i = 0; i < leftOut.size() && i < 12; ++i) { rows += (i ? "; " : "") + leftOut[i]; }
			std::snprintf(line, sizeof(line), TR("AMF_McmImportLeftOut", "%d settings could not come over (buttons it replays, rows with no name, cycling text, rows no longer there): %s"),
						  leftOutCount + notFound, rows.c_str());
			msg += std::string("\n") + line;
		}
		logger::info("Remembered settings: imported profile \"{}\" - {} setting(s) from {} menu(s), {} kept by MCM Helper, {} menu(s) not here, {} left out, {} not found",
					 a_profile, imported, importedMenus, keptByHelper, missingMenus.size(), leftOutCount, notFound);
		{
			std::scoped_lock lock(g_mutex);
			g_lastResult = msg;
		}
		return msg;
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
		// 2.1.5: import from MCM Memory - mcmmemory (its profiles, whether its automatic restore is on) | import {name}
		if (action == "mcmmemory") { return json{ { "ok", true }, { "profiles", McmMemoryProfiles() }, { "autoRestoreOn", McmMemoryAutoRestoreOn() } }.dump(); }
		if (action == "import")
		{
			const auto names = McmMemoryProfiles();
			const std::string name = !str("name").empty() ? str("name") : (names.empty() ? std::string() : names.front());
			if (name.empty()) { return R"({"ok":false,"error":"no MCM Memory profile found"})"; }
			return json{ { "ok", true }, { "profile", name }, { "result", ImportFromMcmMemory(name) } }.dump();
		}
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
