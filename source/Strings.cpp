#include "Strings.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <windows.h>

// Declared by the renderer: flips its "rebuild the font atlas before the next frame" flag.
namespace renderer { void RequestFontRebuild(); }

namespace strings
{
	namespace
	{
		std::mutex g_lock;
		std::unordered_map<std::string, std::string> g_texts;   // key (without '$') -> UTF-8 text
		std::string g_language = "english";
		std::string g_allText;

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::filesystem::path TranslationsDir()
		{
			return std::filesystem::current_path() / "Data" / "Interface" / "Translations";
		}

		std::filesystem::path FileFor(const std::string& a_language)
		{
			return TranslationsDir() / ("ApocryphaMenuFramework_" + a_language + ".txt");
		}

		std::string GameLanguage()
		{
			std::string lang = "english";
			if (auto* ini = RE::INISettingCollection::GetSingleton())
			{
				if (auto* setting = ini->GetSetting("sLanguage:General"); setting && setting->GetString() && setting->GetString()[0])
				{
					lang = setting->GetString();
				}
			}
			return Lower(lang);
		}

		std::string ToUtf8(const std::wstring& a_w)
		{
			if (a_w.empty()) { return {}; }
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// The SkyUI/SKSE translation file: UTF-16LE with BOM, "$key<TAB>text" per line. Lines
		// without '$' or a tab are comments. "\n" inside a text is an actual line break.
		int ReadInto(const std::filesystem::path& a_path, std::unordered_map<std::string, std::string>& a_out, bool a_overwrite)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) { return -1; }
			std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != 0xFF || static_cast<unsigned char>(bytes[1]) != 0xFE) { return -2; }
			std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data() + 2), (bytes.size() - 2) / 2);
			int added = 0;
			std::size_t pos = 0;
			while (pos < text.size())
			{
				auto eol = text.find(L'\n', pos);
				if (eol == std::wstring::npos) { eol = text.size(); }
				std::wstring line = text.substr(pos, eol - pos);
				pos = eol + 1;
				if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
				if (line.empty() || line[0] != L'$') { continue; }
				const auto tab = line.find(L'\t');
				if (tab == std::wstring::npos) { continue; }
				std::string key = ToUtf8(line.substr(1, tab - 1));
				std::wstring value = line.substr(tab + 1);
				for (std::size_t i = 0; i + 1 < value.size(); ++i)
				{
					if (value[i] == L'\\' && value[i + 1] == L'n') { value.replace(i, 2, L"\n"); }
				}
				if (!a_overwrite && a_out.count(key)) { continue; }
				a_out[key] = ToUtf8(value);
				++added;
			}
			return added;
		}
	}

	void Load()
	{
		const std::string override = Lower(settings::Get().language);
		const std::string lang = override.empty() || override == "auto" ? GameLanguage() : override;

		std::unordered_map<std::string, std::string> texts;
		const int fromLang = ReadInto(FileFor(lang), texts, true);
		int fromEnglish = 0;
		if (lang != "english") { fromEnglish = ReadInto(FileFor("english"), texts, false); }

		std::string all;
		for (const auto& [k, v] : texts) { all += v; all += '\n'; }

		{
			std::scoped_lock l(g_lock);
			g_texts = std::move(texts);
			g_language = lang;
			g_allText = std::move(all);
		}

		if (fromLang < 0)
		{
			logger::info("strings: no translation file for \"{}\" ({}); {}", lang, FileFor(lang).filename().string(),
						 fromEnglish > 0 ? "the English file is used" : "the compiled English text is used");
		}
		else if (fromLang == -2)
		{
			logger::warn("strings: {} is not UTF-16LE with a BOM - the SKSE translation format - and was ignored", FileFor(lang).filename().string());
		}
		else
		{
			logger::info("strings: {} text(s) read for \"{}\"{} ({})", fromLang, lang,
						 fromEnglish > 0 ? std::format(", {} filled from English", fromEnglish) : "",
						 override.empty() ? "the game's language" : "the INI's sLanguage");
		}
	}

	const char* TR(const char* a_key, const char* a_english)
	{
		std::scoped_lock l(g_lock);
		const auto it = g_texts.find(a_key);
		return it == g_texts.end() ? a_english : it->second.c_str();
	}

	const std::string& Language()
	{
		return g_language;
	}

	std::vector<std::string> Available()
	{
		std::vector<std::string> out;
		std::error_code ec;
		const auto dir = TranslationsDir();
		if (std::filesystem::is_directory(dir, ec))
		{
			for (const auto& e : std::filesystem::directory_iterator(dir, ec))
			{
				if (!e.is_regular_file(ec)) { continue; }
				const std::string name = Lower(e.path().filename().string());
				constexpr const char* prefix = "apocryphamenuframework_";
				if (name.rfind(prefix, 0) != 0 || name.size() <= std::strlen(prefix) + 4 || name.substr(name.size() - 4) != ".txt") { continue; }
				out.push_back(name.substr(std::strlen(prefix), name.size() - std::strlen(prefix) - 4));
			}
		}
		std::sort(out.begin(), out.end());
		out.erase(std::unique(out.begin(), out.end()), out.end());
		const auto en = std::find(out.begin(), out.end(), "english");
		if (en != out.end()) { out.erase(en); }
		out.insert(out.begin(), "english");
		return out;
	}

	void SetLanguage(const std::string& a_language)
	{
		settings::Get().language = Lower(a_language) == "auto" ? "" : Lower(a_language);
		settings::Save();
		Load();
		renderer::RequestFontRebuild();
		logger::info("strings: language set to \"{}\" -> showing \"{}\"", a_language, g_language);
	}

	const std::string& AllText()
	{
		return g_allText;
	}
}
