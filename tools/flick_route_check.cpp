// Off-game check of include/FlickRouteCore.h (2.2.1) - where a FLICK mod goes: the player's choice first, else a mod with
// FLICK windows to the real FLICK when it is installed, else this menu; the choice file's lines; the IWindow class marker.
//   tools\flick_route_check.bat [folder of FLICK client DLLs]
// With a folder, every DLL in it (recursively) that asks for RequestFUCK is read for the marker and listed; the check then
// requires the marker in exactly the DLLs named FUCK-RACE.dll and FUCK-QTY.dll (Racemenu Enhancer, Stacks or Singles - the
// FLICK mods known to register windows) and in none of the others. Prints one line per check and
// "flick_route_check: N of N passed"; exits 1 on any failure.
#include "../include/FlickRouteCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
			const bool expect = name == "fuck-race.dll" || name == "fuck-qty.dll";
			Check(Has(bytes) == expect, std::string(expect ? "has FLICK windows:    " : "no FLICK windows:     ") +
											std::filesystem::relative(it->path(), argv[1], ec).string());
		}
		Check(seen > 0, "the folder holds FLICK client DLLs (" + std::to_string(seen) + ")");
	}

	std::cout << "flick_route_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
