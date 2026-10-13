#pragma once

// AMF-Host-OAR - the frame logic with no game and no SKSE in it, so tools\host_oar_check.cpp runs the same code offline
// against a real Dear ImGui 1.89.6 context. Copyright (c) ApocryphaRealm. GPL-3.0-or-later.
//
// Called once a frame with Open Animation Replacer's ImGuiContext, between two of OAR's frames. It checks the context
// (FINDINGS section 5, points 2-4), reports whether OAR's main window is open and wants the mouse, and - while AMF's hosted
// page is up - moves that window into AMF's rectangle; when hosting ends it puts the window back where it was. A non-empty
// Result::fail means: stop hosting for the session, touch nothing more. The caller's own current context is put back.

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

#include "AMF/API.h"

namespace hostlogic
{
	struct State
	{
		int lastFrameCount = -1;
		bool saved = false;          // OAR's own window position, kept to put it back when hosting ends
		ImVec2 savedPos, savedSize;
		bool rectWarned = false;
		int lastOpen = -2;
		std::uint32_t lastThread = 0;   // the render thread AMF named last call (only for the "moved" log line)
		std::vector<std::string> keptLogged;   // OAR's other windows already logged as kept inside, this hosting session
	};

	struct Result
	{
		std::string fail;                // non-empty: fail closed, with this reason
		std::vector<std::string> info;   // lines for the log
		std::vector<std::string> warn;
	};

	inline bool Finite(const ImVec2& a_v) { return std::isfinite(a_v.x) && std::isfinite(a_v.y); }

	// One axis of a window of a_size kept inside [a_min, a_min + a_room]; one larger than the room starts at its near edge.
	inline float KeepAxis(float a_pos, float a_size, float a_min, float a_room)
	{
		if (a_size >= a_room) { return a_min; }
		return std::clamp(a_pos, a_min, a_min + a_room - a_size);
	}

	inline bool Fits(const ImVec2& a_size, const ImVec2& a_room) { return a_size.x <= a_room.x + 0.5f && a_size.y <= a_room.y + 0.5f; }

	// 3b. OAR'S OTHER WINDOWS (2.2.0). OAR's own Settings window (its "Settings >" button), and any other top-level window,
	// popup or menu it opens, opened where OAR put it - over AMF's frame and partly off screen (v6 run). While hosting, each
	// one that showed in OAR's last frame is moved inside AMF's panel; one that cannot fit there is kept inside the display.
	// Never hidden. A plain resizable window larger than the panel is shrunk to it (it scrolls, as ImGui windows do); a popup,
	// a menu or a self-sizing window is never resized. Child windows move with their parent, and tooltips are left to ImGui,
	// which places each one inside the display itself every frame. The same fail-closed rule as the main window: a window
	// whose position or size is not a finite number stops hosting for the session (the returned reason; empty = fine).
	inline std::string KeepOthersInside(ImGuiContext* a_ctx, ImGuiWindow* a_main, const ImVec2& a_panelPos, const ImVec2& a_panelSize,
										const ImVec2& a_display, State& a_s, Result& a_r)
	{
		for (ImGuiWindow* w : a_ctx->Windows)
		{
			if (!w || w == a_main || !w->Active || w->Hidden || w->IsFallbackWindow) { continue; }
			if ((w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Tooltip)) != 0) { continue; }
			const char* name = w->Name ? w->Name : "";
			if (!Finite(w->Pos) || !Finite(w->SizeFull))
			{
				return std::format("OAR's window \"{}\" has a position or size that is not a number", name);
			}
			const bool popup = (w->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_ChildMenu)) != 0;
			const bool selfSizing = (w->Flags & (ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize)) != 0;
			ImVec2 size = w->SizeFull;
			bool resize = false;
			if (!popup && !selfSizing && !Fits(size, a_panelSize))
			{
				size = ImVec2(std::min(size.x, a_panelSize.x), std::min(size.y, a_panelSize.y));
				resize = true;
			}
			const bool inPanel = Fits(size, a_panelSize);
			const ImVec2 roomPos = inPanel ? a_panelPos : ImVec2(0.0f, 0.0f);
			const ImVec2 room = inPanel ? a_panelSize : a_display;
			const ImVec2 pos(KeepAxis(w->Pos.x, size.x, roomPos.x, room.x), KeepAxis(w->Pos.y, size.y, roomPos.y, room.y));
			if (!resize && pos.x == w->Pos.x && pos.y == w->Pos.y) { continue; }
			if (std::find(a_s.keptLogged.begin(), a_s.keptLogged.end(), name) == a_s.keptLogged.end())
			{
				a_s.keptLogged.emplace_back(name);
				a_r.info.push_back(std::format("hosting: OAR's window \"{}\" at ({}, {}) {} x {} - kept inside {} at ({}, {}) {} x {}", name, w->Pos.x,
											   w->Pos.y, w->SizeFull.x, w->SizeFull.y, inPanel ? "AMF's page" : "the display", pos.x, pos.y,
											   size.x, size.y));
			}
			if (resize) { ImGui::SetWindowSize(w, size, 0); }
			ImGui::SetWindowPos(w, pos, 0);
		}
		return {};
	}

	// 4a. THE THREAD (v6). AMF calls the bridge only from inside its present hook and names, in the frame record, the thread
	// that hook is running on this frame. The call must be on exactly that thread. v5 pinned the FIRST caller's thread
	// instead; Skyrim SE 1.5.97 draws its first frames on a short-lived thread and then moves present to another, so v5
	// switched hosting off on every launch. A move of the render thread between frames is followed (one log line), never
	// a call off AMF's named thread. a_callerThread: GetCurrentThreadId() in the game, any value in the offline check.
	inline bool CheckThread(const AMF_HostFrame* a_f, std::uint32_t a_callerThread, State& a_s, Result& a_r)
	{
		if (a_f->size < sizeof(AMF_HostFrame))
		{
			a_r.fail = "AMF's frame record has no render thread (an AMF older than this bridge)";
			return false;
		}
		if (a_f->renderThread == 0)
		{
			a_r.fail = "AMF named no render thread for this frame";
			return false;
		}
		if (a_callerThread != a_f->renderThread)
		{
			a_r.fail = std::format("called on thread {} - AMF's present hook runs on thread {} this frame", a_callerThread, a_f->renderThread);
			return false;
		}
		if (a_s.lastThread != 0 && a_s.lastThread != a_f->renderThread)
		{
			a_r.info.push_back(std::format("the game's present moved from thread {} to {} - followed (AMF's present hook names it each frame)",
										   a_s.lastThread, a_f->renderThread));
		}
		a_s.lastThread = a_f->renderThread;
		return true;
	}

	// a_imageBase / a_imageEnd: OAR's module image - its "imgui_impl_dx11" string must lie inside it.
	inline Result Frame(ImGuiContext* a_ctx, AMF_HostFrame* a_f, State& a_s, std::uintptr_t a_imageBase, std::uintptr_t a_imageEnd,
						const std::string& a_title)
	{
		Result r;
		// 2. the context is OAR's, read at the right layout
		if (!a_ctx->Initialized)
		{
			r.fail = "OAR's ImGui context reads as not initialized - not the layout this bridge was built for";
			return r;
		}
		const auto renderer = reinterpret_cast<std::uintptr_t>(a_ctx->IO.BackendRendererName);
		if (renderer < a_imageBase || renderer >= a_imageEnd || std::strcmp(a_ctx->IO.BackendRendererName, "imgui_impl_dx11") != 0)
		{
			r.fail = "OAR's ImGui context does not name OAR's own imgui_impl_dx11 renderer - not the layout this bridge was built for";
			return r;
		}
		// 4. outside OAR's NewFrame -> Render
		if (a_ctx->WithinFrameScope)
		{
			r.fail = "called inside Open Animation Replacer's own frame";
			return r;
		}

		ImGuiContext* const previous = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(a_ctx);
		ImGuiWindow* window = ImGui::FindWindowByName(a_title.c_str());
		const bool drew = a_ctx->FrameCount != a_s.lastFrameCount;   // OAR draws no frame at all while none of its windows shows
		a_s.lastFrameCount = a_ctx->FrameCount;
		const bool open = window && window->Active && drew;
		a_f->targetOpen = open ? 1 : 0;
		a_f->wantsMouse = (open && a_ctx->IO.WantCaptureMouse) ? 1 : 0;
		if (static_cast<int>(open) != a_s.lastOpen)
		{
			a_s.lastOpen = open;
			r.info.push_back(std::format("Open Animation Replacer's menu {}", open ? "is open" : "is closed"));
		}

		if (a_f->hosting && window)
		{
			// 3. the window and the rectangle
			if (std::strcmp(window->Name, a_title.c_str()) != 0 || !Finite(window->Pos) || !Finite(window->SizeFull))
			{
				ImGui::SetCurrentContext(previous);
				r.fail = "OAR's main window is not what this bridge expects (title, position or size)";
				return r;
			}
			const ImVec2 display = a_ctx->IO.DisplaySize;
			const bool rectOk = std::isfinite(a_f->x) && std::isfinite(a_f->y) && std::isfinite(a_f->w) && std::isfinite(a_f->h) &&
								a_f->w >= 50.0f && a_f->h >= 50.0f && a_f->x >= -1.0f && a_f->y >= -1.0f &&
								a_f->x + a_f->w <= display.x + 1.0f && a_f->y + a_f->h <= display.y + 1.0f;
			if (!rectOk)
			{
				if (!a_s.rectWarned)
				{
					a_s.rectWarned = true;
					r.warn.push_back(std::format("AMF's rectangle ({}, {}, {} x {}) does not fit OAR's display ({} x {}) - not placed", a_f->x, a_f->y,
												 a_f->w, a_f->h, display.x, display.y));
				}
			}
			else
			{
				if (!a_s.saved)
				{
					a_s.saved = true;
					a_s.savedPos = window->Pos;
					a_s.savedSize = window->SizeFull;
					r.info.push_back(std::format("hosting: OAR's window was at ({}, {}) {} x {} - moved into AMF's page ({}, {}) {} x {}", a_s.savedPos.x,
												 a_s.savedPos.y, a_s.savedSize.x, a_s.savedSize.y, a_f->x, a_f->y, a_f->w, a_f->h));
				}
				ImGui::SetWindowPos(window, ImVec2(a_f->x, a_f->y), 0);
				ImGui::SetWindowSize(window, ImVec2(a_f->w, a_f->h), 0);
				a_f->placed = 1;
				// 3b. its other windows (Settings, popups) inside the panel too, once OAR has drawn a frame to read them from
				if (drew)
				{
					const std::string why = KeepOthersInside(a_ctx, window, ImVec2(a_f->x, a_f->y), ImVec2(a_f->w, a_f->h), display, a_s, r);
					if (!why.empty())
					{
						ImGui::SetCurrentContext(previous);
						r.fail = why;
						return r;
					}
				}
			}
		}
		else if (!a_f->hosting && a_s.saved)
		{
			// hosting ended: OAR's window goes back where its player had it (its other windows stay where they were kept -
			// inside the display)
			a_s.saved = false;
			a_s.keptLogged.clear();
			if (window && Finite(a_s.savedPos) && Finite(a_s.savedSize))
			{
				ImGui::SetWindowPos(window, a_s.savedPos, 0);
				ImGui::SetWindowSize(window, a_s.savedSize, 0);
				r.info.push_back(std::format("hosting ended: OAR's window put back at ({}, {}) {} x {}", a_s.savedPos.x, a_s.savedPos.y,
											 a_s.savedSize.x, a_s.savedSize.y));
			}
		}
		// 5. the caller's own current context back as it was
		ImGui::SetCurrentContext(previous);
		return r;
	}
}
