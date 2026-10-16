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
#include <vector>

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
	// DevBench op=flick show (2.1.8): list a FLICK copy the one-entry rule hid, for this session; returns its page count.
	int ShowCopy(const std::string& a_entry);
	struct ToolInfo
	{
		std::string plugin, name, group, entry;
		bool listed = true;
		std::string dll;   // the mod's DLL file name, lower case - the key of the per-mod choice below
	};
	std::size_t ToolCount();
	ToolInfo ToolAt(std::size_t a_index);
	// Whether the real FLICK (FUCK.dll) is installed beside AMF (the owner, 2026-10-08: AMF keeps the FLICK mods and warns;
	// 2.2.1: except the mods with FLICK windows, which go to FLICK by default - see THE DEFAULT below).
	bool RealFlickInstalled();

	// THE PER-MOD CHOICE (the owner, 2026-10-08: the three-way choice every converted menu system gets - "The standard logic
	// for these menu conversion projects including flick"). A FLICK mod is held here, or left to the real FLICK (FUCK.dll):
	// the name alias answers FUCK.dll per calling DLL (SmfAlias.cpp), so a mod left to FLICK gets the real one. "Both at
	// once" is not possible - a FLICK mod hands its pages to the one table it got. Keyed by the DLL's file name, lower
	// case; kept in FlickLeftToFlick.txt and read with Configure, so a change applies from the next game start.
	//
	// 2.2.1 THE DEFAULT (FlickRouteCore.h, the owner agreed 2026-10-15): with no choice made, a mod that has FLICK windows
	// - which this menu does not draw - goes to the real FLICK when FLICK is installed; every other mod stays here.
	//
	// SmfAlias, when a DLL asks for FUCK.dll: whether this menu answers it (true) or the real FLICK does (false) - decided
	// once per DLL for the session, from the choice read at start, FUCK.dll's presence and the DLL's own bytes
	// (a_module: the calling module, read for FLICK's IWindow class name).
	bool AnswerHere(const std::string& a_dllLower, const void* a_module);
	// a_shownName: the mod's name as this menu shows it - kept with the choice, because a mod left to FLICK never tells
	// this menu its name again. a_on: FLICK's own window; off: this menu - both saved as the player's choice.
	void SetLeftToFlick(const std::string& a_dllLower, bool a_on, const std::string& a_shownName = {});
	bool LeftToFlickChanged();   // a choice differs from what this session started with (restart to apply)
	// SmfAlias: a DLL asked for FUCK.dll and was answered (a_toAmf: AMF's table; false: the real FLICK, or nothing).
	void NoteConsumer(const std::string& a_dllLower, bool a_toAmf);
	struct Consumer
	{
		std::string dll;
		std::string name;    // shown name: its page group/name, or the one kept with the choice; empty when never seen
		bool toAmf = true;   // where it went this session
		bool leftToFlick = false;   // where it goes from the next start: the choice saved now, else the default (2.2.1)
		bool chosen = false;        // 2.2.1: the player picked its place (else the default applies)
		bool windows = false;       // 2.2.1: it has FLICK windows (its DLL carries the IWindow class, or it registered one here)
		// 2.1.8: the FLICK header the DLL was built with (FlickHost.cpp, AbiOfModule) - "current", "old-2026-02-27" /
		// "old-2026-04-13" / "old-2026-05-30" (2.1.9: a known old build, answered with its own header's table order),
		// "old-unknown" (an old header this menu cannot place: not
		// connected), "no-marker" (its own FLICK client); empty until the DLL asks for FLICK's table
		std::string era;
	};
	// Every FLICK mod seen this session or left to FLICK, by DLL name.
	std::vector<Consumer> Consumers();
}

// The export a FLICK mod asks for. Returns AMF's table (version 5) - or, by the CALLING module's header era (2.1.8), the
// table in a known old build's own order, or nothing for an old header this menu cannot place (FlickHost.cpp).
extern "C" __declspec(dllexport) void* RequestFUCK();
