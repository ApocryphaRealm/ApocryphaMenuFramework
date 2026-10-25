#pragma once

// ============================================================================================
// WHERE A FLICK PAGE SITS IN THIS MENU (2.3.0) - the part with no game in it, so tools\flick_slots_check.cpp runs the same
// code offline.
//
// A FLICK tool (FlickHost.h's Tool) names a sidebar group and its own name. Up to 2.2.x the menu entry was the group (or
// the name when there is none) + " (FLICK)", and the page the tool's name. FLICK API 6 lets a group nest with "::", up to
// four deep ("Camera::Presets" - the header's own example; Fuzzles' FUCK-DEMO and FUCK-Template use it). The raw "::"
// would show in the list, so (4. plans\amf-flick-v6\PLAN.md 3.3) the FIRST segment is the entry and the rest becomes the
// page's path, as this menu's registry already nests pages with "/" (SKSE Menu Framework's "Presets/Old"):
//   group "Camera::Presets", tool "Orbit"  ->  entry "Camera (FLICK)", page "Presets/Orbit"
//   group "General",         tool "Main"   ->  entry "General (FLICK)", page "Main"          (unchanged from 2.2.x)
//   no group,                tool "Main"   ->  entry "Main (FLICK)",    page "Main"          (unchanged)
// A group without "::" gives the 2.2.x entry and page. Empty segments ("A::::B", a trailing "::") are dropped.
// 2.3.0 (the 2026-10-25 run): blanks at either end of the group, of every segment AND of the tool's name are trimmed in
// both shapes alike - FUCK-DEMO's names began with a tab, trimmed for its nested groups but kept for its top-level pages,
// so it showed as two list entries. (A name with no blank at its ends - every mod seen so far - is unchanged.)
// ============================================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace flick::toolpath
{
	inline constexpr std::string_view kSuffix = " (FLICK)";

	struct Place
	{
		std::string entry;   // the menu entry, with " (FLICK)"
		std::string page;    // the page path inside it ("Presets/Orbit"), never empty
		std::string base;    // the entry without the suffix (the name the one-entry rule compares)
	};

	inline std::string Trim(std::string_view a_s)
	{
		const auto b = a_s.find_first_not_of(" \t");
		if (b == std::string_view::npos) { return {}; }
		const auto e = a_s.find_last_not_of(" \t");
		return std::string(a_s.substr(b, e - b + 1));
	}

	inline Place Of(std::string_view a_rawGroup, std::string_view a_rawName)
	{
		Place out;
		const std::string group = Trim(a_rawGroup);
		const std::string name = Trim(a_rawName);
		const std::string_view a_group = group;
		const std::string_view a_name = name;
		const std::string page = a_name.empty() ? std::string("Settings") : std::string(a_name);
		if (a_group.find("::") == std::string_view::npos)
		{
			// the 2.2.x shape (trimmed, as the nested shape is)
			out.base = a_group.empty() ? std::string(a_name) : std::string(a_group);
			out.entry = out.base + std::string(kSuffix);
			out.page = page;
			return out;
		}
		std::vector<std::string> parts;
		std::size_t at = 0;
		while (at <= a_group.size())
		{
			const auto next = a_group.find("::", at);
			const auto seg = Trim(a_group.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
			if (!seg.empty()) { parts.push_back(seg); }
			if (next == std::string_view::npos) { break; }
			at = next + 2;
		}
		if (parts.empty())
		{
			out.base = a_name.empty() ? std::string("Settings") : std::string(a_name);
			out.entry = out.base + std::string(kSuffix);
			out.page = page;
			return out;
		}
		out.base = parts.front();
		out.entry = out.base + std::string(kSuffix);
		for (std::size_t i = 1; i < parts.size(); ++i) { out.page += parts[i] + "/"; }
		out.page += page;
		return out;
	}

	// One line of a FLICK mod's translation file ("$KEY<tab>text", UTF-8 already, CR removed) into its key and text. False
	// for a line that is not one. 2.3.0 (the 2026-10-25 run): files line their columns up with spaces and several tabs
	// ("$DEMO_TabBasicWidgets \t\tBasic") - the text starts after the run of tabs and spaces, so neither carries the padding.
	// 2.3.0 (combined run 3266, 2026-10-25): FUCK-Template_english.txt separates key and text with SPACES only
	// ("$TEMPL_ToolName              General Settings") and a tab-only split read 0 of its lines, so its entry showed as
	// "$TEMPL_Group_General (FLICK)". As the game's own translation loader does, the key now ends at the FIRST blank - a
	// space or a tab - and the text is everything after that run of blanks, inner spaces and tabs kept.
	inline bool TranslationLine(std::string_view a_line, std::string& a_key, std::string& a_text)
	{
		const auto blank = a_line.find_first_of(" \t");
		if (a_line.empty() || a_line[0] != '$' || blank == std::string_view::npos) { return false; }
		const std::string_view key = a_line.substr(0, blank);
		if (key.size() < 2) { return false; }
		const auto textAt = a_line.find_first_not_of(" \t", blank);
		a_key = std::string(key);
		a_text = textAt == std::string_view::npos ? std::string() : std::string(a_line.substr(textAt));
		return true;
	}

	// A whole translation file's bytes as UTF-8 text: UTF-16 LE after its BOM (the game's format), else UTF-8 with or without
	// a BOM. Surrogate pairs are joined; an unpaired one becomes U+FFFD. No Windows call, so the offline check runs it.
	inline std::string TranslationFileText(std::string_view a_raw)
	{
		if (a_raw.size() >= 2 && static_cast<unsigned char>(a_raw[0]) == 0xFF && static_cast<unsigned char>(a_raw[1]) == 0xFE)
		{
			std::string out;
			out.reserve(a_raw.size());
			auto unit = [&](std::size_t a_at) {
				return static_cast<std::uint32_t>(static_cast<unsigned char>(a_raw[a_at])) |
				       (static_cast<std::uint32_t>(static_cast<unsigned char>(a_raw[a_at + 1])) << 8);
			};
			for (std::size_t i = 2; i + 1 < a_raw.size(); i += 2)
			{
				std::uint32_t cp = unit(i);
				if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < a_raw.size() && unit(i + 2) >= 0xDC00 && unit(i + 2) <= 0xDFFF)
				{
					cp = 0x10000 + ((cp - 0xD800) << 10) + (unit(i + 2) - 0xDC00);
					i += 2;
				}
				else if (cp >= 0xD800 && cp <= 0xDFFF)
				{
					cp = 0xFFFD;
				}
				if (cp < 0x80) { out += static_cast<char>(cp); }
				else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
				else if (cp < 0x10000)
				{
					out += static_cast<char>(0xE0 | (cp >> 12));
					out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				}
				else
				{
					out += static_cast<char>(0xF0 | (cp >> 18));
					out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
					out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
					out += static_cast<char>(0x80 | (cp & 0x3F));
				}
			}
			return out;
		}
		if (a_raw.size() >= 3 && a_raw.substr(0, 3) == "\xEF\xBB\xBF") { return std::string(a_raw.substr(3)); }
		return std::string(a_raw);
	}

	// Every "$KEY text" line of a translation file's bytes, handed to a_take(key, text) in file order (CR removed, blank and
	// non-key lines skipped). Returns how many lines were taken. FlickHost.cpp's reader and tools\flick_slots_check run this.
	template <class F>
	std::size_t ForEachTranslation(std::string_view a_raw, F&& a_take)
	{
		const std::string text = TranslationFileText(a_raw);
		std::size_t pos = 0, count = 0;
		while (pos < text.size())
		{
			std::size_t end = text.find('\n', pos);
			if (end == std::string::npos) { end = text.size(); }
			std::string_view line(text.data() + pos, end - pos);
			pos = end + 1;
			if (!line.empty() && line.back() == '\r') { line.remove_suffix(1); }
			std::string key, value;
			if (!TranslationLine(line, key, value)) { continue; }
			a_take(std::move(key), std::move(value));
			++count;
		}
		return count;
	}
}
