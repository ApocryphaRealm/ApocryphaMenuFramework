#pragma once

// ============================================================================================
// EXPERIMENTAL (exp/mcm-loader), phase 3 - the owner, 2026-10-04: "do phase 3 for the SkyUI
// script menus". Plan: D:\Claude output\4. plans\amf-mcm-loader\PLAN.md, "Phase 3".
//
// SkyUI menus written only in Papyrus (a quest script extending SKI_ConfigBase, no MCM Helper
// config.json) drawn as AMF pages. AMF stands in for SkyUI's config manager: it makes the same
// calls on the mod's own script (OpenConfig, SetPage, SelectOption, RequestSliderDialogData /
// SetSliderValue, ... CloseConfig), one at a time, and reads the page from the option arrays
// the script fills (_optionFlagsBuf, _textBuf, _strValueBuf, _numValueBuf). Nothing of the
// mod's is patched. Menu lists and message boxes reach only the Journal's Flash panel
// (UI.InvokeStringA), so that one SKSE native is forwarded through a recorder while the Journal
// is closed - the original still runs every time.
// ============================================================================================

#include <cstddef>
#include <string>
#include <vector>

namespace mcmloader::scripts
{
	// kPostLoadGame / kNewGame: the last game's script objects are dropped and every entry is hidden until discovery
	// finds it in this game (passes over the first 40 s).
	void OnGameLoaded();

	// Main thread: find this game's script-only SkyUI configs, register or show their entries. Idempotent.
	// Does nothing while [MCM] bLoadSkyUIScriptMenus=0.
	void Discover();

	// Every frame, right after ImGui::NewFrame: CloseConfig for the config whose entry stopped being drawn.
	void Frame();

	// The settings-page switch. The caller saves the INI. Off: entries hidden, the open config closed, the native
	// recorder taken out again.
	void SetEnabled(bool a_on);

	// For the SkyUI-list switch (McmLoader's SyncSkyUI, main thread): every config found in this game.
	struct HideTarget
	{
		RE::BSTSmartPointer<RE::BSScript::Object> config;
		std::string modName;  // the ModName property, as the config registers itself with
		std::size_t index;
	};
	std::vector<HideTarget> HideTargets();
	void SetHidden(std::size_t a_index, bool a_hidden);
	int Hidden();
	int Count();

	// DevBench amf.mcm op=skyui (rules 31 and 64): action list | open | page | options | select | slider | menu |
	// menuoptions | color | key | input | default | info | answer | close. Thread-safe.
	std::string ToolJson(const std::string& a_argsJson);
}
