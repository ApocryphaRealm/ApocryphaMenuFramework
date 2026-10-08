#pragma once

// THE FLICK HOST (2.1.6). The owner, 2026-10-08: "we're going to add the flick support with the wait your turn redux
// installed and see if it works" - after planning it in 4. plans\amf-flick\PLAN.md and answering its questions there
// (section 15: tab "Converted menus", suffix "(FLICK)", AMF keeps the FLICK mods when the real FLICK is installed too,
// their own play-time keys off by default, pure overlays may draw during play, one entry per mod).
//
// FLICK (Nexus 181603, the file FUCK.dll) is a menu framework: a mod finds it with GetModuleHandleW(L"FUCK.dll") and
// GetProcAddress("RequestFUCK"), gets back one C table of 259 functions, and hands it ITool objects (pages in FLICK's
// sidebar) and IWindow objects (free windows). AMF answers in its place - the SMF alias (SmfAlias.cpp) answers the name
// FUCK.dll with AMF's own module, and AMF exports RequestFUCK - so a FLICK mod draws its page inside AMF's window as
// "<name> (FLICK)", reached only through AMF: no FLICK window, no FLICK key, no FLICK pause-menu row (those are FLICK's
// own code, which is not in the game when AMF answers).
//
// AMF CONTAINS NO FLICK CODE. What it shares with FLICK is interface fact, needed to interoperate: the export name, the
// order and C types of the table's slots (FlickSlots.inc, generated from the public header by tools/gen-flick-slots.py),
// the vtable order of ITool / IWindow and the ManagedHotkey layout, declared here fresh. The ImGui values a FLICK mod
// passes are ImGui 1.92's and are translated by name to the 1.90.8 AMF draws with (FlickImGuiMap.h).

#include <cstddef>
#include <cstdint>
#include <string>

#include <imgui.h>

namespace flick
{
	// The page object a FLICK mod registers (FLICK's ITool) - the same virtual functions in the same order. AMF only ever
	// calls through a mod's object; it never constructs or destroys one.
	class Tool
	{
	public:
		virtual ~Tool() = default;
		virtual const char* PluginName() const = 0;
		virtual const char* Name() const = 0;
		virtual const char* Group() const = 0;
		virtual void        Draw() = 0;
		virtual void        RenderOverlay() = 0;
		virtual void        OnOpen() = 0;
		virtual void        OnClose() = 0;
		virtual bool        OnAsyncInput(const void* a_events) = 0;
		virtual bool        ShowInSidebar() const = 0;
	};

	// A free window a FLICK mod registers (FLICK's IWindow).
	class Window
	{
	public:
		virtual ~Window() = default;
		virtual const char* Id() const = 0;
		virtual const char* PluginName() const = 0;
		virtual const char* Title() const = 0;
		virtual void        Draw() = 0;
		virtual void        RenderOverlay() = 0;
		virtual bool        IsOpen() const = 0;
		virtual void        SetOpen(bool a_open) = 0;
		virtual int         GetFlags() const = 0;
		virtual ImVec2      GetDefaultSize() const = 0;
		virtual ImVec2      GetDefaultPos() const = 0;
		virtual bool        OnAsyncInput(const void* a_events) = 0;
	};

	// A hotkey a FLICK mod owns (FLICK's ManagedHotkey), laid out the same.
	struct Hotkey
	{
		std::uint32_t kKey = 0, gKey = 0;
		std::int32_t  kMod1 = -1, gMod1 = -1;
		std::int32_t  kMod2 = -1, gMod2 = -1;
		bool          isBinding = false;
		bool          wasTriggered = false;
		bool          waitForRelease = false;
		bool          disallowModifiers = false;
	};
	static_assert(sizeof(Hotkey) == 28, "flick::Hotkey must match FLICK's ManagedHotkey layout");

	// Read the switch ([FLICK] bHost) once, before the alias is installed: whether AMF answers FLICK's names at all.
	void Configure(bool a_host);
	bool Enabled();

	// Render thread, once per frame after the window has been drawn: a FLICK page that was open and is no longer drawn
	// gets its OnClose (FLICK's "tool deselected") - leaving the page or closing the menu alike.
	void EndFrame();

	// For the settings page and DevBench: the connected FLICK mods and their pages, and the calls AMF declined or does not
	// do yet (each logged once per mod).
	std::string StatusJson();
	struct ToolInfo
	{
		std::string plugin, name, group, entry;
		bool listed = true;
	};
	std::size_t ToolCount();
	ToolInfo ToolAt(std::size_t a_index);
	// Whether the real FLICK (FUCK.dll) is installed beside AMF - it then loads with no mods in it (the owner: AMF keeps
	// the FLICK mods and warns).
	bool RealFlickInstalled();
}

// The export a FLICK mod asks for. Returns AMF's table (version 5).
extern "C" __declspec(dllexport) void* RequestFUCK();
