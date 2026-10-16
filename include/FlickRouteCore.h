#pragma once

// ============================================================================================
// WHERE A FLICK MOD GOES - the part with no game in it, so tools\flick_route_check.cpp runs the same code offline (2.2.1).
//
// A FLICK mod (FlickHost.h) is answered by ONE framework for the whole session: this menu's FLICK host, or the real FLICK
// (FUCK.dll). The SMF alias decides it per calling DLL at the moment the mod asks for FUCK.dll (SmfAlias.cpp), because a
// mod hands its pages and windows to the one table it got and cannot be moved afterwards.
//
// THE RULE (the owner agreed, 2026-10-15, after DSAdmin's Nexus report that Racemenu Enhancer's RaceMenu widgets
// vanished in 2.1.6-2.2.0): this menu holds a FLICK mod's settings pages, but it does not draw FLICK's free windows -
// Racemenu Enhancer's RaceWidgetWindow / RaceEquipWindow, which FLICK draws while RaceSex Menu is open, were held here and
// never drawn. So:
//   1. the player's own choice for that mod (Settings > Converted menus > FLICK) always wins - "FLICK's own window" or
//      "This menu";
//   1b. (2026-10-16) with no choice made, a mod build listed in kKnownClashes stays in this menu when exactly the FLICK
//      version it is proven to crash is installed;
//   2. with no choice made, a mod that HAS FLICK WINDOWS goes to the real FLICK when FLICK is installed;
//   3. everything else stays here, as before (and a windowed mod stays here too when FLICK is not installed - with a
//      warning in the log that its windows cannot be drawn without FLICK).
//
// "Has FLICK windows" is read from the mod's own DLL before it connects - POSITIVE evidence, per module (logic library,
// 2026-10-11: classify a FLICK client by what its DLL carries, never by a guess): a class derived from FLICK's IWindow
// leaves its base class's RTTI name, ".?AVIWindow@FUCK@@", in the DLL. Scanned 2026-10-15 over every FLICK client on this
// machine (the 61 that ask for RequestFUCK in 4. plans\amf-flick\old-builds, and Wait Your Turn Redux 1.0.3's Nexus DLL):
// exactly the two that register windows carry it - Racemenu Enhancer
// (FUCK-RACE.dll) and Stacks or Singles (FUCK-QTY.dll); none of the mods this menu fully hosts does (Wait Your Turn Redux,
// Simple Timed Block, Stances NG, Critical Hit Tweaks, Inventory Limits, Static Regeneration, Global Variable Framework,
// Lurking Menace, Navigation Restrictions NG, Exhausting Combat, Speed of Strolling, Kill Feed). A DLL built without RTTI
// would not carry the name, so a window registered here is also REMEMBERED (a "windows:" line in the choice file) and
// that mod is sent to FLICK from the next start - the backstop.
//
// The choice file, Data/SKSE/Plugins/ApocryphaMenuFramework/FlickLeftToFlick.txt, one DLL per line (lower case):
//   <dll> = <shown name>            the player chose FLICK's own window (the 2.1.6 format, unchanged)
//   amf: <dll> = <shown name>       the player chose this menu
//   windows: <dll> = <shown name>   it registered a FLICK window here (the backstop above) - not a choice
// ============================================================================================

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace flick::route
{
	// The RTTI type name a class derived from FLICK's IWindow carries for its base (MSVC's decorated name, NUL-terminated).
	inline constexpr std::string_view kWindowClassMarker = ".?AVIWindow@FUCK@@";

	enum class Choice { kNone, kAmf, kFlick };
	enum class Place { kAmf, kFlick };

	// THE RULE (above). a_realFlick: FUCK.dll is installed; a_hasWindows: the DLL carries the IWindow marker, or registered
	// a window here in an earlier session. a_knownClash (2.2.1, the owner, 2026-10-16): this exact DLL is proven to crash the
	// installed FLICK version (kKnownClashes below) - kept here, as "This menu" would. The player's choice still wins.
	constexpr Place Decide(Choice a_choice, bool a_realFlick, bool a_hasWindows, bool a_knownClash = false)
	{
		if (a_choice == Choice::kFlick) { return Place::kFlick; }
		if (a_choice == Choice::kAmf) { return Place::kAmf; }
		if (a_knownClash) { return Place::kAmf; }
		return (a_realFlick && a_hasWindows) ? Place::kFlick : Place::kAmf;
	}

	// ---- KNOWN CLASHES (2.2.1, the owner approved 2026-10-16) ----------------------------------------------------------------
	// A FLICK mod build proven to crash a FLICK version on its own is kept in this menu by default when exactly that FLICK
	// version is installed. Matched by the mod DLL's file name AND its SHA-256 (one build, never "any version of the mod") and
	// by FUCK.dll's file version (major.minor.patch). Any other hash or FLICK version gets the normal default.
	// Add a row only with proof that the pair crashes with nothing else hooked in.
	struct KnownClash
	{
		std::string_view dll;            // lower case
		std::string_view sha256;         // lower-case hex of the mod's DLL file
		std::string_view flickVersion;   // FUCK.dll's file version, "major.minor.patch"
		std::string_view what;           // for the log
	};
	inline constexpr KnownClash kKnownClashes[] = {
		// Racemenu Enhancer 1.0.0 (Nexus 182536, file 763451) on FLICK NG 1.5.0 (FUCK.dll 1.5.0.0, sha256 fea9690f35dae930...
		// 2a4749): crashes at FUCK.dll+00E67C2 (InputEvent::AsButtonEvent) the moment RaceMenu opens - 8 of 8 runs, with or
		// without AMF, and in a profile with no other input hooks (the AMF 2.2.1 test hand-off, 2026-10-16, minimal-profile run).
		{ "fuck-race.dll", "502dd85cd79206efd817f8afe9f639743974ca11c8376d447a34d9da9bc5e676", "1.5.0",
		  "Racemenu Enhancer 1.0.0 crashes FLICK 1.5.0 when RaceMenu opens" },
	};

	// "1.5.0.0" / "1, 5, 0, 0" -> "1.5.0"; empty when it has fewer than three numbers
	inline std::string ShortVersion(std::string_view a_version)
	{
		std::string out;
		int parts = 0;
		std::string num;
		auto flush = [&]() {
			if (num.empty()) { return; }
			if (parts < 3) { out += (parts ? "." : "") + num; }
			++parts;
			num.clear();
		};
		for (const char c : a_version)
		{
			if (c >= '0' && c <= '9') { num += c; }
			else { flush(); }
		}
		flush();
		return parts >= 3 ? out : std::string{};
	}

	// The known clash for this DLL build and this FLICK version, or nullptr. a_sha256 in any case; a_flickVersion as the file
	// version string reads (ShortVersion is applied).
	inline const KnownClash* FindKnownClash(std::string_view a_dllLower, std::string_view a_sha256, std::string_view a_flickVersion)
	{
		std::string hash(a_sha256);
		std::transform(hash.begin(), hash.end(), hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const std::string flick = ShortVersion(a_flickVersion);
		if (flick.empty() || hash.empty()) { return nullptr; }
		for (const auto& k : kKnownClashes)
		{
			if (k.dll == a_dllLower && k.sha256 == hash && k.flickVersion == flick) { return &k; }
		}
		return nullptr;
	}

	// whether any row names this DLL - so the (slower) file hash is only taken for a DLL that could match
	inline bool MayClash(std::string_view a_dllLower)
	{
		for (const auto& k : kKnownClashes) { if (k.dll == a_dllLower) { return true; } }
		return false;
	}

	// true when [a_begin, a_end) holds the marker with its terminating NUL (the whole name, not a prefix of a longer one)
	inline bool ContainsWindowClass(const std::uint8_t* a_begin, const std::uint8_t* a_end)
	{
		if (!a_begin || !a_end || a_end <= a_begin) { return false; }
		const std::string needle(kWindowClassMarker.data(), kWindowClassMarker.size() + 1);
		return std::search(a_begin, a_end, needle.begin(), needle.end()) != a_end;
	}

	// ---- the choice file -------------------------------------------------------------------------------------------------
	struct Line
	{
		enum class Kind { kNone, kFlick, kAmf, kWindows };
		Kind        kind = Kind::kNone;
		std::string dll;    // lower case
		std::string name;   // as the menu showed it, may be empty
	};

	inline std::string Trimmed(std::string_view a_text)
	{
		const auto b = a_text.find_first_not_of(" \t\r\n");
		if (b == std::string_view::npos) { return {}; }
		const auto e = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(b, e - b + 1));
	}

	inline std::string Lower(std::string a_text)
	{
		std::transform(a_text.begin(), a_text.end(), a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_text;
	}

	// One line of the file. Blank lines and "#" comments are Kind::kNone.
	inline Line ParseLine(std::string_view a_line)
	{
		Line out;
		std::string text = Trimmed(a_line);
		if (text.empty() || text.front() == '#') { return out; }
		out.kind = Line::Kind::kFlick;
		// a prefix is "amf:" or "windows:" before the DLL name (a DLL file name never holds a colon)
		if (const auto colon = text.find(':'); colon != std::string::npos && colon < text.find('='))
		{
			const std::string prefix = Lower(Trimmed(std::string_view(text).substr(0, colon)));
			if (prefix == "amf") { out.kind = Line::Kind::kAmf; }
			else if (prefix == "windows") { out.kind = Line::Kind::kWindows; }
			else { return Line{}; }   // a prefix this build does not know: ignored, never read as a FLICK choice
			text = Trimmed(std::string_view(text).substr(colon + 1));
		}
		std::string dll = text;
		if (const auto eq = text.find('='); eq != std::string::npos)
		{
			dll = Trimmed(std::string_view(text).substr(0, eq));
			out.name = Trimmed(std::string_view(text).substr(eq + 1));
		}
		out.dll = Lower(dll);
		if (out.dll.empty()) { return Line{}; }
		return out;
	}

	inline std::string FormatLine(Line::Kind a_kind, const std::string& a_dll, const std::string& a_name)
	{
		std::string prefix = a_kind == Line::Kind::kAmf ? "amf: " : a_kind == Line::Kind::kWindows ? "windows: " : "";
		return prefix + a_dll + (a_name.empty() || a_name == a_dll ? std::string{} : " = " + a_name);
	}
}
