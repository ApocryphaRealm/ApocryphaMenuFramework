#include <atomic>
#include <fstream>
#include "AMF/API.h"
#include "DevBenchTool.h"
#include "Input.h"
#include "Persistence.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"

namespace renderer { void RequestFontRebuild(); }
#include "SmfAlias.h"
#include "SystemRow.h"
#include "Theme.h"

#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>

#include <Windows.h>

#include <array>
#include <filesystem>
#include <format>

// ============================================================================================
// Apocrypha Menu Framework - M0: a plugin that loads on both runtimes, logs richly, and ships
// its public contract. Rendering (M1), input (M2) and the page registry (M3) build on this.
// ============================================================================================

// Set at load when a stale pre-1.6.3 ApocryphaMenuFramework.dll sits beside this DLL; read at kDataLoaded.
static std::atomic<bool> g_staleOldCopy{ false };

namespace
{
	// The keys the framework consumes while its menu is open. Menu toggle is F1 (0x3B) - the author,
	// 2026-08-27, during the first smoke test: rule 28's K default is for MODS, and the framework
	// on K collided with Dragon's Eye Minimap's own K the moment both ran. F1 matches the
	// established framework convention (SMF uses it), so "F1 = a framework menu" stays one idea
	// for users. M2 replaces this set with the live Controls-page bindings; the EXPORT CONTRACT
	// is what must not change.
	//
	// DirectInput scan codes: 0x3B = F1 (framework menu), 0x0F = Tab, 0x01 = Escape,
	// 0xC8/0xD0/0xCB/0xCD = arrow keys, 0x1C = Enter.
	constexpr std::array<std::int32_t, 8> kReservedKeys{ 0x3B, 0x0F, 0x01, 0xC8, 0xD0, 0xCB, 0xCD, 0x1C };

	void SKSEMessageListener(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kInputLoaded:
			logger::debug("kInputLoaded received (input capture is hook-based since M2; no sink to register)");
			break;
		case SKSE::MessagingInterface::kSaveGame:
			// data/dataLen is the save's own name (SKSE convention, undocumented in the header
			// but stable across the ecosystem) - not necessarily null-terminated, hence the
			// explicit length rather than treating it as a C-string. NEVER construct a
			// string_view over a.data unchecked: a null data with nonzero dataLen (or vice
			// versa) is UB the instant persistence touches it - a crash on every save/load a
			// bug like this can cause is exactly the kind of defect this project's rules exist
			// to prevent (rule 14 null checks; observed a real in-game crash during kPostLoadGame
			// dispatch to this plugin, 2026-08-27 - root cause not yet isolated, but this guard
			// is correct regardless of what turns out to have actually been null).
			if (a_msg->data && a_msg->dataLen > 0)
			{
				persistence::OnSaveGame(std::string_view(static_cast<const char*>(a_msg->data), a_msg->dataLen));
			}
			else
			{
				logger::warn("kSaveGame: data={}, dataLen={} - refusing to construct a string_view over this; state for this save was NOT committed",
							 static_cast<const void*>(a_msg->data), a_msg->dataLen);
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			// kPreLoadGame's payload is the save NAME (char* + dataLen) - capture it here.
			if (a_msg->data && a_msg->dataLen > 0)
			{
				persistence::OnPreLoadGame(std::string_view(static_cast<const char*>(a_msg->data), a_msg->dataLen));
			}
			else
			{
				logger::warn("kPreLoadGame: data={}, dataLen={} - no save name captured",
							 static_cast<const void*>(a_msg->data), a_msg->dataLen);
				persistence::OnPreLoadGame(std::string_view{});
			}
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			// kPostLoadGame's data is a BOOL (load succeeded), NOT a string - do NOT construct a
			// string_view over it (that dereferenced (void*)0x1 and crashed, 1.3.3). The name was
			// captured at kPreLoadGame; restore it only if the load actually succeeded.
			persistence::OnPostLoadGame(a_msg->data != nullptr);
			break;
		case SKSE::MessagingInterface::kPostLoad:
			// Re-run the SMF module-name alias. It was installed during our own load, but every
			// plugin that loads AFTER us imports an unpatched GetModuleHandleW, and a consumer
			// caches whatever it resolves on its FIRST call - so the patch has to be in place
			// before that call, not merely before the mod registers.
			smf_alias::Install();

			// Earliest point DevBench's cross-plugin interface can be requested (its own
			// contract). Register the amf.menu driving tool; retried at kDataLoaded since
			// DevBench's server can still be finishing startup here.
			devbenchtool::Init(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logger::debug("kDataLoaded received");
			// The framework's own text, in the game's language (or the INI's) - the game's INI is
			// readable by now, and the atlas is rebuilt for the language's glyphs before the next frame.
			strings::Load();
			renderer::RequestFontRebuild();
			if (g_staleOldCopy.load(std::memory_order_acquire)) {
				constexpr auto kStale = "Apocrypha Menu Framework: delete the old ApocryphaMenuFramework.dll from SKSE/Plugins (this version is !ApocryphaMenuFramework.dll).";
#if AMF_RUNTIME_LINE == 17
				RE::SendHUDMessage::ShowHUDMessage(kStale, nullptr, true);
#else
				RE::DebugNotification(kStale);
#endif
			}
			smf_alias::Install();  // last sweep: anything that loaded during the message phase
			devbenchtool::Init(true);  // retry / last attempt, once registration has had its chance

			// The row in the GAME's own System menu. Installed here because the UI singleton is
			// reliably up by kDataLoaded; a first failed lookup is never treated as permanent.
			systemrow::Install();

			break;
		default:
			break;
		}
	}
}

AMF_API std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity)
{
	const auto count = static_cast<std::uint32_t>(kReservedKeys.size());

	if (!a_buffer)
	{
		// Null buffer = "how big a buffer do I need" - the probe DEM 1.5.3+ already performs.
		return count;
	}

	const std::uint32_t written = a_capacity < count ? a_capacity : count;

	for (std::uint32_t i = 0; i < written; ++i)
	{
		a_buffer[i] = kReservedKeys[i];
	}

	logger::debug("SMF_GetReservedKeyCodes: reported {} reserved key(s) to a caller", written);

	return written;
}

AMF_API const char* AMF_GetVersionString()
{
	// Derived from the one authoritative version (CMake -> plugin declaration), never a
	// second hand-maintained literal - the literal drifted once already (said 1.0.0 at 1.0.2).
	static const std::string version = SKSE::PluginDeclaration::GetSingleton()->GetVersion().string(".");
	return version.c_str();
}

AMF_API std::uint32_t AMF_GetAPIVersion()
{
	return 1;
}

AMF_API bool AMF_RegisterPage(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render)
{
	return registry::Register(a_modName, a_pageName, a_render);
}

AMF_API std::uint32_t AMF_GetInputMode()
{
	// The LIVE mode, not a stored one. Consumers use this to word their own prompts, so it has to
	// track the pad being picked up mid-session; a saved setting could sit there saying "keyboard"
	// while the player is on a controller.
	return input::UsingController() ? 1u : 0u;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	// ONCE-ONLY GUARD (1.3.3; replaces the 1.3.2 filename check, which refused BOTH loads).
	// The AMF-MO2-Plugin presents this same DLL a second time under the virtual name
	// SKSEMenuFramework.dll so mods importing SMF by name resolve against AMF. SKSE enumerates
	// that alias as a plugin of its own and calls SKSEPlugin_Load into it a second time. Under
	// usvfs both names resolve to ONE real file, so the loader dedupes them into one module and
	// the hooked GetModuleFileNameW can answer with the alias name for the real load as well -
	// a "which file am I" test cannot tell the two calls apart (1.3.2 proved it live: both
	// loads "reported as incompatible"). What CAN tell them apart is order: SKSE always reaches
	// ApocryphaMenuFramework.dll before SKSEMenuFramework.dll (alphabetical, and the load-order
	// shim lists only the real name). So the FIRST call initialises; any later call - same
	// module or a genuine second copy - is refused before it touches the logger, the messaging
	// interface, or a hook. A named event is process-wide, so it holds in both cases.
	//
	// SINCE 1.6.3 THE FILE IS !ApocryphaMenuFramework.dll, which sorts before every other plugin, so
	// this module is always the first load - and a stale ApocryphaMenuFramework.dll a player left
	// behind from an older version loads AFTER it and lands here, inert. The alias answers the old
	// name with this module, so consumers never reach the stale copy; the stale file is reported
	// once the log is up (see the SORT-FIRST block below).
	//
	// THE NAME CARRIES THE PROCESS ID (1.5.9). "Local\" is the LOGON SESSION's namespace, not the
	// process's: the plain name was shared with every other SkyrimSE.exe alive at the time, and a
	// game left wedged in its exit (1.7.104, 2026-09-05 - one thread, no window, unkillable)
	// still held the event, so the NEXT game's AMF saw ERROR_ALREADY_EXISTS at its first and only
	// load, returned "import target only", and ran nothing - no log, no menu, no alias - while
	// SKSE reported it "loaded correctly". A lingering SkyrimSE.exe is a common thing on a
	// player's machine. Scoping the name to this process keeps the once-only guard exactly
	// once-only for THIS game and nobody else's.
	{
		wchar_t onceName[128]{};
		swprintf_s(onceName, L"Local\\ApocryphaMenuFramework.SKSEPlugin_Load.once.%lu", static_cast<unsigned long>(GetCurrentProcessId()));
		HANDLE once = CreateEventW(nullptr, TRUE, FALSE, onceName);
		if (once && GetLastError() == ERROR_ALREADY_EXISTS)
		{
			CloseHandle(once);

			// TRUE, not false - and the difference is load-bearing (1.5.0). SKSE calls
			// FreeLibrary on any plugin whose Load returns false, so returning false here
			// UNLOADED the alias module: GetModuleHandleW(L"SKSEMenuFramework.dll") then found
			// nothing, and a launcher mod resolving the framework by that name got null. Proven
			// against Risa's All In One Menu 4.9 on 2026-09-03 - its button logged
			// "GetMainWindow returned null" even though the exports were present. Returning true
			// keeps the module resident and reachable while this call still does nothing else:
			// no logger, no messaging listener, no hook, no second initialisation.
			return true;  // second SKSEPlugin_Load (the SKSEMenuFramework.dll alias): import target only
		}
		// deliberately leaked: the event must outlive this call for the lifetime of the process
	}

	// Workaround for static initialization order bug of CommonLibSSE-NG
	REL::Module::reset();

	// ADDRESS LIBRARY PRE-CHECK (1.5.6, tightened 1.5.7). Every address this plugin touches
	// resolves through Address Library, and CommonLibSSE opens the database on the FIRST lookup -
	// with errors that name a build-directory hash and nothing a player can act on (bug reports
	// 2026-08-27 and 2026-09-04). Check the file ourselves, before any relocation, and fail with
	// an instruction instead. SE names the file version-X-Y-Z-0.bin (format 1); AE 1.6.x names it
	// versionlib-X-Y-Z-0.bin (format 2). Skyrim 1.7.x ships a NEW database format (5) that the
	// CommonLibSSE-NG this build links cannot read, so a 1.7.x game is refused up front with the
	// truth - "install the newer library" would only move the player to a different error box.
	{
		const auto ver = REL::Module::get().version();
		const bool ae = ver[1] >= 6;
		const auto file = std::format("Data/SKSE/Plugins/{}-{}-{}-{}-0.bin", ae ? "versionlib" : "version", ver[0], ver[1], ver[2]);
		constexpr auto kHub = "Still stuck? The Apocrypha Realm Modding Hub on Discord - post in the bug reports forum with your skse64.log.";
		std::error_code ec;
		if (!std::filesystem::exists(file, ec)) {
			const auto msg = std::format(
				"Apocrypha Menu Framework cannot start: no Address Library database for Skyrim {}.{}.{}.\n\n"
				"Missing file: {}\n\n"
				"Install \"Address Library for SKSE Plugins\" (Nexus mod 32444) - the \"All in One\" file, "
				"which carries the database for every game version this build supports.\n\n{}",
				ver[0], ver[1], ver[2], file, kHub);
			logger::critical("{}", msg);
			SKSE::stl::report_and_fail(msg);
		}
		// Two build lines, one source (CMake AMF_RUNTIME_LINE): line 1 is SE 1.5.97 / AE 1.6.x on
		// CommonLibSSE-NG 3.7.0 and reads formats 1 and 2; line 17 is Skyrim 1.7.x on CommonLibSSE-NG
		// 7.2.0 and reads format 5 for 1.7 (and 2 for 1.6). The FOMOD installs whichever the player
		// picked, so a wrong pick is the most likely reason to land here - say so.
		constexpr bool kLine17 = (AMF_RUNTIME_LINE == 17);
		const bool game17 = ver[1] >= 7;
		const std::int32_t expectedFormat = !ae ? 1 : (game17 ? 5 : 2);
		const char* const  lineReads = kLine17 ? "2 and 5" : "1 and 2";  // what THIS build line can read
		const bool lineSupportsGame = kLine17 ? ae : !game17;
		std::int32_t format = 0;
		if (std::ifstream in(file, std::ios::binary); in) {
			in.read(reinterpret_cast<char*>(&format), sizeof(format));
		}
		if (!lineSupportsGame || format != expectedFormat) {
			const auto msg = std::format(
				"Apocrypha Menu Framework cannot start: Skyrim {}.{}.{} is not supported by the installed build.\n\n"
				"This is the {} build; it supports {}. Its Address Library database ({}) is format {}; "
				"this build reads formats {}.\n\n"
				"Re-run the mod's installer and pick the option for your game version. Updating Address "
				"Library alone will not change this.\n\n{}",
				ver[0], ver[1], ver[2],
				kLine17 ? "Skyrim 1.7.x" : "Skyrim SE / AE 1.6",
				kLine17 ? "Skyrim AE 1.6.x and 1.7.x" : "Skyrim SE 1.5.97 and AE 1.6.x",
				file, format, lineReads, kHub);
			logger::critical("{}", msg);
			SKSE::stl::report_and_fail(msg);
		}
	}

	// SKSE::Init AFTER the pre-check (it may touch the module/database) and BEFORE our logger:
	// CommonLibSSE-NG 7.x (the Skyrim 1.7.x line) installs its own truncating default logger inside
	// SKSE::Init, so ours has to come after it to keep every line in our format on both lines.
	SKSE::Init(a_skse);

	const SKSE::PluginDeclaration* plugin = SKSE::PluginDeclaration::GetSingleton();

	if (!logger::init(plugin->GetName()))
	{
		return false;
	}

	logger::info("Loading {} {}...", plugin->GetName(), plugin->GetVersion());
	logger::info("Original framework embedding Dear ImGui (MIT); not a fork of SKSE Menu Framework");
	logger::info("Build line: {} (CommonLibSSE-NG {}); game {}", AMF_RUNTIME_LINE == 17 ? "Skyrim 1.7.x" : "SE 1.5.97 / AE 1.6.x", AMF_RUNTIME_LINE == 17 ? "7.2.0" : "3.7.0", REL::Module::get().version().string("."));

	logger::debug("SKSE core APIs initialized");

	// SMF module-name alias, as early as we can manage. Third-party mods built against the stock
	// SKSE Menu Framework consumer header find the framework with
	// GetModuleHandleW(L"SKSEMenuFramework") and cache the result on the first call; without this
	// they resolve null, register nothing, and - because their success logging is unconditional -
	// report success anyway. See SmfAlias.h for why the MO2 virtual alias cannot cover this.

	// SORT-FIRST FILENAME (1.6.3): the shipped DLL is !ApocryphaMenuFramework.dll. A player who
	// updated by hand may still have the OLD ApocryphaMenuFramework.dll beside it; that copy loads
	// after this one and is refused by the once-only guard above, so nothing breaks - but it is
	// dead weight that will confuse the next bug report, so say so plainly.
	{
		std::error_code ec;
		if (std::filesystem::exists("Data/SKSE/Plugins/ApocryphaMenuFramework.dll", ec)) {
			logger::warn("A stale ApocryphaMenuFramework.dll is installed beside this build (!ApocryphaMenuFramework.dll). "
						 "It loads inert and does nothing, but delete it: Data/SKSE/Plugins/ApocryphaMenuFramework.dll "
						 "(and its .pdb). Mod Organizer users: replace the framework's mod folder instead of merging into it.");
			g_staleOldCopy.store(true, std::memory_order_release);
		}
	}
	smf_alias::Install();

	if (!SKSE::GetMessagingInterface()->RegisterListener("SKSE", SKSEMessageListener))
	{
		logger::error("Could not register the SKSE message listener; plugin load aborted");
		return false;
	}

	logger::debug("SKSE message listener registered");

	// Renderer hooks install HERE, not at kDataLoaded: D3D bring-up precedes kDataLoaded, so
	// there is no later moment that still catches init. The tension with the DEM early-
	// relocation lesson is real and deliberate - the mitigation is the byte-pattern guard on
	// every site: nothing is written over bytes that are not the expected call instruction,
	// and a refused guard leaves the plugin loaded-but-inert with the reason in the log.
	settings::Load();


	if (renderer::Install())
	{
		logger::info("Renderer hooks installed");

		if (input::Install())
		{
			logger::info("Input capture installed (M2)");
		}
		else
		{
			logger::warn("Input capture NOT installed - the menu renders but cannot take input (see errors above)");
		}
	}
	else
	{
		logger::warn("Renderer hooks NOT installed - the framework is inert this session (see errors above)");
	}

	logger::info("Successfully loaded!");

	return true;
}
