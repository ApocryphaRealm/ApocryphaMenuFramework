// Off-game check of include/FlickRouteCore.h (2.2.1) - where a FLICK mod goes: the player's choice first, else a mod with
// FLICK windows to the real FLICK when it is installed, else this menu; the choice file's lines; the IWindow class marker.
//   tools\flick_route_check.bat [folder of FLICK client DLLs]
// With a folder, every DLL in it (recursively) that asks for RequestFUCK is read for the marker and listed; the check then
// requires the marker in exactly the DLLs named FUCK-RACE.dll and FUCK-QTY.dll (Racemenu Enhancer, Stacks or Singles - the
// FLICK mods known to register windows; 2.3.0: and Fuzzles' FUCK-DEMO / FUCK-Template test builds) and in none of the others,
// and reads each DLL's required API version from its code (checked for the DLLs in kExpectedApi, printed for the rest). Prints one line per check and
// "flick_route_check: N of N passed"; exits 1 on any failure.
#include "../include/FlickRouteCore.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	using flick::route::Choice;
	using flick::route::Decide;
	using flick::route::Place;
	using Kind = flick::route::Line::Kind;

	bool Has(const std::string& a_bytes)
	{
		const auto* b = reinterpret_cast<const std::uint8_t*>(a_bytes.data());
		return flick::route::ContainsWindowClass(b, b + a_bytes.size());
	}

	std::string Lower(std::string a_s) { return flick::route::Lower(std::move(a_s)); }

	// 2.3.0: a DLL file laid out as the loader maps it (headers, then each section's raw data at its VirtualAddress), so
	// RequiredApiFromImage reads it exactly as it reads a loaded module in game
	std::vector<std::uint8_t> MapImage(const std::string& a_file)
	{
		auto rd = [&](std::size_t o, int n) -> std::uint32_t {
			std::uint32_t v = 0;
			for (int b = 0; b < n; ++b) { if (o + b < a_file.size()) { v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_file[o + b])) << (8 * b); } }
			return v;
		};
		if (a_file.size() < 0x400 || rd(0, 2) != 0x5A4D) { return {}; }
		const std::size_t nt = rd(0x3C, 4);
		if (rd(nt, 4) != 0x00004550) { return {}; }
		const std::size_t opt = nt + 24;
		const std::uint32_t imageSize = rd(opt + 56, 4), headers = rd(opt + 60, 4);
		if (imageSize == 0 || imageSize > (512u << 20)) { return {}; }
		std::vector<std::uint8_t> img(imageSize, 0);
		std::memcpy(img.data(), a_file.data(), std::min<std::size_t>({ headers, a_file.size(), img.size() }));
		const std::uint32_t sections = rd(nt + 6, 2);
		const std::size_t first = opt + rd(nt + 20, 2);
		for (std::uint32_t s = 0; s < sections; ++s)
		{
			const std::size_t h = first + s * 40;
			const std::uint32_t va = rd(h + 12, 4), rawSize = rd(h + 16, 4), raw = rd(h + 20, 4);
			if (va >= img.size() || raw >= a_file.size()) { continue; }
			const std::size_t n = std::min<std::size_t>({ rawSize, a_file.size() - raw, img.size() - va });
			std::memcpy(img.data() + va, a_file.data() + raw, n);
		}
		return img;
	}

	// the API version each known FLICK client DLL requires (4. plans\amf-flick-v6\PLAN.md 5.2 for the installed ones; the two
	// test builds of 4. plans\amf-flick-v6\testmods); a DLL not listed is printed, not checked
	const std::map<std::string, int> kExpectedApi = {
		{ "waityourturn.dll", 1 },      // Wait Your Turn Redux (Nexus and unpublished builds)
		{ "feetofskyrim.dll", 3 },      // Inverse Kinematics - Feet of Skyrim
		{ "testbench.dll", 3 },         // ours
		{ "autoaudioswitch.dll", 5 },   // Auto Output Switch Redux (9. quarantine, 2026-10-19)
		// (po3_killfeed.dll is not listed: 1.1.0 reads 1, 1.2.0 reads 3 and 1.2.1 reads 4 - each built with a newer header)
		{ "risaallinonemenu.dll", 1 },  // its own FLICK client, which asks for version 1
		{ "fuck-demo.dll", 6 },
		{ "fuck-template.dll", 7 },
	};
}

int main(int argc, char** argv)
{
	// ---- the rule ----
	Check(Decide(Choice::kNone, false, false) == Place::kAmf, "no choice, no FLICK, no windows -> this menu (as before)");
	Check(Decide(Choice::kNone, true, false) == Place::kAmf, "no choice, FLICK installed, no windows -> this menu (as before)");
	Check(Decide(Choice::kNone, false, true) == Place::kAmf, "no choice, windows, FLICK NOT installed -> this menu (as now, with a warning)");
	Check(Decide(Choice::kNone, true, true) == Place::kFlick, "no choice, windows, FLICK installed -> the real FLICK (the new default)");
	for (const bool realFlick : { false, true })
	{
		for (const bool windows : { false, true })
		{
			Check(Decide(Choice::kFlick, realFlick, windows) == Place::kFlick,
				  std::string("choice FLICK wins (FLICK ") + (realFlick ? "on" : "off") + ", windows " + (windows ? "yes" : "no") + ")");
			Check(Decide(Choice::kAmf, realFlick, windows) == Place::kAmf,
				  std::string("choice this menu wins (FLICK ") + (realFlick ? "on" : "off") + ", windows " + (windows ? "yes" : "no") + ")");
		}
	}

	// ---- the choice file ----
	{
		const auto l = flick::route::ParseLine("WaitYourTurn.dll = Wait Your Turn\r");
		Check(l.kind == Kind::kFlick && l.dll == "waityourturn.dll" && l.name == "Wait Your Turn", "2.1.6 line 'dll = name' reads as FLICK's own window");
	}
	{
		const auto l = flick::route::ParseLine("fuck-race.dll");
		Check(l.kind == Kind::kFlick && l.dll == "fuck-race.dll" && l.name.empty(), "a bare DLL name reads as FLICK's own window");
	}
	{
		const auto l = flick::route::ParseLine("  amf: FUCK-RACE.dll = Racemenu Enhancer ");
		Check(l.kind == Kind::kAmf && l.dll == "fuck-race.dll" && l.name == "Racemenu Enhancer", "'amf: dll = name' reads as this menu");
	}
	{
		const auto l = flick::route::ParseLine("windows: fuck-qty.dll");
		Check(l.kind == Kind::kWindows && l.dll == "fuck-qty.dll", "'windows: dll' reads as a remembered window, not a choice");
	}
	Check(flick::route::ParseLine("# comment").kind == Kind::kNone, "a comment is skipped");
	Check(flick::route::ParseLine("   \r").kind == Kind::kNone, "a blank line is skipped");
	Check(flick::route::ParseLine("later: x.dll").kind == Kind::kNone, "an unknown prefix is ignored, never read as a FLICK choice");
	Check(flick::route::ParseLine("amf: = name").kind == Kind::kNone, "a line with no DLL is ignored");
	Check(flick::route::ParseLine("x.dll = a: b").kind == Kind::kFlick && flick::route::ParseLine("x.dll = a: b").name == "a: b",
		  "a colon in the shown name is not a prefix");
	for (const auto kind : { Kind::kFlick, Kind::kAmf, Kind::kWindows })
	{
		const auto text = flick::route::FormatLine(kind, "fuck-race.dll", "Racemenu Enhancer");
		const auto back = flick::route::ParseLine(text);
		Check(back.kind == kind && back.dll == "fuck-race.dll" && back.name == "Racemenu Enhancer", "round trip: " + text);
	}
	Check(flick::route::FormatLine(Kind::kAmf, "a.dll", "a.dll") == "amf: a.dll", "a name equal to the DLL is not written twice");

	// ---- the IWindow class marker ----
	Check(Has(std::string("xx.?AVIWindow@FUCK@@") + '\0' + "yy"), "the marker with its NUL is found");
	Check(!Has(std::string("xx.?AVIWindow@FUCK@@Z") + '\0'), "a longer name that starts with the marker is not");
	Check(!Has(std::string(".?AVITool@FUCK@@") + '\0'), "the page class (ITool) alone is not a window");
	Check(!Has(std::string()), "an empty buffer is not");

	// ---- known clashes (2026-10-16) ----
	{
		constexpr std::string_view kRaceSha = "502dd85cd79206efd817f8afe9f639743974ca11c8376d447a34d9da9bc5e676";
		const auto* k = flick::route::FindKnownClash("fuck-race.dll", kRaceSha, "1.5.0.0");
		Check(k != nullptr && k->flickVersion == "1.5.0", "Racemenu Enhancer 1.0.0's DLL on FLICK 1.5.0.0 is a known clash");
		Check(flick::route::FindKnownClash("fuck-race.dll", "502DD85CD79206EFD817F8AFE9F639743974CA11C8376D447A34D9DA9BC5E676", "1, 5, 0, 0") != nullptr,
			  "the hash in upper case and the version written with commas match too");
		Check(flick::route::FindKnownClash("fuck-race.dll", kRaceSha, "1.5.1.0") == nullptr, "another FLICK version (1.5.1) is not a clash");
		Check(flick::route::FindKnownClash("fuck-race.dll", kRaceSha, "1.4.0.0") == nullptr, "an older FLICK version (1.4.0) is not a clash");
		Check(flick::route::FindKnownClash("fuck-race.dll", "0000000000000000000000000000000000000000000000000000000000000000", "1.5.0.0") == nullptr,
			  "another build of the mod (another hash) is not a clash");
		Check(flick::route::FindKnownClash("fuck-qty.dll", kRaceSha, "1.5.0.0") == nullptr, "another DLL name with the same hash is not");
		Check(flick::route::FindKnownClash("fuck-race.dll", "", "1.5.0.0") == nullptr, "an unreadable hash is not");
		Check(flick::route::FindKnownClash("fuck-race.dll", kRaceSha, "") == nullptr, "an unknown FLICK version is not");
		Check(flick::route::MayClash("fuck-race.dll") && !flick::route::MayClash("waityourturn.dll"), "only a listed DLL name is ever hashed");
		Check(flick::route::ShortVersion("1.5.0.0") == "1.5.0" && flick::route::ShortVersion("1.5") .empty(), "versions shorten to three numbers");
		Check(Decide(Choice::kNone, true, true, true) == Place::kAmf, "a known clash keeps the windowed mod here (the default would send it to FLICK)");
		Check(Decide(Choice::kFlick, true, true, true) == Place::kFlick, "the player's 'FLICK's own window' still wins over a known clash");
		Check(Decide(Choice::kAmf, true, true, true) == Place::kAmf, "the player's 'This menu' agrees with it");
		Check(Decide(Choice::kNone, true, true, false) == Place::kFlick, "no clash: the normal default again");
	}

	// ---- 2.3.0: a FLICK older than the mod's API keeps the mod here (4. plans\amf-flick-v6\PLAN.md 3.4) ----
	{
		using flick::route::FlickApiOfFileVersion;
		using flick::route::FlickTooOld;
		using flick::route::kApiNewer;
		using flick::route::kApiUnknown;
		Check(FlickApiOfFileVersion("1.5.0.0") == 5 && FlickApiOfFileVersion("1, 6, 1, 0") == 6 && FlickApiOfFileVersion("1.7.3.0") == 7,
			  "FUCK.dll 1.5.x / 1.6.x / 1.7.x answer API 5 / 6 / 7");
		Check(FlickApiOfFileVersion("1.8.0.0") == kApiNewer && FlickApiOfFileVersion("2.0.0.0") == kApiNewer,
			  "a FLICK newer than 1.7 is assumed new enough");
		Check(FlickApiOfFileVersion("1.4.2.0") == kApiUnknown && FlickApiOfFileVersion("") == kApiUnknown &&
				  FlickApiOfFileVersion("x") == kApiUnknown && FlickApiOfFileVersion("0.9.0.0") == kApiUnknown,
			  "an older or unreadable FLICK version makes no claim");
		Check(FlickTooOld(5, 6) && FlickTooOld(5, 7) && FlickTooOld(6, 7), "FLICK API 5 is too old for a v6 / v7 mod, API 6 for a v7 mod");
		Check(!FlickTooOld(5, 5) && !FlickTooOld(7, 6) && !FlickTooOld(kApiNewer, 7), "the same or a newer FLICK is not too old");
		Check(!FlickTooOld(kApiUnknown, 7) && !FlickTooOld(5, kApiUnknown), "an unknown version on either side is never 'too old'");
		Check(Decide(Choice::kNone, true, true, false, true) == Place::kAmf, "too old + windows -> this menu (it would have no menu in FLICK)");
		Check(Decide(Choice::kNone, true, false, false, true) == Place::kAmf, "too old, no windows -> this menu");
		Check(Decide(Choice::kFlick, true, true, false, true) == Place::kFlick, "too old + the player chose FLICK -> FLICK (the choice wins)");
		Check(Decide(Choice::kAmf, true, true, false, true) == Place::kAmf, "too old + the player chose this menu -> this menu");
		Check(Decide(Choice::kNone, true, true, false, FlickTooOld(kApiUnknown, 6)) == Place::kFlick,
			  "unknown FLICK version + windows -> FLICK, as before (no claim, no change)");
		Check(Decide(Choice::kNone, true, true, false, FlickTooOld(5, kApiUnknown)) == Place::kFlick,
			  "unknown mod version + windows -> FLICK, as before (no claim, no change)");
		Check(Decide(Choice::kNone, true, true, false, FlickTooOld(7, 6)) == Place::kFlick, "FLICK 1.7 with a v6 windowed mod -> FLICK, as before");
		Check(Has(std::string("..?AVICameraClient@FUCK@@") + '\0') == false &&
				  flick::route::ContainsCameraClass(reinterpret_cast<const std::uint8_t*>("x.?AVICameraClient@FUCK@@"),
													reinterpret_cast<const std::uint8_t*>("x.?AVICameraClient@FUCK@@") + 26),
			  "the camera client marker is its own (not the window one) and is found with its NUL");
	}

	// ---- 2.3.0: the API version a DLL requires, read from its code (a synthetic module) ----
	{
		auto make = [](const std::vector<std::uint8_t>& a_code) {
			std::vector<std::uint8_t> img(0x3000, 0);
			auto put16 = [&](std::size_t o, std::uint32_t v) { img[o] = static_cast<std::uint8_t>(v); img[o + 1] = static_cast<std::uint8_t>(v >> 8); };
			auto put32 = [&](std::size_t o, std::uint32_t v) { for (int b = 0; b < 4; ++b) { img[o + b] = static_cast<std::uint8_t>(v >> (8 * b)); } };
			img[0] = 'M'; img[1] = 'Z';
			put32(0x3C, 0x80);
			put32(0x80, 0x00004550);
			put16(0x86, 2);      // two sections
			put16(0x94, 0xF0);   // optional header size
			const std::size_t sh = 0x80 + 24 + 0xF0;
			std::memcpy(&img[sh], ".text", 5); put32(sh + 8, 0x1000); put32(sh + 12, 0x1000); put32(sh + 36, 0x60000020);
			std::memcpy(&img[sh + 40], ".rdata", 6); put32(sh + 48, 0x1000); put32(sh + 52, 0x2000); put32(sh + 76, 0x40000040);
			std::memcpy(&img[0x2100], "RequestFUCK", 12);
			std::memcpy(&img[0x1000], a_code.data(), a_code.size());
			return img;
		};
		// lea rdx, [rip + disp] at 0x1000 -> 0x2100: disp = 0x2100 - 0x1007 = 0x10F9
		const std::vector<std::uint8_t> lea = { 0x48, 0x8D, 0x15, 0xF9, 0x10, 0x00, 0x00 };
		auto with = [&](std::vector<std::uint8_t> a_tail) { auto c = lea; c.insert(c.end(), a_tail.begin(), a_tail.end()); return c; };
		const std::vector<std::uint8_t> call = { 0xFF, 0x15, 0, 0, 0, 0, 0x48, 0x85, 0xC0, 0x74, 0x10, 0xFF, 0xD0, 0x48, 0x85, 0xC0, 0x74, 0x08 };
		auto api = [&](std::vector<std::uint8_t> a_tail) {
			auto c = call; c.insert(c.end(), a_tail.begin(), a_tail.end());
			const auto img = make(with(c));
			return flick::route::RequiredApiFromImage(img.data(), img.size());
		};
		Check(api({ 0x83, 0x38, 0x06, 0x72, 0x05 }) == 6, "cmp dword [rax], 6 / jb -> API 6");
		Check(api({ 0x83, 0x3B, 0x07, 0x73, 0x05 }) == 7, "cmp dword [rbx], 7 / jae -> API 7");
		Check(api({ 0x41, 0x83, 0x3E, 0x05, 0x0F, 0x82, 0, 0, 0, 0 }) == 5, "cmp dword [r14], 5 / jb rel32 -> API 5");
		Check(api({ 0x83, 0x38, 0x05, 0x76, 0x05 }) == 6, "cmp dword [rax], 5 / jbe -> API 6 (version <= N-1)");
		Check(api({ 0x8B, 0x00, 0x83, 0xF8, 0x03, 0x72, 0x05 }) == 3, "mov eax, [rax] / cmp eax, 3 / jb -> API 3 (FeetOfSkyrim's shape)");
		Check(api({ 0x44, 0x8B, 0x03, 0x41, 0x83, 0xF8, 0x07, 0x72, 0x05 }) == 7, "mov r8d, [rbx] / cmp r8d, 7 / jb -> API 7");
		Check(api({ 0x8B, 0x00, 0x83, 0xF9, 0x03, 0x72, 0x05 }) == flick::route::kApiUnknown, "a compare of another register than the one loaded says nothing");
		Check(api({ 0x83, 0x38, 0x06, 0x74, 0x05 }) == flick::route::kApiUnknown, "a compare not followed by an unsigned jump says nothing");
		Check(api({ 0x90, 0x90 }) == flick::route::kApiUnknown, "no compare at all -> unknown");
		{
			// two sites that disagree: no claim
			auto c1 = with([&] { auto c = call; c.insert(c.end(), { 0x83, 0x38, 0x06, 0x72, 0x05 }); return c; }());
			auto img = make(c1);
			const std::size_t second = 0x1400;
			const std::int32_t disp = static_cast<std::int32_t>(0x2100 - (second + 7));
			const std::uint8_t lea2[] = { 0x48, 0x8D, 0x15, static_cast<std::uint8_t>(disp), static_cast<std::uint8_t>(disp >> 8),
										  static_cast<std::uint8_t>(disp >> 16), static_cast<std::uint8_t>(disp >> 24), 0x83, 0x38, 0x07, 0x72, 0x05 };
			std::memcpy(&img[second], lea2, sizeof(lea2));
			Check(flick::route::RequiredApiFromImage(img.data(), img.size()) == flick::route::kApiUnknown, "two sites that disagree -> unknown");
		}
		{
			auto img = make(with(call));
			img[0x2100] = 'X';   // no "RequestFUCK" text
			Check(flick::route::RequiredApiFromImage(img.data(), img.size()) == flick::route::kApiUnknown, "no RequestFUCK text -> unknown");
		}
		Check(flick::route::RequiredApiFromImage(nullptr, 0) == flick::route::kApiUnknown, "no image -> unknown");
	}

	// ---- real DLLs (optional) ----
	if (argc > 1)
	{
		std::error_code ec;
		int seen = 0;
		for (auto it = std::filesystem::recursive_directory_iterator(argv[1], ec); !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (!it->is_regular_file() || Lower(it->path().extension().string()) != ".dll") { continue; }
			std::ifstream in(it->path(), std::ios::binary);
			const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (bytes.find("RequestFUCK") == std::string::npos) { continue; }
			++seen;
			const std::string name = Lower(it->path().filename().string());
			// 2.3.0: Fuzzles' template carries a HUD window too (FUCK-Template, built here as the v7 test mod)
			const bool expect = name == "fuck-race.dll" || name == "fuck-qty.dll" || name == "fuck-demo.dll" || name == "fuck-template.dll";
			const std::string rel = std::filesystem::relative(it->path(), argv[1], ec).string();
			Check(Has(bytes) == expect, std::string(expect ? "has FLICK windows:    " : "no FLICK windows:     ") + rel);
			// 2.3.0: the API version its code requires (RequiredApiFromImage), on the file mapped as the loader would
			const auto image = MapImage(bytes);
			const int api = flick::route::RequiredApiFromImage(image.data(), image.size());
			const auto want = kExpectedApi.find(name);
			if (want != kExpectedApi.end())
			{
				Check(api == want->second, "requires API " + std::to_string(api) + " (expected " + std::to_string(want->second) + "): " + rel);
			}
			else { std::cout << "INFO requires API " << (api ? std::to_string(api) : std::string("unknown")) << ": " << rel << "\n"; }
		}
		Check(seen > 0, "the folder holds FLICK client DLLs (" + std::to_string(seen) + ")");
	}

	std::cout << "flick_route_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
