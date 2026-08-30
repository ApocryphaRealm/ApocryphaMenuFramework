#pragma once

// ============================================================================================
// Apocrypha Menu Framework - public C API (the contract, drafted M0)
//
// Everything here is a plain C surface resolved by consumers at runtime via GetModuleHandle +
// GetProcAddress, the same consumption pattern this project's mods already use against a menu
// framework. Plain C on purpose: no C++ types cross the DLL boundary, so a consumer built with
// a different toolchain or CRT still works.
//
// ORIGINALITY RAIL: this header is designed from this project's own consumer-side needs (what
// Dragon's Eye Minimap, Local Map Upgrade and the other SMF conversions actually call for) and
// from the public cimgui definitions. It must never be informed by SKSE Menu Framework's source
// (LGPL-2.1) or binary (All Rights Reserved) - see plan.md.
// ============================================================================================

#include <cstdint>

#define AMF_API extern "C" __declspec(dllexport)

// --------------------------------------------------------------------------------------------
// Reserved keys - THE FIRST EXPORT, per the standing decision in PLANNED-MODS.md.
//
// Fills a_buffer with the DirectInput scan codes the framework currently consumes (navigation,
// activate, back, menu toggle - whatever the Controls page has them bound to right now) and
// returns how many it wrote. Called with a null buffer, returns the count required.
//
// DirectInput scan codes, NOT ImGuiKey values: RE::ButtonEvent::GetIDCode() reports scan codes,
// so that is what a consumer's keybind is stored as. Returning ImGuiKey would force every caller
// to repeat a mapping the framework is better placed to do once.
//
// Dragon's Eye Minimap 1.5.3+ already probes for an export with exactly this name and signature
// and adopts it with no code change - the name is kept even though this framework is not SMF,
// because the probe site is the contract that already exists in the wild.
// --------------------------------------------------------------------------------------------
AMF_API std::uint32_t SMF_GetReservedKeyCodes(std::int32_t* a_buffer, std::uint32_t a_capacity);

// --------------------------------------------------------------------------------------------
// Framework identity & versioning
// --------------------------------------------------------------------------------------------
AMF_API const char*   AMF_GetVersionString();  // "1.0.0"
AMF_API std::uint32_t AMF_GetAPIVersion();     // bumped ONLY on breaking C-API changes

// --------------------------------------------------------------------------------------------
// Page registration (M3 - declared now so the contract is visible from birth)
//
// A mod registers a named settings page and a render callback. The callback runs inside the
// framework's ImGui frame; within it, the mod calls the ig* surface below. Unregistering on
// plugin unload is unnecessary - the framework drops dead callbacks safely.
// --------------------------------------------------------------------------------------------
using AMF_RenderCallback = void (*)();

AMF_API bool AMF_RegisterPage(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render);

// --------------------------------------------------------------------------------------------
// Input mode (the built-in settings page owns this; exposed so mods can adapt hints/prompts)
// --------------------------------------------------------------------------------------------
enum class AMF_InputMode : std::uint32_t
{
	kKeyboard = 0,  // arrow keys belong to widgets; nav focus does NOT follow arrows
	kGamepad = 1,   // nav box and sliders driven by the pad
};

AMF_API std::uint32_t AMF_GetInputMode();

// --------------------------------------------------------------------------------------------
// ig* surface (M3): cimgui-compatible C exports generated from the PUBLIC cimgui definitions
// (github.com/cimgui/cimgui, MIT). Consumers that already resolve names like "igText",
// "igSliderFloat", "igTextDisabledV" keep working by resolving the same names from this DLL.
// Not declared here one-by-one - the generator owns that surface; this comment records intent.
// --------------------------------------------------------------------------------------------

// --------------------------------------------------------------------------------------------
// HUD widgets (1.4.4). A registered HUD callback runs EVERY frame inside the framework's ImGui
// frame, whether or not the menu is open - this is how a mod draws an always-on overlay element
// (a pointer, a ring, a readout) with the framework's renderer and theme alpha instead of its own
// D3D hook. Inside the callback, draw ONLY through the AMF_Hud* primitives below (or the ig*
// surface); never touch ImGui windows from a HUD callback - a HUD widget takes no input.
// Coordinates are screen pixels, origin top-left; colours are 0xAABBGGRR (ImGui's IM_COL32).
// The callback decides for itself whether to draw (its own enable setting, the game's HUD state).
// THREAD CONTRACT: the callback runs on the game's RENDER thread inside Present. It must be
// lock-free with respect to the main thread: never call RE::UI (IsMenuOpen/GetMenu), never build a
// BSFixedString, never look forms up by editor ID. Track menu state from a MenuOpenCloseEvent sink
// on the main thread and read atomics here; rate-limit any walk of game data. (The first consumer
// wedged the game before the main menu by calling IsMenuOpen every frame - 2026-08-29.)
// --------------------------------------------------------------------------------------------
using AMF_HudCallback = void (*)();

AMF_API bool  AMF_RegisterHudWidget(const char* a_modName, const char* a_widgetName, AMF_HudCallback a_draw);
AMF_API void  AMF_HudScreenSize(float* a_width, float* a_height);  // valid any time after D3D init
AMF_API void  AMF_HudLine(float a_x1, float a_y1, float a_x2, float a_y2, std::uint32_t a_rgba, float a_thickness);
AMF_API void  AMF_HudCircle(float a_cx, float a_cy, float a_radius, std::uint32_t a_rgba, float a_thickness, std::int32_t a_segments);
AMF_API void  AMF_HudCircleFilled(float a_cx, float a_cy, float a_radius, std::uint32_t a_rgba, std::int32_t a_segments);
AMF_API void  AMF_HudTriangleFilled(float a_x1, float a_y1, float a_x2, float a_y2, float a_x3, float a_y3, std::uint32_t a_rgba);
AMF_API void  AMF_HudRect(float a_x1, float a_y1, float a_x2, float a_y2, std::uint32_t a_rgba, float a_thickness);
AMF_API void  AMF_HudText(float a_x, float a_y, std::uint32_t a_rgba, const char* a_text, float a_size);  // a_size 0 = default
AMF_API float AMF_HudTextWidth(const char* a_text, float a_size);
