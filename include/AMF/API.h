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
// Language (1.6.5). The language the framework's own text is showing - "english", "japanese",
// "korean", "chinese", "russian", ... - after the player's choice on the Framework Settings page
// (or the INI's sLanguage) and the game's own sLanguage. Consumer mods that ship
// Interface\Translations\<Mod>_<language>.txt read this each frame and reload their strings when
// it changes, so ONE setting drives every page. The pointer is stable for the process lifetime
// and the string only changes when the player switches language. The framework's font atlas is
// built from EVERY <anything>_<language>.txt in Interface\Translations for the active language,
// so a consumer's kana, hangul, hanzi or Cyrillic draw without the consumer touching fonts.
// --------------------------------------------------------------------------------------------
AMF_API const char* AMF_GetLanguage();

// --------------------------------------------------------------------------------------------
// ig* surface (M3): cimgui-compatible C exports generated from the PUBLIC cimgui definitions
// (github.com/cimgui/cimgui, MIT). Consumers that already resolve names like "igText",
// "igSliderFloat", "igTextDisabledV" keep working by resolving the same names from this DLL.
// Not declared here one-by-one - the generator owns that surface; this comment records intent.
// --------------------------------------------------------------------------------------------
