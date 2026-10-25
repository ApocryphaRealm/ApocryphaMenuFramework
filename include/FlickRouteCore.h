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
//   1c. (2.3.0) with no choice made, a mod built for a NEWER FLICK API than the installed FLICK answers stays in this
//      menu - FLICK 1.5.0 (API 5) with a mod built for API 6 or 7 would refuse it in the mod's own Connect(), leaving it no
//      menu anywhere; here its pages are hosted (its windows wait for a newer FLICK);
//   2. with no choice made, a mod that HAS FLICK WINDOWS (2.3.0: or drives the camera through FLICK's camera API) goes to
//      the real FLICK when FLICK is installed;
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
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace flick::route
{
	// The RTTI type name a class derived from FLICK's IWindow carries for its base (MSVC's decorated name, NUL-terminated).
	inline constexpr std::string_view kWindowClassMarker = ".?AVIWindow@FUCK@@";

	// 2.3.0 (FLICK API 7): a class derived from FLICK's camera client (FUCK_Camera.h's ICameraClient) leaves this name. A mod
	// that drives the game camera through FLICK needs FLICK itself for it - this menu answers every camera call "no camera
	// here" - so it is routed like a mod with FLICK windows (THE RULE, 2).
	inline constexpr std::string_view kCameraClassMarker = ".?AVICameraClient@FUCK@@";

	enum class Choice { kNone, kAmf, kFlick };
	enum class Place { kAmf, kFlick };

	// THE RULE (above). a_realFlick: FUCK.dll is installed; a_hasWindows: the DLL carries the IWindow marker (or, 2.3.0, the
	// camera client's), or registered a window here in an earlier session. a_knownClash (2.2.1, the owner, 2026-10-16): this
	// exact DLL is proven to crash the installed FLICK version (kKnownClashes below) - kept here, as "This menu" would.
	// a_flickTooOld (2.3.0, 4. plans\amf-flick-v6\PLAN.md 3.4): the installed FLICK answers an OLDER API version than the
	// mod was built for, so the mod's own Connect() would refuse it and it would have no menu anywhere - kept here, where
	// its pages at least are hosted. Known only from positive evidence on both sides (FlickTooOld below). The player's
	// choice still wins over both.
	constexpr Place Decide(Choice a_choice, bool a_realFlick, bool a_hasWindows, bool a_knownClash = false, bool a_flickTooOld = false)
	{
		if (a_choice == Choice::kFlick) { return Place::kFlick; }
		if (a_choice == Choice::kAmf) { return Place::kAmf; }
		if (a_knownClash) { return Place::kAmf; }
		if (a_flickTooOld) { return Place::kAmf; }
		return (a_realFlick && a_hasWindows) ? Place::kFlick : Place::kAmf;
	}

	// ---- API VERSIONS (2.3.0) --------------------------------------------------------------------------------------------
	// A FLICK mod built against API n refuses a table whose version is below n (its header's Connect():
	// "if (!iface || iface->version < FUCK_API_VERSION) ... Version Mismatch"). These two readings feed a_flickTooOld above.
	inline constexpr int kApiUnknown = 0;
	inline constexpr int kApiNewer = 1000;   // a FLICK newer than any release this build knows: assumed new enough

	// The API version the real FLICK answers, from FUCK.dll's file version: 1.5.x -> 5, 1.6.x -> 6, 1.7.x -> 7 (the releases
	// read: 1.5.0 = API a9ce5d1 v5, 1.6.0 / 1.6.1 = 22b5362 v6, 1.7.0-1.7.3 = 895d596 / ddd727c v7). Anything newer
	// -> kApiNewer; older than 1.5 or unreadable -> kApiUnknown (no claim either way).
	inline int FlickApiOfFileVersion(std::string_view a_fileVersion)
	{
		int nums[2] = { -1, -1 };
		int at = 0;
		bool inNum = false;
		for (const char c : a_fileVersion)
		{
			if (c >= '0' && c <= '9')
			{
				if (!inNum) { if (at >= 2) { break; } nums[at] = 0; inNum = true; }
				nums[at] = nums[at] * 10 + (c - '0');
				if (nums[at] > 100000) { return kApiUnknown; }
			}
			else if (inNum) { inNum = false; ++at; }
		}
		const int major = nums[0], minor = nums[1];
		if (major < 0 || minor < 0) { return kApiUnknown; }
		if (major > 1) { return kApiNewer; }
		if (major < 1) { return kApiUnknown; }
		if (minor >= 5 && minor <= 7) { return minor; }
		return minor > 7 ? kApiNewer : kApiUnknown;
	}

	// true only when both versions are known and the real FLICK's is below the mod's
	inline constexpr bool FlickTooOld(int a_realApi, int a_modApi)
	{
		return a_realApi > kApiUnknown && a_modApi > kApiUnknown && a_realApi < a_modApi;
	}

	// The API version a mod's DLL requires, read from its CODE (positive evidence, or kApiUnknown). The header's Connect()
	// compiles to: lea rdx, ["RequestFUCK"] / call GetProcAddress / ... / call rax / test rax, rax / je ... /
	// cmp dword ptr [reg], N / jb (or jae) - so N is the first "cmp dword ptr [reg], imm8" followed by an unsigned
	// conditional jump within a short window after an instruction that loads the address of the "RequestFUCK" text
	// (jbe / ja compare N-1). Every such site in the module must agree, or the answer is kApiUnknown.
	// a_image: the module as MAPPED (section i at base + VirtualAddress), a_size its SizeOfImage. Reads nothing outside it.
	inline int RequiredApiFromImage(const std::uint8_t* a_image, std::size_t a_size)
	{
		if (!a_image || a_size < 0x400) { return kApiUnknown; }
		auto rd16 = [&](std::size_t o) -> std::uint32_t { return o + 2 <= a_size ? static_cast<std::uint32_t>(a_image[o] | (a_image[o + 1] << 8)) : 0u; };
		auto rd32 = [&](std::size_t o) -> std::uint32_t {
			return o + 4 <= a_size ? static_cast<std::uint32_t>(a_image[o]) | (static_cast<std::uint32_t>(a_image[o + 1]) << 8) |
										 (static_cast<std::uint32_t>(a_image[o + 2]) << 16) | (static_cast<std::uint32_t>(a_image[o + 3]) << 24) :
									 0u;
		};
		if (rd16(0) != 0x5A4D) { return kApiUnknown; }   // "MZ"
		const std::size_t nt = rd32(0x3C);
		if (nt + 24 > a_size || rd32(nt) != 0x00004550) { return kApiUnknown; }   // "PE\0\0"
		const std::uint32_t sections = rd16(nt + 6);
		const std::uint32_t optSize = rd16(nt + 20);
		const std::size_t first = nt + 24 + optSize;
		struct Range { std::size_t begin, end; bool exec; };
		Range ranges[96]{};
		std::uint32_t count = 0;
		for (std::uint32_t s = 0; s < sections && count < 96; ++s)
		{
			const std::size_t h = first + s * 40;
			if (h + 40 > a_size) { break; }
			const std::uint32_t vsize = rd32(h + 8), va = rd32(h + 12), ch = rd32(h + 36);
			if (!(ch & 0x40000000u)) { continue; }   // not readable
			const std::size_t b = va, e = std::min<std::size_t>(static_cast<std::size_t>(va) + vsize, a_size);
			if (b >= e) { continue; }
			ranges[count++] = { b, e, (ch & 0x20000000u) != 0 };
		}
		// the text's address(es), with its NUL - in the readable, non-code sections
		static constexpr char kName[] = "RequestFUCK";
		std::size_t texts[8]{};
		std::uint32_t textCount = 0;
		for (std::uint32_t r = 0; r < count; ++r)
		{
			if (ranges[r].exec) { continue; }
			const auto* b = a_image + ranges[r].begin;
			const auto* e = a_image + ranges[r].end;
			for (const auto* p = b; p < e && textCount < 8;)
			{
				p = std::search(p, e, kName, kName + sizeof(kName));   // sizeof includes the NUL
				if (p == e) { break; }
				texts[textCount++] = static_cast<std::size_t>(p - a_image);
				p += sizeof(kName);
			}
		}
		if (textCount == 0) { return kApiUnknown; }
		int found = kApiUnknown;
		for (std::uint32_t r = 0; r < count; ++r)
		{
			if (!ranges[r].exec) { continue; }
			for (std::size_t i = ranges[r].begin; i + 7 <= ranges[r].end; ++i)
			{
				// lea r64, [rip + disp32]: REX.W (48 or 4C) 8D, ModRM mod=00 rm=101
				const std::uint8_t rex = a_image[i];
				if ((rex != 0x48 && rex != 0x4C) || a_image[i + 1] != 0x8D || (a_image[i + 2] & 0xC7) != 0x05) { continue; }
				const auto disp = static_cast<std::int32_t>(rd32(i + 3));
				const auto target = static_cast<std::int64_t>(i) + 7 + disp;
				bool isText = false;
				for (std::uint32_t t = 0; t < textCount; ++t) { if (static_cast<std::int64_t>(texts[t]) == target) { isText = true; } }
				if (!isText) { continue; }
				// the first cmp dword ptr [reg], imm8 + unsigned jcc in the next 256 bytes
				const std::size_t end = std::min<std::size_t>(i + 7 + 256, ranges[r].end);
				for (std::size_t j = i + 7; j + 8 <= end; ++j)
				{
					// (a) cmp dword ptr [reg], imm8 - 83 /7 with mod=00, no SIB / RIP (41: [r8]..[r15])
					// (b) mov r32, dword ptr [reg] then cmp r32, imm8 - 8B /r mod=00, then 83 F8+r (FeetOfSkyrim's build)
					std::size_t k = j;
					if (a_image[k] == 0x41) { ++k; }
					bool matched = false;
					if (a_image[k] == 0x83)
					{
						const std::uint8_t modrm = a_image[k + 1];
						matched = (modrm & 0xF8) == 0x38 && (modrm & 7) != 4 && (modrm & 7) != 5;
					}
					if (!matched)
					{
						k = j;
						std::uint8_t rex = 0;
						if (a_image[k] >= 0x40 && a_image[k] <= 0x4F) { rex = a_image[k]; ++k; }
						if ((rex & 0x08) || a_image[k] != 0x8B) { continue; }   // a 32-bit load only
						const std::uint8_t m = a_image[k + 1];
						if ((m & 0xC0) != 0 || (m & 7) == 4 || (m & 7) == 5) { continue; }
						const int reg = ((rex & 0x04) ? 8 : 0) | ((m >> 3) & 7);
						k += 2;
						if (reg >= 8) { if (a_image[k] != 0x41) { continue; } ++k; }
						if (k + 5 > end || a_image[k] != 0x83 || a_image[k + 1] != (0xF8 | (reg & 7))) { continue; }
					}
					const int imm = a_image[k + 2];
					const std::uint8_t j0 = a_image[k + 3];
					const std::uint8_t j1 = k + 4 < ranges[r].end ? a_image[k + 4] : 0;
					int api = kApiUnknown;
					if (j0 == 0x72 || j0 == 0x73) { api = imm; }                                   // jb / jae rel8: version < N
					else if (j0 == 0x76 || j0 == 0x77) { api = imm + 1; }                          // jbe / ja rel8: version <= N-1
					else if (j0 == 0x0F && (j1 == 0x82 || j1 == 0x83)) { api = imm; }              // jb / jae rel32
					else if (j0 == 0x0F && (j1 == 0x86 || j1 == 0x87)) { api = imm + 1; }          // jbe / ja rel32
					else { continue; }
					if (api < 1 || api > 64) { break; }   // not a version compare: this site says nothing
					if (found != kApiUnknown && found != api) { return kApiUnknown; }   // sites disagree: no claim
					found = api;
					break;
				}
			}
		}
		return found;
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
	inline bool ContainsMarker(const std::uint8_t* a_begin, const std::uint8_t* a_end, std::string_view a_marker)
	{
		if (!a_begin || !a_end || a_end <= a_begin) { return false; }
		const std::string needle(a_marker.data(), a_marker.size() + 1);
		return std::search(a_begin, a_end, needle.begin(), needle.end()) != a_end;
	}
	inline bool ContainsWindowClass(const std::uint8_t* a_begin, const std::uint8_t* a_end) { return ContainsMarker(a_begin, a_end, kWindowClassMarker); }
	inline bool ContainsCameraClass(const std::uint8_t* a_begin, const std::uint8_t* a_end) { return ContainsMarker(a_begin, a_end, kCameraClassMarker); }

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
