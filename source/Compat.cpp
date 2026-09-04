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

AMF_EXPORT void AddSectionItem(const char* a_path, RenderFunction a_render)
{
	// The consumer-side idiom is SetSection("Mod Name") + AddSectionItem("Menu", cb), which
	// arrives here as one "Mod Name/Menu" path - split at the FIRST slash so a menu name may
	// itself contain one. Everything maps onto the native registry: section = the mod's one
	// menu, item = a page (tabs when a mod has several).
	if (!a_path || !a_render)
	{
		logger::warn("AddSectionItem refused: path={}, render={}",
					 a_path ? a_path : "<null>", static_cast<const void*>(reinterpret_cast<void*>(a_render)));
		return;
	}

	const std::string path(a_path);
	const auto slash = path.find('/');
	const std::string section = slash == std::string::npos ? path : path.substr(0, slash);
	const std::string item = slash == std::string::npos ? std::string("Settings") : path.substr(slash + 1);

	registry::Register(section.c_str(), item.c_str(), a_render);
	logger::info("AddSectionItem (SMF-compat): \"{}\" -> section \"{}\", page \"{}\"", path, section, item);
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

AMF_EXPORT void UnregisterEvent(std::int64_t a_id)
{
	std::scoped_lock lock(g_eventLock);
	std::erase_if(g_eventCallbacks, [&](const EventEntry& e) { return e.id == a_id; });
	logger::debug("UnregisterEvent (SMF-compat): id {}", a_id);
}

AMF_EXPORT float GetMenuFrameworkVersion()
{
	// Probed optionally by the vendored header. AMF reports its own major.minor as a float.
	return 1.2f;
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

AMF_EXPORT void igTextV(const char* a_fmt, va_list a_args) { ImGui::TextV(a_fmt, a_args); }
AMF_EXPORT void igTextWrappedV(const char* a_fmt, va_list a_args) { ImGui::TextWrappedV(a_fmt, a_args); }
AMF_EXPORT void igTextDisabledV(const char* a_fmt, va_list a_args) { ImGui::TextDisabledV(a_fmt, a_args); }
AMF_EXPORT void igSetTooltipV(const char* a_fmt, va_list a_args) { ImGui::SetTooltipV(a_fmt, a_args); }

AMF_EXPORT void igSameLine(float a_offset, float a_spacing) { ImGui::SameLine(a_offset, a_spacing); }
AMF_EXPORT void igSpacing() { ImGui::Spacing(); }
AMF_EXPORT void igSeparator() { ImGui::Separator(); }
AMF_EXPORT void igSeparatorText(const char* a_label) { ImGui::SeparatorText(a_label); }
AMF_EXPORT void igIndent(float a_width) { ImGui::Indent(a_width); }
AMF_EXPORT void igUnindent(float a_width) { ImGui::Unindent(a_width); }
AMF_EXPORT void igPushItemWidth(float a_width) { ImGui::PushItemWidth(a_width); }
AMF_EXPORT void igPopItemWidth() { ImGui::PopItemWidth(); }
AMF_EXPORT bool igBeginChild_Str(const char* a_id, const ImVec2 a_size, int a_childFlags, int a_windowFlags)
{
	return ImGui::BeginChild(a_id, a_size, a_childFlags, a_windowFlags);
}
AMF_EXPORT void igEndChild() { ImGui::EndChild(); }

AMF_EXPORT bool igButton(const char* a_label, const ImVec2 a_size) { return ImGui::Button(a_label, a_size); }
AMF_EXPORT bool igCombo_Str_arr(const char* a_label, int* a_current, const char* const a_items[], int a_count, int a_popupMax)
{
	return ImGui::Combo(a_label, a_current, a_items, a_count, a_popupMax);
}
AMF_EXPORT bool igCombo_Str(const char* a_label, int* a_current, const char* a_itemsSeparatedByZeros, int a_popupMax)
{
	return ImGui::Combo(a_label, a_current, a_itemsSeparatedByZeros, a_popupMax);
}
AMF_EXPORT bool igCheckbox(const char* a_label, bool* a_value) { return ImGui::Checkbox(a_label, a_value); }
AMF_EXPORT bool igSliderFloat(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format, int a_flags)
{
	return ImGui::SliderFloat(a_label, a_value, a_min, a_max, a_format, a_flags);
}
AMF_EXPORT bool igSliderInt(const char* a_label, int* a_value, int a_min, int a_max, const char* a_format, int a_flags)
{
	return ImGui::SliderInt(a_label, a_value, a_min, a_max, a_format, a_flags);
}
AMF_EXPORT bool igInputInt(const char* a_label, int* a_value, int a_step, int a_stepFast, int a_flags)
{
	return ImGui::InputInt(a_label, a_value, a_step, a_stepFast, a_flags);
}
AMF_EXPORT bool igInputText(const char* a_label, char* a_buf, size_t a_bufSize, int a_flags, ImGuiInputTextCallback a_callback, void* a_userData)
{
	return ImGui::InputText(a_label, a_buf, a_bufSize, a_flags, a_callback, a_userData);
}
AMF_EXPORT bool igSelectable_Bool(const char* a_label, bool a_selected, int a_flags, const ImVec2 a_size)
{
	return ImGui::Selectable(a_label, a_selected, a_flags, a_size);
}
AMF_EXPORT bool igCollapsingHeader_TreeNodeFlags(const char* a_label, int a_flags)
{
	return ImGui::CollapsingHeader(a_label, a_flags);
}
AMF_EXPORT bool igInvisibleButton(const char* a_id, const ImVec2 a_size, int a_flags)
{
	return ImGui::InvisibleButton(a_id, a_size, a_flags);
}
AMF_EXPORT void igPushID_Str(const char* a_id) { ImGui::PushID(a_id); }
AMF_EXPORT void igPopID() { ImGui::PopID(); }

AMF_EXPORT bool igIsItemHovered(int a_flags) { return ImGui::IsItemHovered(a_flags); }
AMF_EXPORT bool igIsItemClicked(int a_mouseButton) { return ImGui::IsItemClicked(a_mouseButton); }
AMF_EXPORT bool igIsItemActive() { return ImGui::IsItemActive(); }
AMF_EXPORT bool igIsKeyPressed_Bool(int a_key, bool a_repeat) { return ImGui::IsKeyPressed(static_cast<ImGuiKey>(a_key), a_repeat); }

// The inventory's one pOut case: result through the pointer, never by value across the boundary.
AMF_EXPORT void igGetCursorScreenPos(ImVec2* a_out)
{
	if (a_out)
	{
		*a_out = ImGui::GetCursorScreenPos();
	}
}
AMF_EXPORT ImDrawList* igGetWindowDrawList() { return ImGui::GetWindowDrawList(); }
AMF_EXPORT float igGetFrameHeight() { return ImGui::GetFrameHeight(); }
AMF_EXPORT void ImDrawList_AddRectFilled(ImDrawList* a_self, const ImVec2 a_min, const ImVec2 a_max, ImU32 a_color, float a_rounding, int a_flags)
{
	if (a_self)
	{
		a_self->AddRectFilled(a_min, a_max, a_color, a_rounding, a_flags);
	}
}
AMF_EXPORT void ImDrawList_AddCircleFilled(ImDrawList* a_self, const ImVec2 a_center, float a_radius, ImU32 a_color, int a_segments)
{
	if (a_self)
	{
		a_self->AddCircleFilled(a_center, a_radius, a_color, a_segments);
	}
}
