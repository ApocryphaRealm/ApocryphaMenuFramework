// Off-game check of include/FlickCrashCore.h (2.2.1) - the FLICK crash notice: the last-session marker, which crash log is
// read, the plain "FUCK.dll" search, and which mods are offered.
//   tools\flick_crash_check.bat [real crash logs that fault in FUCK.dll ...]
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

	// ---- the faulting module (2026-10-16: only the exception header and the top frame, never the module list) ----
	{
		// the header of a real Crash Logger SSE log (minimal-M2, 2026-10-16), shortened, then its module list
		const std::string flickCrash =
			"Skyrim SSE v1.5.97\r\nCrashLoggerSSE v1-15-0-0 Oct 12 2024 11:33:37\r\n\r\n"
			"Unhandled exception \"EXCEPTION_ACCESS_VIOLATION\" at 0x7FF9871F67C2 FUCK.dll+00E67C2\tcmp [rcx+0x0C], eax |  RE::InputEvent::AsButtonEvent\r\n"
			"Exception Flags: 0x00000000\r\n\r\nPROBABLE CALL STACK:\r\n"
			"\t[0] 0x7FF9871F67C2     FUCK.dll+00E67C2\r\n\t[1] 0x7FF9871D580E     FUCK.dll+00C580E\r\n\r\n"
			"MODULES:\r\n\tSkyrimSE.exe 0x7FF61B6B0000\r\n\tFUCK.dll 0x7FF987110000\r\n";
		Check(crash::FaultingModule(flickCrash) == "FUCK.dll", "a crash in FUCK.dll: the exception line's module is FUCK.dll");
		Check(crash::FaultsInFlick(flickCrash), "a crash in FUCK.dll offers");
		// the same log, but the crash is in the game: FUCK.dll is only in the module list
		const std::string elsewhere =
			"Skyrim SSE v1.5.97\r\n\r\n"
			"Unhandled exception \"EXCEPTION_ACCESS_VIOLATION\" at 0x7FF61C2C5130 SkyrimSE.exe+0C15130\tmov rcx, [0x00007FF61E600B28]\r\n\r\n"
			"PROBABLE CALL STACK:\r\n\t[0] 0x7FF61C2C5130 SkyrimSE.exe+0C15130 -> 67315+0x80\tmov rcx, [rax]\r\n"
			"\t[1] 0x7FF9871D580E     FUCK.dll+00C580E\r\n\r\n"
			"MODULES:\r\n\tSkyrimSE.exe 0x7FF61B6B0000\r\n\tFUCK.dll 0x7FF987110000\r\nSKSE PLUGINS:\r\n\tFUCK.dll v1.5.0\r\n";
		Check(crash::NamesFlick(elsewhere), "(the crash elsewhere still has FUCK.dll in its module list and a lower frame)");
		Check(crash::FaultingModule(elsewhere) == "SkyrimSE.exe", "a crash elsewhere: the faulting module is SkyrimSE.exe");
		Check(!crash::FaultsInFlick(elsewhere), "a crash elsewhere whose module list includes FUCK.dll does NOT offer");
		// the exception line without a module (an address in no module): the top frame decides
		const std::string noModule =
			"Unhandled exception \"EXCEPTION_ACCESS_VIOLATION\" at 0x000000000000\r\n\r\nPROBABLE CALL STACK:\r\n"
			"\t[ 0] 0x7FF8F17567C2                             FUCK.dll+00E67C2\r\n\t[ 1] 0x7FF8F1735000 SkyrimSE.exe+0123456\r\n";
		Check(crash::FaultsInFlick(noModule), "an exception line with no module: the top frame (the '[ 0]' spelling) decides");
		const std::string noModuleGame =
			"Unhandled exception \"EXCEPTION_STACK_OVERFLOW\" at 0x0\r\n\r\nPROBABLE CALL STACK:\r\n"
			"\t[0] 0x7FF61C2C5130 SkyrimSE.exe+0C15130\r\n\t[1] 0x7FF9871D580E     FUCK.dll+00C580E\r\n\r\nMODULES:\r\n\tFUCK.dll\r\n";
		Check(!crash::FaultsInFlick(noModuleGame), "...and FUCK.dll in frame 1 only does not offer");
		Check(crash::FaultsInFlick("Unhandled exception \"X\" at 0x7FF9871F67C2 fuck.dll+00E67C2\n"), "any case of the module name");
		Check(!crash::FaultsInFlick("Unhandled exception \"X\" at 0x7FF9871F67C2 FUCK-RACE.dll+0001234\n"), "FUCK-RACE.dll (the mod itself) is not FLICK");
		Check(!crash::FaultsInFlick(""), "empty text does not offer");
		Check(!crash::FaultsInFlick("MODULES:\r\n\tFUCK.dll\r\n"), "a log with only a module list does not offer");
	}

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

	// ---- real crash logs (optional): each must fault in FUCK.dll, and the same log with its faulting lines moved to the game
	// must not - its module list still names FUCK.dll ----
	for (int i = 1; i < argc; ++i)
	{
		std::ifstream in(argv[i], std::ios::binary);
		std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		Check(!text.empty() && crash::FaultsInFlick(text), std::string("real log faults in FUCK.dll: ") + argv[i]);
		// move the exception line's and the top frame's module to the game - everything after the header stays as it was
		std::string moved = text;
		for (int n = 0; n < 2; ++n)
		{
			if (const auto at = moved.find("FUCK.dll+"); at != std::string::npos) { moved.replace(at, 8, "SkyrimSE.exe"); }
		}
		Check(crash::NamesFlick(moved) && !crash::FaultsInFlick(moved),
			  std::string("the same log faulting in the game, FUCK.dll still listed: no offer - ") + argv[i]);
	}

	std::cout << "flick_crash_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
