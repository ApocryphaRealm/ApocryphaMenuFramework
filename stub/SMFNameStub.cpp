// !ApocryphaMenuFramework-SMFName.dll - THE SKSE MENU FRAMEWORK PLUGIN-NAME STUB (2.2.1).
//
// Its ONLY job: tell SKSE there is a plugin named "SKSEMenuFramework", version 3.14.0. A mod's Papyrus that checks for
// SKSE Menu Framework with
//     return SKSE.GetPluginVersion("SKSEMenuFramework") > -1
// got -1 under Apocrypha Menu Framework, which registers as "ApocryphaMenuFramework" (hirato's Nexus report, 2026-10-15).
// SKSE answers GetPluginVersion by the name a plugin DECLARES (its SKSEPlugin_Version data on AE and 1.7, its
// SKSEPlugin_Query answer on SE 1.5.97), not by its file name - AMF's own file is !ApocryphaMenuFramework.dll and is known
// to SKSE as "ApocryphaMenuFramework". SKSE's own source is not on this machine, so that is proven in game (the 2.2.1
// hand-off's Papyrus check), not read. Declaring the name is the whole fix. The menu itself is AMF; nothing here draws,
// hooks, exports or forwards anything.
//
// The owner approved this as an exception to the decision that AMF never ships a second binary standing in for SKSE
// Menu Framework (4. plans\ApocryphaMenuFramework\identity-and-load-order-decisions.md section 1), 2026-10-15: "do the
// stub since flick only has one version to call". The section's constraints still hold:
//   * its OWN file name, never SKSEMenuFramework.dll - it can never collide with or overwrite the real one;
//   * no SKSE Menu Framework code and no exports beyond SKSE's three entry points (written fresh from SKSE's public plugin
//     interface; nothing here comes from SMF's source or binary);
//   * the FOMOD installs it only while SKSE\Plugins\SKSEMenuFramework.dll is MISSING (fomod\ModuleConfig.xml).
// Version 3.14.0 is SMF's own declared version, the one the owner named. The file's own version resource carries AMF's
// version, because it is part of AMF's build.
//
// No CommonLibSSE: the stub reads no game address and no game structure, so it declares itself independent of the
// runtime (signature-style address independence + no struct use) and loads on SE 1.5.97, AE 1.6.x and 1.7.x alike, with
// no Address Library file needed.

#include <cstdint>

namespace
{
	// SKSE's plugin version packing (major.minor.patch.build -> 8.8.12.4 bits).
	constexpr std::uint32_t Pack(std::uint32_t a_major, std::uint32_t a_minor, std::uint32_t a_patch, std::uint32_t a_build = 0)
	{
		return ((a_major & 0xFF) << 24) | ((a_minor & 0xFF) << 16) | ((a_patch & 0xFFF) << 4) | (a_build & 0xF);
	}

	constexpr char          kDeclaredName[] = "SKSEMenuFramework";
	constexpr std::uint32_t kDeclaredVersion = Pack(3, 14, 0);
	static_assert(kDeclaredVersion == 0x030E0000, "3.14.0 packs to 0x030E0000");

	// SKSE 1.5.97's query interface: SKSEPlugin_Query fills this in.
	struct PluginInfo
	{
		std::uint32_t infoVersion;   // 1
		const char*   name;
		std::uint32_t version;
	};

	// SKSE AE / 1.7's SKSEPlugin_Version data (dataVersion 1), laid out as SKSE reads it.
	struct PluginVersionData
	{
		std::uint32_t dataVersion;
		std::uint32_t pluginVersion;
		char          pluginName[256];
		char          author[256];
		char          supportEmail[252];
		std::uint32_t versionIndependenceEx;   // bit 0: uses no game structures
		std::uint32_t versionIndependence;     // bit 1: address independent (needs no Address Library)
		std::uint32_t compatibleVersions[16];
		std::uint32_t xseMinimum;
	};
	static_assert(sizeof(PluginVersionData) == 0x350, "SKSE reads 0x350 bytes");

	constexpr std::uint32_t kNoStructUse = 1u << 0;    // versionIndependenceEx
	constexpr std::uint32_t kSignatures = 1u << 1;     // versionIndependence: needs no Address Library file

	constexpr PluginVersionData MakeVersionData()
	{
		PluginVersionData d{};
		d.dataVersion = 1;
		d.pluginVersion = kDeclaredVersion;
		for (std::uint32_t i = 0; i < sizeof(kDeclaredName) && i < sizeof(d.pluginName); ++i) { d.pluginName[i] = kDeclaredName[i]; }
		constexpr char kAuthor[] = "ApocryphaRealm (Apocrypha Menu Framework)";
		for (std::uint32_t i = 0; i < sizeof(kAuthor) && i < sizeof(d.author); ++i) { d.author[i] = kAuthor[i]; }
		d.versionIndependenceEx = kNoStructUse;
		d.versionIndependence = kSignatures;
		return d;
	}
}

extern "C"
{
	// AE 1.6.x and 1.7.x: read by SKSE before the DLL's code runs.
	__declspec(dllexport) constinit PluginVersionData SKSEPlugin_Version = MakeVersionData();

	// SE 1.5.97: SKSE asks the plugin for its name and version.
	__declspec(dllexport) bool SKSEPlugin_Query(const void* /*a_skse*/, PluginInfo* a_info)
	{
		if (!a_info) { return false; }
		a_info->infoVersion = 1;
		a_info->name = kDeclaredName;
		a_info->version = kDeclaredVersion;
		return true;
	}

	// Every runtime: nothing to do. Returning true is what keeps the declared name registered.
	__declspec(dllexport) bool SKSEPlugin_Load(const void* /*a_skse*/)
	{
		return true;
	}
}
