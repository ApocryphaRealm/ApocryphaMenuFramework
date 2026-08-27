#pragma once

// ============================================================================================
// EVERY SE-vs-AE difference in this framework is a numeric literal in THIS file (survey §7.3).
// The rest of the codebase is runtime-agnostic; if a hook misbehaves on one runtime, this is
// the only place to look.
//
// Provenance discipline: each value names where it came from and how well it is corroborated.
// Nothing here was read from SKSE Menu Framework (licence rails - plan.md).
// ============================================================================================

namespace offsets
{
	// -----------------------------------------------------------------------------------------
	// DXGI present - the per-frame render hook site.
	// (SE 75461, AE 77246) + 0x9: corroborated across 18 independent repositories in the survey
	// (Wheeler, dMenu, ModExplorerMenu, equipment-cycle-hotkeys, soulsy, ImprovedCameraSE-NG...).
	// Treat as settled.
	// -----------------------------------------------------------------------------------------
	inline constexpr REL::RelocationID kPresentID{ 75461, 77246 };
	inline constexpr auto kPresentOffset = REL::Offset(0x9);

	// -----------------------------------------------------------------------------------------
	// Renderer/D3D init - the one-shot hook site where the device and swapchain become real.
	// The FUNCTION is agreed: (SE 75595, AE 77226). The OFFSET within it is genuinely disputed
	// between two shipping, working mods (survey §1.3):
	//
	//     Wheeler (BSD-3):          SE 0x9,  AE 0x275
	//     ModExplorerMenu (MIT):    SE 0x50, AE 0x2BC
	//
	// Different call sites in the same routine; both work. Rather than guess, the renderer
	// PROBES both candidates at runtime behind the byte-pattern guard: whichever site actually
	// holds an E8 call instruction gets hooked, and the winner is logged so PROGRESS.md can
	// record the empirical answer per runtime. If both match, the first (Wheeler's) wins; if
	// neither matches, the framework refuses loudly and stays inert - the game is unaffected.
	// -----------------------------------------------------------------------------------------
	// -----------------------------------------------------------------------------------------
	// BSInputDeviceManager::PollInputDevices - the input splice site (survey §2.1).
	// (SE 67315, AE 68617) + 0x7B: ModExplorerMenu (MIT) uses REL::Relocate(0x7B, 0x7B, 0x81) and
	// equipment-cycle-hotkeys (MIT) independently uses REL::Offset(0x7B) with the identical ID -
	// two unrelated shipping mods agreeing on both runtimes. Treat as settled; guarded anyway.
	// -----------------------------------------------------------------------------------------
	inline constexpr REL::RelocationID kPollInputDevicesID{ 67315, 68617 };
	inline constexpr auto kPollInputDevicesOffset = REL::Offset(0x7B);

	inline constexpr REL::RelocationID kD3DInitID{ 75595, 77226 };

	struct D3DInitCandidate
	{
		const char* origin;
		std::size_t seOffset;
		std::size_t aeOffset;
	};

	inline constexpr D3DInitCandidate kD3DInitCandidates[] = {
		{ "Wheeler (BSD-3)", 0x9, 0x275 },
		{ "ModExplorerMenu (MIT)", 0x50, 0x2BC },
	};
}
