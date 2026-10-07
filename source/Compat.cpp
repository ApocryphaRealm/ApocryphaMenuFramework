#include "Compat.h"

#include "ConsumerSurface.h"
#include "Registry.h"
#include "Renderer.h"
#include "utils/Logger.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// ============================================================================================
// Export names and signatures are the inventory's master table, verbatim. Calling-convention
// notes: the consumer typedefs say __stdcall, which on x64 is identical to the default
// convention - declared plainly here. ImVec2 crosses by value except igGetCursorScreenPos,
// the inventory's one pOut case (result written through a pointer).
// ============================================================================================

#define AMF_EXPORT extern "C" __declspec(dllexport)

namespace
{
	using RenderFunction = void (*)();
	using InputEventCallback = bool (*)(RE::InputEvent*);
	using EventCallback = void (*)(int eventType);

	struct EventEntry
	{
		std::int64_t id;
		EventCallback callback;
		float priority;
	};

	struct InputEntry
	{
		std::int64_t id;
		InputEventCallback callback;
	};

	std::mutex g_eventLock;
	std::vector<EventEntry> g_eventCallbacks;
	std::vector<InputEntry> g_inputCallbacks;
	std::int64_t g_nextId = 1;
}

namespace compat
{
	void FireMenuEvent(MenuEvent a_event)
	{
		std::vector<EventEntry> snapshot;
		{
			std::scoped_lock lock(g_eventLock);
			snapshot = g_eventCallbacks;
		}

		for (const EventEntry& entry : snapshot)
		{
			entry.callback(static_cast<int>(a_event));
		}
	}

	bool DispatchInputEvent(RE::InputEvent* a_event)
	{
		std::vector<InputEntry> snapshot;
		{
			std::scoped_lock lock(g_eventLock);
			snapshot = g_inputCallbacks;
		}

		bool consumed = false;
		for (const InputEntry& entry : snapshot)
		{
			if (entry.callback(a_event))
			{
				consumed = true;
			}
		}
		return consumed;
	}
}

// --------------------------------------------------------------------------------------------
// SMF registration API (3 required + the optional unregister/version names the header probes)
// --------------------------------------------------------------------------------------------

namespace
{
	// A menu path as SKSE Menu Framework 3.18 reads it (its MenuPath::Parse): '/' separates the segments, "\/" is a
	// literal slash inside one, and an empty segment makes the whole path invalid. The first segment is the mod's menu
	// (AMF's section), the rest - joined with '/' again - its page. False for an invalid path.
	bool SplitMenuPath(std::string_view a_path, std::string& a_section, std::string& a_page)
	{
		std::vector<std::string> segments;
		std::string              segment;
		for (std::size_t i = 0; i < a_path.size(); ++i)
		{
			const char c = a_path[i];
			if (c == '\\' && i + 1 < a_path.size() && a_path[i + 1] == '/')
			{
				segment.push_back('/');
				++i;
				continue;
			}
			if (c == '/')
			{
				if (segment.empty())
				{
					return false;
				}
				segments.push_back(std::move(segment));
				segment.clear();
				continue;
			}
			segment.push_back(c);
		}
		if (segment.empty())
		{
			return false;
		}
		segments.push_back(std::move(segment));
		a_section = segments.front();
		a_page.clear();
		for (std::size_t i = 1; i < segments.size(); ++i)
		{
			a_page += (i > 1 ? "/" : "") + segments[i];
		}
		return true;
	}
}

AMF_EXPORT void AddSectionItem(const char* a_path, RenderFunction a_render)
{
	// The consumer-side idiom is SetSection("Mod Name") + AddSectionItem("Menu", cb), which
	// arrives here as one "Mod Name/Menu" path. The first segment is the section, the rest the page, read the way SMF
	// 3.18 reads it (an escaped "\/" is a slash inside a name; 2.1.4 - before, the path was split at its first raw
	// slash). Everything maps onto the native registry: section = the mod's one menu, item = a page (tabs when a mod
	// has several).
	if (!a_path || !a_render)
	{
		logger::warn("AddSectionItem refused: path={}, render={}",
					 a_path ? a_path : "<null>", static_cast<const void*>(reinterpret_cast<void*>(a_render)));
		return;
	}

	const std::string path(a_path);
	std::string section, item;
	if (!SplitMenuPath(path, section, item))
	{
		logger::warn("AddSectionItem refused: \"{}\" is not a menu path (an empty segment)", path);
		return;
	}
	if (item.empty())
	{
		item = "Settings";
	}

	registry::Register(section.c_str(), item.c_str(), a_render);
	logger::info("AddSectionItem (SMF-compat): \"{}\" -> section \"{}\", page \"{}\"", path, section, item);
}

// SKSE Menu Framework 3.18 (API version 1): rename or delete a menu entry by its path - the mod's whole menu
// ("Mod") or one page and everything under it ("Mod/Page"). NPC Preset Applier needs 3.18 (a Nexus report, 2026-10-07:
// its portraits did not show and could not be generated under AMF, which lacked these and the API version below).
AMF_EXPORT bool RenameSection(const char* a_path, const char* a_newName)
{
	std::string section, page;
	if (!a_path || !a_newName || !SplitMenuPath(a_path, section, page))
	{
		logger::warn("RenameSection (SMF-compat) refused: path=\"{}\", new name=\"{}\"", a_path ? a_path : "<null>",
					 a_newName ? a_newName : "<null>");
		return false;
	}
	std::string newName;
	std::string ignored;
	if (!SplitMenuPath(a_newName, newName, ignored) || !ignored.empty())
	{
		logger::warn("RenameSection (SMF-compat) refused: \"{}\" is not one name (escape a slash in it as \\/)", a_newName);
		return false;
	}
	return registry::Rename(section, page, newName);
}

AMF_EXPORT bool DeleteSection(const char* a_path)
{
	std::string section, page;
	if (!a_path || !SplitMenuPath(a_path, section, page))
	{
		logger::warn("DeleteSection (SMF-compat) refused: path=\"{}\"", a_path ? a_path : "<null>");
		return false;
	}
	return registry::Remove(section, page);
}

AMF_EXPORT std::int64_t RegisterInpoutEvent(InputEventCallback a_callback)  // (sic) the name consumers resolve
{
	if (!a_callback)
	{
		return 0;
	}

	std::scoped_lock lock(g_eventLock);
	const std::int64_t id = g_nextId++;
	g_inputCallbacks.push_back({ id, a_callback });
	logger::info("RegisterInpoutEvent (SMF-compat): input callback {} registered ({} total)", id, g_inputCallbacks.size());
	return id;
}

AMF_EXPORT void UnregisterInputEvent(std::uint64_t a_id)
{
	std::scoped_lock lock(g_eventLock);
	std::erase_if(g_inputCallbacks, [&](const InputEntry& e) { return e.id == static_cast<std::int64_t>(a_id); });
	logger::debug("UnregisterInputEvent (SMF-compat): id {}", a_id);
}

AMF_EXPORT std::int64_t RegisterEventPriority(EventCallback a_callback, float a_priority)
{
	if (!a_callback)
	{
		return 0;
	}

	std::scoped_lock lock(g_eventLock);
	const std::int64_t id = g_nextId++;
	g_eventCallbacks.push_back({ id, a_callback, a_priority });
	std::stable_sort(g_eventCallbacks.begin(), g_eventCallbacks.end(),
					 [](const EventEntry& a, const EventEntry& b) { return a.priority > b.priority; });
	logger::info("RegisterEventPriority (SMF-compat): event callback {} at priority {:.2f} ({} total)",
				 id, a_priority, g_eventCallbacks.size());
	return id;
}

// SKSE Menu Framework exports both forms; this one is the priority-less default, and defers to the
// prioritised implementation below rather than keeping a second copy of the bookkeeping.
AMF_EXPORT std::int64_t RegisterEvent(EventCallback a_callback) { return RegisterEventPriority(a_callback, 0.0f); }

AMF_EXPORT void UnregisterEvent(std::int64_t a_id)
{
	std::scoped_lock lock(g_eventLock);
	std::erase_if(g_eventCallbacks, [&](const EventEntry& e) { return e.id == a_id; });
	logger::debug("UnregisterEvent (SMF-compat): id {}", a_id);
}

AMF_EXPORT float GetMenuFrameworkVersion()
{
	// This reports the SKSE Menu Framework INTERFACE version we impersonate - NOT AMF's own product
	// version. It returned 1.2f with a comment saying "AMF reports its own major.minor", which was
	// wrong on both counts: the number was stale (AMF was 1.7.3 by then), and the question a
	// consumer asks here is "which SMF am I talking to", so our own number can never satisfy it.
	// A user's mod refused to appear and logged, exactly:
	//   "skse framework 1.2 found. Expected minimum version not met."
	//
	// MEASURED, not guessed - read out of the reference SMF DLL's own code bytes via its PE export
	// table (RVA 0x119190):
	//     F3 0F 10 05 A0 ED 21 00     movss xmm0, [rip+0x21EDA0]
	//     C3                          ret
	// and the float at that address is 3.7. Note SMF's FILE resource says 3.0.0.0 - a different
	// number again, and NOT the contract; do not take the version from the resource.
	//
	// Bump this when the SMF interface we mirror moves, never when AMF's own version moves.
	// 2.1.4: 3.8 - SKSE Menu Framework 3.18's own source returns 3.8f here (QTR-Modding/SKSE-Menu-Framework-3,
	// src/SKSEMenuFramework.cpp, MENU_FRAMEWORK_VERSION, 2026-09-28), and AMF now mirrors that interface: RenameSection,
	// DeleteSection and GetMenuFrameworkAPIVersion below.
	return 3.8f;
}

// SKSE Menu Framework 3.18's interface number (its MENU_FRAMEWORK_API_VERSION, 1): the check a mod needing 3.18's
// functions makes. Without this export AMF's stand-in returned 0 and such a mod took AMF for an older framework.
AMF_EXPORT std::uint32_t GetMenuFrameworkAPIVersion()
{
	return 1;
}

// --------------------------------------------------------------------------------------------
// LAUNCHER CONTROL - the three names a menu-launcher mod resolves out of the framework module.
//
// The object handed back by GetMainWindow is written to DIRECTLY by the caller: it stores true
// into IsOpen to show the menu and false to hide it. So its LAYOUT is the contract, not just its
// address - two std::atomic<bool> in this order, with nothing before them. It has static storage
// duration, so the pointer stays valid for the life of the process and a caller that holds it
// across a save load cannot end up writing into freed memory.
//
// Nothing here acts on the flags itself; PumpExternalWindow does that on the render thread, once
// per frame, which keeps every visibility change on the one thread that owns the menu's state.
// --------------------------------------------------------------------------------------------

namespace
{
	struct ExternalWindow
	{
		std::atomic<bool> IsOpen{ false };
		std::atomic<bool> BlockUserInput{ true };
	};

	ExternalWindow g_externalWindow;

	// What the flag read as last frame, so an outside write can be told apart from our own.
	bool g_externalWindowLast = false;

	std::atomic<bool> g_hotkeyEnabled{ true };
}

AMF_EXPORT void* GetMainWindow()
{
	return &g_externalWindow;
}

AMF_EXPORT bool IsAnyBlockingWindowOpened()
{
	// "Blocking" in the launcher's sense: a window is up and taking the player's input, which is
	// exactly what the framework menu does whenever it is visible - AND now also what a consumer
	// window does, since AddWindow exists. A launcher asking this question wants one truthful
	// answer for the whole process, not just for our own menu.
	return renderer::IsMainWindowVisible() || consumer::AnyBlockingWindowOpen();
}

AMF_EXPORT void SetHotkeyEnabled(bool a_enabled)
{
	const bool was = g_hotkeyEnabled.exchange(a_enabled, std::memory_order_acq_rel);
	if (was != a_enabled)
	{
		logger::info("menu toggle key {} by an external launcher (runtime only - the INI is untouched)",
			a_enabled ? "handed back" : "taken over");
	}
}

AMF_EXPORT bool IsHotkeyEnabled()
{
	return g_hotkeyEnabled.load(std::memory_order_acquire);
}

// --------------------------------------------------------------------------------------------
// CONSUMER SURFACE (1.5.3) - the exports a mod uses when it wants its OWN window, a HUD element,
// a named font or an image, rather than a page inside our menu.
//
// These were absent until now, and absence is invisible from the consumer's side: the stock
// header's wrappers read `static auto func = GetFunction<...>(name); if (func) { ... }` with no
// else, so a missing export makes the call a silent no-op. Five third-party mods were measured
// resolving AMF correctly and registering nothing for exactly this reason - they call AddWindow,
// not AddSectionItem. The bodies live in ConsumerSurface.cpp; these are the ABI edge.
// --------------------------------------------------------------------------------------------

AMF_EXPORT void* AddWindow(RenderFunction a_render)
{
	return consumer::AddWindow(a_render, nullptr);
}

AMF_EXPORT void* AddWindowWithView(RenderFunction a_render, const char* a_view)
{
	return consumer::AddWindow(a_render, a_view);
}

AMF_EXPORT std::int64_t RegisterHudElement(void (*a_callback)())
{
	return consumer::RegisterHudElement(a_callback);
}

AMF_EXPORT void UnregisterHudElement(std::uint64_t a_id)
{
	consumer::UnregisterHudElement(static_cast<std::int64_t>(a_id));
}

AMF_EXPORT void* LoadTexture(const char* a_path, ImVec2* a_outSize)
{
	return consumer::LoadTexture(a_path, a_outSize);
}

AMF_EXPORT void DisposeTexture(const char* a_path)
{
	consumer::DisposeTexture(a_path);
}

AMF_EXPORT void PushFont(const char* a_name)
{
	consumer::PushNamedFont(a_name);
}

AMF_EXPORT void PushRegular() { consumer::PushRegular(); }
AMF_EXPORT void PushSolid() { consumer::PushSolid(); }
AMF_EXPORT void PushBrands() { consumer::PushBrands(); }

// The other three font pushes SKSE Menu Framework exports. This framework builds a single atlas
// face, so like PushRegular/Solid/Brands above they push the current font - what matters to the
// frames around them is that every push has its Pop, and that is preserved. A consumer asking for
// a face we do not have gets the same look, never an unbalanced stack.
AMF_EXPORT void PushBig() { consumer::PushNamedFont("big"); }
AMF_EXPORT void PushSmall() { consumer::PushNamedFont("small"); }
AMF_EXPORT void PushDefault() { consumer::PushNamedFont("default"); }

// Named "Pop" because that is the name the consumer header resolves - it is the pop half of the
// four pushes above, not a general-purpose stack pop.
AMF_EXPORT void Pop() { consumer::PopFont(); }

namespace compat
{
	void PumpExternalWindow()
	{
		const bool external = g_externalWindow.IsOpen.load(std::memory_order_acquire);
		const bool internal = renderer::IsMainWindowVisible();

		if (external != g_externalWindowLast)
		{
			// Somebody outside wrote the flag: that is a request to open or close.
			g_externalWindowLast = external;
			if (external != internal)
			{
				logger::info("external launcher {} the framework window", external ? "opened" : "closed");
				renderer::SetMenuVisible(external);
			}
			return;
		}

		if (internal != external)
		{
			// The menu changed from the inside - the toggle key, the DevBench tool, a mod closing
			// it. Publish that so the launcher does not go on believing the menu is still up.
			g_externalWindow.IsOpen.store(internal, std::memory_order_release);
			g_externalWindowLast = internal;
		}
	}

	bool IsHotkeyEnabled()
	{
		return g_hotkeyEnabled.load(std::memory_order_acquire);
	}
}

// --------------------------------------------------------------------------------------------
// cimgui-compatible surface - the 36 ig*/ImDrawList_* names from the inventory, forwarding to
// the embedded Dear ImGui. Text family forwards va_list (the igTextDisabledV gotcha family).
// --------------------------------------------------------------------------------------------
