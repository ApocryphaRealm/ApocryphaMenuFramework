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
	};

	struct Result
	{
		std::string fail;                // non-empty: fail closed, with this reason
		std::vector<std::string> info;   // lines for the log
		std::vector<std::string> warn;
	};

	inline bool Finite(const ImVec2& a_v) { return std::isfinite(a_v.x) && std::isfinite(a_v.y); }

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
			}
		}
		else if (!a_f->hosting && a_s.saved)
		{
			// hosting ended: OAR's window goes back where its player had it
			a_s.saved = false;
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
