#include "AMF/API.h"
#include "Theme.h"

#include "utils/Logger.h"

#include <array>

// ============================================================================================
// Apocrypha Menu Framework - M0: a plugin that loads on both runtimes, logs richly, and ships
// its public contract. Rendering (M1), input (M2) and the page registry (M3) build on this.
// ============================================================================================

namespace
{
	// The keys the framework consumes while its menu is open. M0 placeholder set: the menu
	// toggle default (K, per project rule 28) plus ImGui's stock nav keys. M2 replaces this with
	// the live Controls-page bindings; the EXPORT CONTRACT is what must not change.
	//
	// DirectInput scan codes: 0x25 = K (rule 28 default), 0x0F = Tab, 0x01 = Escape,
	// 0xC8/0xD0/0xCB/0xCD = arrow keys, 0x1C = Enter.
	constexpr std::array<std::int32_t, 8> kReservedKeys{ 0x25, 0x0F, 0x01, 0xC8, 0xD0, 0xCB, 0xCD, 0x1C };

	std::uint32_t g_inputMode = 0;  // AMF_InputMode::kKeyboard until the settings page (M2) owns it

	void SKSEMessageListener(SKSE::MessagingInterface::Message* a_msg)
	{
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kDataLoaded:
			// M1 hooks the renderer here. Deliberately NOT during SKSEPluginLoad: resolving
			// relocated addresses that early is exactly what the DEM crash investigation
			// (2026-08-27) taught this project to avoid.
			logger::debug("kDataLoaded received; renderer hook lands here in M1");
			theme::Apply();
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
	return "1.0.0";
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
	return g_inputMode;
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
	logger::info("Successfully loaded!");

	return true;
}
