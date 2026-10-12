#include "Persistence.h"

#include "utils/Logger.h"

#include <ShlObj.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace persistence
{
	namespace
	{
		std::mutex g_lock;
		std::unordered_map<std::string, std::string> g_values;
		std::string g_activeSaveBaseName;  // set by OnLoadGame/OnSaveGame; empty until the first one fires
		std::string g_pendingLoadName;      // set at kPreLoadGame, consumed at kPostLoadGame

		// The game's save directory = Documents\My Games\Skyrim Special Edition\<sLocalSavePath>.
		// sLocalSavePath:General defaults to "Saves\", but MO2's profile-local saves work by
		// setting it to "__MO_Saves\" and mapping the profile's saves folder onto THAT - it does
		// not remap "Saves\". 1.3.1 assumed "Saves\" and its sibling files landed in the real,
		// unmanaged Saves folder while the .ess/.skse went to the profile (observed live
		// 2026-08-28). Reading the setting the game itself uses makes the sibling land next to
		// the save in every configuration.
		std::string SavesDirectory()
		{
			PWSTR path = nullptr;
			std::string result;

			if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path)) && path)
			{
				char narrow[MAX_PATH]{};
				const int written = WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, sizeof(narrow), nullptr, nullptr);
				if (written > 0)
				{
					result = narrow;
					result += "\\My Games\\Skyrim Special Edition\\";

					std::string localSavePath = "Saves\\";
					if (auto* ini = RE::INISettingCollection::GetSingleton())
					{
						if (auto* setting = ini->GetSetting("sLocalSavePath:General"); setting && setting->GetString() && *setting->GetString())
						{
							localSavePath = setting->GetString();
						}
					}
					if (localSavePath.back() != '\\' && localSavePath.back() != '/')
					{
						localSavePath += '\\';
					}
					result += localSavePath;
				}
			}

			if (path)
			{
				CoTaskMemFree(path);
			}

			return result;
		}

		// Strip a trailing ".ess" (SKSE's save-name payload has historically included it in some
		// versions, not others) so the sibling file's base name always matches regardless.
		std::string StripExtension(std::string_view a_name)
		{
			std::string name(a_name);
			if (name.size() > 4 && name.substr(name.size() - 4) == ".ess")
			{
				name.resize(name.size() - 4);
			}
			return name;
		}

		// UTF-8 <-> a path, without C++20's char8_t strings (the log and the JSON take plain UTF-8 std::string)
		std::string U8(const std::filesystem::path& a_path)
		{
			const std::wstring w = a_path.wstring();
			if (w.empty()) { return {}; }
			const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
			std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
			if (n > 0) { WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr); }
			return s;
		}

		std::filesystem::path FromU8(const std::string& a_s)
		{
			if (a_s.empty()) { return {}; }
			const int n = MultiByteToWideChar(CP_UTF8, 0, a_s.c_str(), static_cast<int>(a_s.size()), nullptr, 0);
			std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
			if (n > 0) { MultiByteToWideChar(CP_UTF8, 0, a_s.c_str(), static_cast<int>(a_s.size()), w.data(), n); }
			return std::filesystem::path(w);
		}

		std::string SiblingPath(const std::string& a_baseName)
		{
			return SavesDirectory() + a_baseName + ".amf-state.ini";
		}
	}

	void OnSaveGame(std::string_view a_saveName)
	{
		const std::string baseName = StripExtension(a_saveName);
		if (baseName.empty())
		{
			logger::warn("persistence: OnSaveGame received an empty save name; nothing written");
			return;
		}

		std::unordered_map<std::string, std::string> snapshot;
		{
			std::scoped_lock lock(g_lock);
			g_activeSaveBaseName = baseName;
			snapshot = g_values;
		}

		const std::string path = SiblingPath(baseName);
		std::ofstream file(path, std::ios::trunc);
		if (!file.is_open())
		{
			logger::error("persistence: could not open {} for writing; state for this save was NOT committed", path);
			return;
		}

		file << "; AMF per-save state - mirrors co-save's scoping (decisions doc S10), plain text.\n"
				"; Written on save, read back on load of THIS specific save. Safe to inspect,\n"
				"; unsafe to hand-edit while the game has this save loaded.\n\n";

		for (const auto& [key, value] : snapshot)
		{
			file << key << "=" << value << "\n";
		}

		logger::info("persistence: {} value(s) committed to {}", snapshot.size(), path);
	}

	void OnPreLoadGame(std::string_view a_saveName)
	{
		// kPreLoadGame's payload is the save name (char* + dataLen) - stash it for post-load.
		std::string baseName = StripExtension(a_saveName);
		std::scoped_lock lock(g_lock);
		g_pendingLoadName = std::move(baseName);
	}

	void OnPostLoadGame(bool a_loadSucceeded)
	{
		std::string name;
		{
			std::scoped_lock lock(g_lock);
			name = g_pendingLoadName;
			g_pendingLoadName.clear();
		}

		if (!a_loadSucceeded)
		{
			logger::info("persistence: load reported unsuccessful; state left at defaults");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		if (name.empty())
		{
			// New game, or a load with no preceding kPreLoadGame name - start clean.
			logger::info("persistence: post-load with no captured save name (new game?) - starting empty");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		OnLoadGame(name);
	}

	void OnLoadGame(std::string_view a_saveName)
	{
		const std::string baseName = StripExtension(a_saveName);
		if (baseName.empty())
		{
			logger::warn("persistence: OnLoadGame received an empty save name; state cleared to defaults");
			std::scoped_lock lock(g_lock);
			g_values.clear();
			g_activeSaveBaseName.clear();
			return;
		}

		const std::string path = SiblingPath(baseName);
		std::ifstream file(path);

		std::unordered_map<std::string, std::string> loaded;

		if (!file.is_open())
		{
			logger::info("persistence: no sibling state file for \"{}\" ({}) - this save has none yet, starting empty", baseName, path);
		}
		else
		{
			std::string line;
			while (std::getline(file, line))
			{
				if (line.empty() || line.front() == ';')
				{
					continue;
				}
				const auto equals = line.find('=');
				if (equals == std::string::npos)
				{
					continue;
				}
				loaded[line.substr(0, equals)] = line.substr(equals + 1);
			}
			logger::info("persistence: {} value(s) restored for save \"{}\" from {}", loaded.size(), baseName, path);
		}

		std::scoped_lock lock(g_lock);
		g_values = std::move(loaded);
		g_activeSaveBaseName = baseName;
	}

	void SetValue(const std::string& a_key, const std::string& a_value)
	{
		std::scoped_lock lock(g_lock);
		g_values[a_key] = a_value;
		logger::debug("persistence: SetValue(\"{}\", \"{}\") - staged, committed on next save", a_key, a_value);
	}

	std::string GetValue(const std::string& a_key, const std::string& a_default)
	{
		std::scoped_lock lock(g_lock);
		const auto it = g_values.find(a_key);
		return it != g_values.end() ? it->second : a_default;
	}

	CleanResult CleanOrphanedState(bool a_listOnly)
	{
		namespace fs = std::filesystem;
		static constexpr std::wstring_view kSuffix = L".amf-state.ini";
		CleanResult r;
		r.listOnly = a_listOnly;
		const std::string dirUtf8 = SavesDirectory();
		r.saveFolder = dirUtf8;
		if (dirUtf8.empty())
		{
			r.error = "the save folder could not be worked out (no Documents folder)";
			logger::warn("persistence: orphaned state - {}; nothing done", r.error);
			return r;
		}
		std::error_code ec;
		const fs::path dir = FromU8(dirUtf8);
		if (!fs::is_directory(dir, ec))
		{
			r.error = "the save folder does not exist yet";
			logger::info("persistence: orphaned state - {} ({}); nothing to tidy", r.error, dirUtf8);
			r.ran = true;
			return r;
		}
		std::string active;
		{
			std::scoped_lock lock(g_lock);
			active = g_activeSaveBaseName;
		}
		std::vector<fs::path> orphans;
		for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
		{
			std::error_code fec;
			if (!it->is_regular_file(fec)) { continue; }
			if (_wcsicmp(it->path().extension().c_str(), L".ess") == 0) { ++r.saves; continue; }
			const std::wstring name = it->path().filename().wstring();
			if (name.size() <= kSuffix.size() ||
				_wcsicmp(name.c_str() + (name.size() - kSuffix.size()), kSuffix.data()) != 0)
			{
				continue;
			}
			++r.stateFiles;
			const std::wstring base = name.substr(0, name.size() - kSuffix.size());
			if (fs::exists(dir / (base + L".ess"), fec) || fec)
			{
				continue;   // its save is there (or could not be checked - then it is left alone)
			}
			if (!active.empty() && U8(fs::path(base)) == active)
			{
				continue;   // the save being played right now, about to be written
			}
			orphans.push_back(it->path());
		}
		if (ec)
		{
			r.error = "the save folder could not be read: " + ec.message();
			logger::warn("persistence: orphaned state - {} ({})", r.error, dirUtf8);
			return r;
		}
		if (r.saves == 0)
		{
			// no save at all: most likely not the folder the game saves to (an MO2 profile's local saves, a moved folder) -
			// every state file would look orphaned, so nothing is touched
			r.error = "no saves in the save folder - probably not the folder the game saves to";
			logger::warn("persistence: orphaned state - {} ({}; {} state file(s)); nothing deleted", r.error, dirUtf8, r.stateFiles);
			return r;
		}
		r.ran = true;
		r.orphaned = static_cast<int>(orphans.size());
		if (orphans.empty())
		{
			logger::info("persistence: orphaned state - {} state file(s) and {} save(s) in {}, none without its save", r.stateFiles,
						 r.saves, dirUtf8);
			return r;
		}
		for (const fs::path& p : orphans)
		{
			const std::string shown = U8(p.filename());
			r.files.push_back(shown);
			if (a_listOnly)
			{
				logger::info("persistence: orphaned state (list only): {} - its save is gone", shown);
				continue;
			}
			if (DeleteFileW(p.c_str()))
			{
				++r.deleted;
				logger::info("persistence: orphaned state deleted: {} (its save is gone)", shown);
			}
			else
			{
				++r.failed;
				logger::warn("persistence: orphaned state NOT deleted: {} (error {}) - left where it was", shown, GetLastError());
			}
		}
		logger::info("persistence: orphaned state - {} of {} state file(s) had no save ({} save(s) in {}); {} {}, {} failed",
					 r.orphaned, r.stateFiles, r.saves, dirUtf8, a_listOnly ? "listed" : "deleted", a_listOnly ? r.orphaned : r.deleted,
					 r.failed);
		return r;
	}
}
