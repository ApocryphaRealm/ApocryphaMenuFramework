#include "AMF/API.h"
#include "Input.h"
#include "Renderer.h"
#include "Settings.h"
#include "Theme.h"

#include "utils/Logger.h"

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
		case SKSE::MessagingInterface::kDataLoaded:
			logger::debug("kDataLoaded received");
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
	// M3 implements the registry. Refusing now (rather than pretending) means an early adopter
	// gets an honest false + a log line, not a silently dropped page.
	logger::warn("AMF_RegisterPage(\"{}\", \"{}\") called before the page registry exists (M3); refused honestly",
				 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>");

	return false;
}

AMF_API std::uint32_t AMF_GetInputMode()
{
	return settings::Get().controllerMode ? 1u : 0u;
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
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
