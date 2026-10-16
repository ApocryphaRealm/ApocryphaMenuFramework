// Off-game check of include/FlickCrashCore.h (2.2.1) - the FLICK crash notice: the last-session marker, which crash log is
// read, the plain "FUCK.dll" search, and which mods are offered.
//   tools\flick_crash_check.bat [real crash log that names FUCK.dll]
// Prints one line per check and "flick_crash_check: N of N passed"; exits 1 on any failure.
#include "../include/FlickCrashCore.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	namespace crash = flick::crash;
}

int main(int argc, char** argv)
{
	// ---- the marker ----
	{
		crash::Marker m{ true, 133456789012345678, { "fuck-race.dll", "fuck-qty.dll" } };
		const auto back = crash::ParseMarker(crash::FormatMarker(m));
		Check(back.valid && back.start == m.start && back.flickDlls == m.flickDlls, "marker round trip (start and both DLLs)");
	}
	{
		const auto m = crash::ParseMarker("start=42\r\nflick: FUCK-RACE.dll\r\nflick: fuck-race.dll\r\n# note\r\n\r\n");
		Check(m.valid && m.start == 42 && m.flickDlls.size() == 1 && m.flickDlls[0] == "fuck-race.dll", "a DLL is lower-cased and listed once");
	}
	Check(!crash::ParseMarker("flick: a.dll\r\n").valid, "a marker without start= is not valid");
	Check(!crash::ParseMarker("start=abc\r\n").valid, "a bad start= is not valid");
	Check(!crash::ParseMarker("").valid, "an empty marker is not valid");

	// ---- which crash log ----
	Check(crash::IsCrashLogName("crash-2026-10-16-04-19-17.log"), "Crash Logger's file name is a crash log");
	Check(crash::IsCrashLogName("CRASH-2026.LOG"), "any case");
	Check(!crash::IsCrashLogName("CrashLogger.log"), "CrashLogger.log (its own log) is not a crash log");
	Check(!crash::IsCrashLogName("crash-2026.log.bak"), "a .bak is not");
	Check(!crash::IsCrashLogName("skse64.log"), "skse64.log is not");
	{
		const std::vector<crash::LogFile> files = {
			{ "crash-old.log", 90 }, { "crash-a.log", 120 }, { "crash-b.log", 150 }, { "CrashLogger.log", 999 }, { "FUCK.log", 500 }
		};
		const auto pick = crash::NewestCrashLogAfter(files, 100);
		Check(pick && pick->name == "crash-b.log", "the NEWEST crash log after the session start is read");
		Check(!crash::NewestCrashLogAfter(files, 150), "a crash log at the start time itself, or older, is never read");
		Check(!crash::NewestCrashLogAfter(files, 200), "no crash log after the start - no notice");
		Check(!crash::NewestCrashLogAfter({}, 0), "an empty folder - no notice");
	}

	// ---- the search ----
	Check(crash::NamesFlick("Unhandled exception at 0x7FF9871F67C2 FUCK.dll+00E67C2\tcmp [rcx+0x0C], eax"), "the crash line names FUCK.dll");
	Check(crash::NamesFlick("modules: fuck.dll 0x7FF98710"), "any case");
	Check(!crash::NamesFlick("SkyrimSE.exe+0123456 ... FUCK-RACE.log"), "a crash elsewhere (FUCK-RACE.log, not FUCK.dll) does not");
	Check(!crash::NamesFlick(""), "empty text does not");

	// ---- what is offered ----
	{
		crash::Marker m{ true, 1, { "fuck-race.dll", "fuck-qty.dll" } };
		const auto both = crash::ToOffer(m, true, {});
		Check(both.size() == 2, "a crash naming FLICK offers every windowed mod the session sent to FLICK");
		const auto one = crash::ToOffer(m, true, { "fuck-qty.dll" });
		Check(one.size() == 1 && one[0] == "fuck-race.dll", "a mod the player already keeps here is not offered");
		Check(crash::ToOffer(m, false, {}).empty(), "a crash that does not name FLICK offers nothing");
		Check(crash::ToOffer(crash::Marker{ true, 1, {} }, true, {}).empty(), "no windowed mod sent to FLICK - nothing offered");
		Check(crash::ToOffer(crash::Marker{}, true, {}).empty(), "no valid marker - nothing offered");
	}

	// ---- a real crash log (optional) ----
	if (argc > 1)
	{
		std::ifstream in(argv[1], std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		Check(!text.empty() && crash::NamesFlick(text), std::string("the real crash log names FUCK.dll: ") + argv[1]);
	}

	std::cout << "flick_crash_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
