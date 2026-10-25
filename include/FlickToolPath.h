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
// A group without "::" gives exactly the 2.2.x entry and page, so nothing a player or a saved list order already holds
// changes name. Empty segments ("A::::B", a trailing "::") are dropped, and segments are trimmed.
// ============================================================================================

#include <string>
#include <string_view>
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

	inline Place Of(std::string_view a_group, std::string_view a_name)
	{
		Place out;
		const std::string page = a_name.empty() ? std::string("Settings") : std::string(a_name);
		if (a_group.find("::") == std::string_view::npos)
		{
			// the 2.2.x shape, byte for byte (no trimming either, so a saved name stays the same)
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
}
