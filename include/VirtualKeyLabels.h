#pragma once

// ============================================================================================
// BEYOND VIRTUALKEY LABELS (2.4.2; Nexus request on the AMF page, 2026-11-09).
//
// Beyond VirtualKey (wwsme, Nexus 187350, plugin VirtualKey.dll) gives MCM hotkeys "virtual" key codes
// 100000..9999999 so they occupy no real key, and shows them as VK001, VK002 ... An MCM key row in AMF used to read
// such a code as "Pad 100001". Now:
//   - Beyond VirtualKey loaded and the code in its catalog: its own label ("VK001"), read through its native API
//     (VirtualKey_GetInterface(2) -> InterfaceV2::CopyBindings -> BindingV2::keyLabelUtf8);
//   - otherwise: "Virtual key 100001".
// The catalog is copied only when its revision changes, and the revision is asked at most once a second, so a frame
// costs a map lookup. Nothing is called while no virtual code is on screen.
//
// The declarations below are AMF's own minimal copy of the few types used, written to match the layout in
// VirtualKeyAPI.h of Beyond VirtualKey 2.1.0 (Zero-Clause BSD). They must stay binary-identical to that header's
// InterfaceV2 prefix and BindingV2.
// ============================================================================================

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace vkeylabels
{
	inline constexpr std::int32_t kFirstVirtualKey = 100000;
	inline constexpr std::int32_t kLastVirtualKey = 9999999;

	constexpr bool IsVirtualKey(std::int32_t a_code) { return a_code >= kFirstVirtualKey && a_code <= kLastVirtualKey; }

	// Beyond VirtualKey's label for a virtual code, or nullopt (not loaded, older API, not in its catalog). Any thread.
	std::optional<std::string> Label(std::int32_t a_code);

	// What a key row shows for a virtual code: the label above, else "Virtual key <code>".
	std::string Name(std::int32_t a_code);

	namespace api
	{
		inline constexpr std::uint32_t kInterfaceVersionV2 = 2;

		struct BindingV2
		{
			std::uint32_t structSize{ sizeof(BindingV2) };
			std::uint64_t id{};
			std::int32_t key{};
			std::uint32_t flags{};
			char nameUtf8[256]{};
			char keyLabelUtf8[64]{};
		};

		// Only the prefix AMF calls; the rest of the table is never touched.
		struct InterfaceV2
		{
			std::uint32_t interfaceVersion{};
			std::uint32_t structSize{};
			bool (*IsReady)(){};
			std::uint64_t (*GetBindingRevision)(){};
			std::size_t (*GetBindingCount)(){};
			bool (*GetBinding)(std::size_t, BindingV2*){};
			std::size_t (*CopyBindings)(BindingV2*, std::size_t){};
		};

		using GetInterfaceFn = const void* (*)(std::uint32_t);
	}
}
