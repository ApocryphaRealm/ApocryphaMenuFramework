#pragma once

// ============================================================================================
// THE FLICK CRASH NOTICE - the part with no game in it, so tools\flick_crash_check.cpp runs the same code offline (2.2.1).
//
// Why (the owner approved it 2026-10-16, after the 2.2.1 test): with the 2.2.1 default (FlickRouteCore.h) a FLICK mod that
// has windows goes to the real FLICK. Racemenu Enhancer 1.0.0 then crashed FLICK NG 1.5.0 the moment RaceMenu opened,
// 6 of 6 times, with or without AMF (FUCK.dll+00E67C2, InputEvent::AsButtonEvent) - the two versions clash. "This menu"
// avoids it. So when the last session ended in a crash inside FLICK while AMF had sent such a mod to FLICK, AMF OFFERS -
// once, never by itself - to keep that mod in this menu.
//
// The test, kept deliberately plain:
//   1. AMF writes a small marker each session (FlickLastSession.txt): the session's start time, and every DLL with FLICK
//      windows it sent to the real FLICK.
//   2. At the next start it reads the previous marker. If that session sent at least one such DLL to FLICK, it takes the
//      NEWEST crash-*.log in the SKSE log folder (where Crash Logger writes) that is newer than that session's start,
//      and asks which module FAULTED (FaultingModule below). Narrowed 2026-10-16 (Main Agent): Crash Logger lists every
//      loaded module in each log ("MODULES:"), so with FLICK installed every crash log contains "FUCK.dll" somewhere;
//      only the exception header counts - the line
//          Unhandled exception "EXCEPTION_ACCESS_VIOLATION" at 0x7FF9871F67C2 FUCK.dll+00E67C2	cmp [rcx+0x0C], eax | ...
//      (its module is the token after "at 0x<address>", up to the "+"), or, when that line names no module, the top frame
//      of "PROBABLE CALL STACK:" ("[0] 0x7FF9871F67C2     FUCK.dll+00E67C2", or "[ 0] ..." in other builds). The format
//      is pinned from the eight real crash logs of the 2.2.1 test (handoffs\evidence-2026-10-15-amf221).
//   3. FUCK.dll faulted: the notice offers "Keep <mod> in this menu" for each of those DLLs the player has not already kept
//      here. A crash anywhere else offers nothing.
// A crash is offered once: the next session's marker starts after that crash log was written, so the same log can never
// match again.
// ============================================================================================

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace flick::crash
{
	// ---- the marker ------------------------------------------------------------------------------------------------------
	//   start=<the session's start, as a file-clock count>
	//   flick: <dll>            one line per DLL with FLICK windows sent to the real FLICK this session
	struct Marker
	{
		bool                     valid = false;   // a "start=" line was read
		std::int64_t             start = 0;
		std::vector<std::string> flickDlls;       // lower case, each once
	};

	inline std::string Trim(std::string_view a_text)
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

	inline Marker ParseMarker(std::string_view a_text)
	{
		Marker m;
		std::size_t pos = 0;
		while (pos <= a_text.size())
		{
			const auto nl = a_text.find('\n', pos);
			const std::string line = Trim(a_text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
			pos = nl == std::string_view::npos ? a_text.size() + 1 : nl + 1;
			if (line.empty() || line.front() == '#') { continue; }
			if (line.rfind("start=", 0) == 0)
			{
				try { m.start = std::stoll(line.substr(6)); m.valid = true; } catch (...) { m.valid = false; }
			}
			else if (line.rfind("flick:", 0) == 0)
			{
				const std::string dll = Lower(Trim(std::string_view(line).substr(6)));
				if (!dll.empty() && std::find(m.flickDlls.begin(), m.flickDlls.end(), dll) == m.flickDlls.end()) { m.flickDlls.push_back(dll); }
			}
		}
		return m;
	}

	inline std::string FormatMarker(const Marker& a_marker)
	{
		std::string out = "# Apocrypha Menu Framework: this session's start and the FLICK mods with windows it sent to FLICK.\r\n"
						  "# Read at the next start to offer a crash notice (FlickCrashCore.h). Safe to delete.\r\n";
		out += "start=" + std::to_string(a_marker.start) + "\r\n";
		for (const auto& dll : a_marker.flickDlls) { out += "flick: " + dll + "\r\n"; }
		return out;
	}

	// ---- the crash log -----------------------------------------------------------------------------------------------------
	struct LogFile
	{
		std::string  name;    // file name only
		std::int64_t time;    // last write, the same file-clock count as Marker::start
	};

	// "crash-<anything>.log", any case - Crash Logger's file name
	inline bool IsCrashLogName(std::string_view a_name)
	{
		const std::string n = Lower(std::string(a_name));
		return n.size() > 10 && n.rfind("crash-", 0) == 0 && n.ends_with(".log");
	}

	// The newest crash log written after a_after; none when there is none.
	inline std::optional<LogFile> NewestCrashLogAfter(const std::vector<LogFile>& a_files, std::int64_t a_after)
	{
		std::optional<LogFile> best;
		for (const auto& f : a_files)
		{
			if (!IsCrashLogName(f.name) || f.time <= a_after) { continue; }
			if (!best || f.time > best->time) { best = f; }
		}
		return best;
	}

	// The whole test of the log: does its text name FLICK's file? (Any case, as a module list may spell it.)
	// "FUCK.dll+00E67C2" at a_pos (after any spaces/tabs) -> "FUCK.dll"; empty when the token has no "+offset"
	inline std::string ModuleToken(std::string_view a_line, std::size_t a_pos)
	{
		while (a_pos < a_line.size() && (a_line[a_pos] == ' ' || a_line[a_pos] == '\t')) { ++a_pos; }
		const auto end = a_line.find_first_of(" \t\r", a_pos);
		const std::string_view token = a_line.substr(a_pos, end == std::string_view::npos ? std::string_view::npos : end - a_pos);
		const auto plus = token.rfind('+');
		if (plus == std::string_view::npos || plus == 0) { return {}; }
		return std::string(token.substr(0, plus));
	}

	// After "0x<hex digits>" at or after a_pos: the index just past the hex number; npos when there is none.
	inline std::size_t PastAddress(std::string_view a_line, std::size_t a_pos)
	{
		const auto at = a_line.find("0x", a_pos);
		if (at == std::string_view::npos) { return std::string_view::npos; }
		std::size_t i = at + 2;
		while (i < a_line.size() && std::isxdigit(static_cast<unsigned char>(a_line[i]))) { ++i; }
		return i == at + 2 ? std::string_view::npos : i;
	}

	inline std::string_view LineAt(std::string_view a_text, std::size_t a_start)
	{
		const auto nl = a_text.find('\n', a_start);
		return a_text.substr(a_start, nl == std::string_view::npos ? std::string_view::npos : nl - a_start);
	}

	// The module the crash log says faulted (see the header): the exception line's module, else the top call-stack frame's.
	// Empty when neither says. Never looks into the module list.
	inline std::string FaultingModule(std::string_view a_text)
	{
		// 1. "Unhandled exception "<code>" at 0x<address> <module>+<offset>" - the first such line, at a line start
		for (std::size_t pos = 0; pos < a_text.size();)
		{
			const auto line = LineAt(a_text, pos);
			if (line.rfind("Unhandled exception", 0) == 0)
			{
				const auto at = line.find(" at 0x");
				if (at != std::string_view::npos)
				{
					const auto past = PastAddress(line, at + 4);
					if (past != std::string_view::npos)
					{
						if (auto m = ModuleToken(line, past); !m.empty()) { return m; }
					}
				}
				break;   // the exception line names no module: try the call stack
			}
			if (line.rfind("MODULES:", 0) == 0) { return {}; }   // past the header - stop
			pos += line.size() + 1;
		}
		// 2. the first frame under "PROBABLE CALL STACK:" - "[0] 0x<address> <module>+<offset>" (or "[ 0]")
		const auto stack = a_text.find("PROBABLE CALL STACK:");
		if (stack == std::string_view::npos) { return {}; }
		std::size_t pos = a_text.find('\n', stack);
		while (pos != std::string_view::npos && pos + 1 < a_text.size())
		{
			const auto line = LineAt(a_text, pos + 1);
			const auto open = line.find('[');
			const auto close = line.find(']');
			if (open == std::string_view::npos || close == std::string_view::npos || close < open) { return {}; }   // section ended
			std::string index(line.substr(open + 1, close - open - 1));
			index.erase(std::remove(index.begin(), index.end(), ' '), index.end());
			if (index == "0")
			{
				const auto past = PastAddress(line, close);
				return past == std::string_view::npos ? std::string{} : ModuleToken(line, past);
			}
			pos = a_text.find('\n', pos + 1);
		}
		return {};
	}

	// Whether FLICK's own file is the faulting module (any case).
	inline bool FaultsInFlick(std::string_view a_text)
	{
		return Lower(FaultingModule(a_text)) == "fuck.dll";
	}

	// (the first cut's whole-text search, kept for the check's "the module list names it too" case)
	inline bool NamesFlick(std::string_view a_text)
	{
		constexpr std::string_view kNeedle = "fuck.dll";
		if (a_text.size() < kNeedle.size()) { return false; }
		const auto it = std::search(a_text.begin(), a_text.end(), kNeedle.begin(), kNeedle.end(),
									[](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; });
		return it != a_text.end();
	}

	// The DLLs to offer: those the previous session sent to FLICK, minus the ones the player keeps here already. Empty when
	// there was no crash naming FLICK, or no windowed mod went to FLICK.
	inline std::vector<std::string> ToOffer(const Marker& a_previous, bool a_crashNamesFlick, const std::set<std::string>& a_keptHere)
	{
		std::vector<std::string> out;
		if (!a_previous.valid || !a_crashNamesFlick) { return out; }
		for (const auto& dll : a_previous.flickDlls)
		{
			if (!a_keptHere.contains(dll)) { out.push_back(dll); }
		}
		return out;
	}
}
