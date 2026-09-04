#include "AMF/API.h"
#include "DevBenchTool.h"
#include "Input.h"
#include "Persistence.h"
#include "Registry.h"
#include "Renderer.h"
#include "Settings.h"
#include "SystemRow.h"
#include "Theme.h"

#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>

#include <Windows.h>

#include <array>

// ============================================================================================
// Apocrypha Menu Framework - M0: a plugin that loads on both runtimes, logs richly, and ships
// its public contract. Rendering (M1), input (M2) and the page registry (M3) build on this.
// ============================================================================================

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
			// Earliest point DevBench's cross-plugin interface can be requested (its own
			// contract). Register the amf.menu driving tool; retried at kDataLoaded since
			// DevBench's server can still be finishing startup here.
			devbenchtool::Init(false);
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			logger::debug("kDataLoaded received");
			devbenchtool::Init(true);  // retry / last attempt, after the demo page has registered

			// The row in the GAME's own System menu. Installed here because the UI singleton is
			// reliably up by kDataLoaded; a first failed lookup is never treated as permanent.
			systemrow::Install();

			// Dogfood the public API: the demo menu registers through AMF_RegisterPage exactly
			// as an external mod would, proving the registry + tab rendering end to end - and
			// giving the window enough selectable content to actually judge gamepad navigation
			// (the author, 1.1.2: "hard to tell whether it's working... there's not anything to
			// select yet"). Two pages -> renders as tabs (one-menu-per-mod rule).
			if (settings::Get().showApiDemo)
			{
				AMF_RegisterPage("AMF API Demo", "Widgets", +[]() {
					static bool toggleA = true;
					static bool toggleB = false;
					static float slider = 0.5f;
					static int counter = 0;
					widgets::Toggle("Demo toggle A", &toggleA);
					widgets::Toggle("Demo toggle B", &toggleB);
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
					ImGui::SliderFloat("Demo slider", &slider, 0.0f, 1.0f, "%.2f");
					if (ImGui::Button("Demo button"))
					{
						++counter;
					}
					ImGui::SameLine();
					ImGui::Text("pressed %d time(s)", counter);
					ImGui::TextWrapped("This menu registered itself through AMF_RegisterPage - "
									   "the same path every mod uses. Disable it with "
									   "bShowApiDemo=0 in ApocryphaMenuFramework.ini.");
				});
				AMF_RegisterPage("AMF API Demo", "Second Tab", +[]() {
					ImGui::TextWrapped("A second page from the same mod renders as a TAB inside "
									   "the one menu - never as a second menu (project rule).");
				});
			}
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
	return settings::Get().controllerMode ? 1u : 0u;
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
	{
		HANDLE once = CreateEventW(nullptr, TRUE, FALSE, L"Local\\ApocryphaMenuFramework.SKSEPlugin_Load.once");
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

	const SKSE::PluginDeclaration* plugin = SKSE::PluginDeclaration::GetSingleton();

	if (!logger::init(plugin->GetName()))
	{
		return false;
	}

	logger::info("Loading {} {}...", plugin->GetName(), plugin->GetVersion());
	logger::info("Original framework embedding Dear ImGui (MIT); not a fork of SKSE Menu Framework");

	SKSE::Init(a_skse);
	logger::debug("SKSE core APIs initialized");

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
