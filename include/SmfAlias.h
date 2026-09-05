#pragma once

// ============================================================================================
// SMF MODULE-NAME ALIAS
//
// The problem, measured in game on 2026-09-04. Every third-party mod built against the stock
// SKSE Menu Framework consumer header resolves the framework like this:
//
//     GetModuleHandleW(L"SKSEMenuFramework")
//
// AMF loads under its own module name, so that returns null and the mod silently registers
// nothing. Most of them then log "registered with SKSE Menu Framework" anyway - their success
// message is unconditional - so the failure is invisible from their side and presents to a
// player as a settings page that simply is not there.
//
// The MO2 file-mapper plugin (AMFLoadOrder.py) was built to close this by presenting AMF's DLL
// virtually as SKSE/Plugins/SKSEMenuFramework.dll. Measured: SKSE does load that alias, but both
// paths resolve to ONE real file and the loader dedupes them into a SINGLE module carrying the
// real name. So the FILE exists and the MODULE NAME still does not, which is the half that
// name-based resolution actually needs. Six third-party mods were live in that test and not one
// reached AMF's registry.
//
// The fix, and the reason it is this shape: patch the IMPORT TABLE of every loaded module so a
// request for "SKSEMenuFramework" returns AMF's own handle, and everything else passes through
// untouched. It ships NO second binary (the standing decision - see
// 4. plans\ApocryphaMenuFramework\identity-and-load-order-decisions.md), it does not rename
// AMF's DLL, and it writes over no system code: an IAT entry is a data pointer, so this is
// reversible and cannot corrupt kernel32.
//
// THE SECOND HALF (1.5.9), found from the first user reports after release. The stock consumer
// header asks a question BEFORE it asks for the module:
//
//     SKSEMenuFramework::IsInstalled()  ==  std::filesystem::exists("Data/SKSE/Plugins/SKSEMenuFramework.dll")
//
// and ten of ten third-party consumers surveyed gate their registration on it. That file never
// exists on a player's install, so they logged "not installed" and registered nothing - while
// every test here passed, because the MO2 file-mapper plugin on this machine presented the
// alias file and made the check true. The same import-table technique now answers the FILE
// question too: a query naming exactly SKSEMenuFramework.dll is redirected to AMF's own DLL
// path. The C++ runtime DLL (msvcp140) is patched alongside the plugins, because a dynamically
// linked consumer runs exists() inside it, not in its own image.
// ============================================================================================

#include <cstddef>

namespace smf_alias
{
	// Patches the module-name, LoadLibrary and file-query imports (GetModuleHandle*,
	// LoadLibrary*, GetProcAddress, GetFileAttributes*, FindFirstFile*, CreateFile*) in every
	// currently loaded SKSE plugin and the C++ runtime. Idempotent and cheap to repeat - call it
	// again whenever more modules may have loaded, because a consumer that loads after us
	// imports an unpatched pointer.
	//
	// Returns the number of import entries redirected by THIS call.
	std::size_t Install();

	// Total entries redirected so far, and how many times each alias has actually answered a
	// lookup. Exposed for the DevBench tool so a live run can be checked without reading the
	// log. FileAliasHits at zero after consumers loaded means the IsInstalled() gate was never
	// asked through a patched import - the symptom to chase, not a clean state.
	std::size_t PatchedEntries();
	std::size_t AliasHits();
	std::size_t FileAliasHits();
}
