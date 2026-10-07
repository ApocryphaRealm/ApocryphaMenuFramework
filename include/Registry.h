#pragma once

// ============================================================================================
// M3: the page registry. Mods register named settings pages via the C API; the framework
// window's left pane lists ONE entry per mod, and a mod with several pages renders them as
// TABS inside its single menu - the project's one-menu-multiple-tabs rule implemented at the
// framework level rather than left to each mod.
// ============================================================================================

#include "AMF/API.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace registry
{
	struct Page
	{
		std::string pageName;
		// MCM loader: a std::function, so a page AMF builds itself from a data file (McmLoader) carries the mod and
		// page it draws in its closure. A consumer's plain AMF_RenderCallback is wrapped unchanged.
		std::function<void()> render;
		bool hidden = false;  // 1.8.3: AMF_SetPageVisible(false) leaves the page out of the menu until shown again
	};

	struct Entry
	{
		std::string modName;
		std::vector<Page> pages;
	};

	// Thread-safe: registration typically arrives from other plugins' load/messaging threads;
	// iteration happens on the render thread. Returns false only for null/empty arguments or a
	// duplicate (mod, page) pair - both logged.
	bool Register(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render);

	// MCM loader: the same registration for a page AMF draws itself, with no consumer DLL behind it.
	bool RegisterFn(const char* a_modName, const char* a_pageName, std::function<void()> a_render);

	// 1.8.3: hide or show one registered page (AMF_SetPageVisible). False when the (mod, page) pair is not
	// registered. The page stays registered - hiding only leaves it out of the menu's tabs.
	bool SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible);

	// 2.1.4: SKSE Menu Framework 3.18's RenameSection / DeleteSection (NPC Preset Applier, a Nexus report 2026-10-07).
	// An empty a_pageName means the mod's whole entry. Rename: the mod, or the page's LAST path segment ("Presets/Old" ->
	// "Presets/New"). Delete: the mod with every page, or the page and every page under it ("Presets" also removes
	// "Presets/Old"). False when nothing matched (or the new name is empty / already taken).
	bool Rename(const std::string& a_modName, const std::string& a_pageName, const std::string& a_newName);
	bool Remove(const std::string& a_modName, const std::string& a_pageName);

	// Render-thread snapshot access. The copy is cheap at menu scale (a handful of mods) and
	// means the render loop never holds the registration lock across user callbacks.
	std::vector<Entry> Snapshot();

	std::size_t Count();
}
