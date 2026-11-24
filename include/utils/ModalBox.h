#pragma once

// 2.5.0 (polish A2 / A7 / B18): what every modal box of this menu shares - a mod's question, the Mod Function Menu
// reply, the rename box, the FLICK crash notice.
//
// WHERE IT OPENS. Dear ImGui centres a modal the first time it is ever shown and then remembers where it was: the
// position goes into the layout file, and every later box opens at that remembered spot. A player who once dragged a
// question into a corner - or whose first one appeared while the menu was small - got every box there from then on
// (the saved layout held "[##skyuiquestion] Pos=1346,793"). So the boxes are no longer saved, and each one is centred
// on the menu's own window every time it appears. It can still be dragged while it is up.
//
// CANCEL. Escape and B (the pad's cancel) answer "no" / close the box where the box has such an answer. Renderer.cpp
// no longer reads that Escape as "close the menu" (NavSide.h EscapeIsForAModal).

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace modalbox
{
	inline constexpr ImGuiWindowFlags kFlags =
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings;

	// Just before BeginPopupModal: centre the box on the menu's window (the screen's centre when there is none).
	inline void CenterNext()
	{
		ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
		if (const ImGuiContext* const g = GImGui; g && g->CurrentWindow && g->CurrentWindow->RootWindow)
		{
			centre = g->CurrentWindow->RootWindow->Rect().GetCenter();
		}
		ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	}

	// Inside the box: Escape or B pressed this frame. a_typing: a text field of the box is being typed in, or the
	// on-screen keyboard is up - then the press belongs to that, not to the box.
	inline bool CancelPressed(bool a_typing = false)
	{
		if (a_typing) { return false; }
		return ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
	}

	// One width for a row of answer buttons: the widest label, and never narrower than six letters' worth.
	inline float ButtonWidth(std::initializer_list<const char*> a_labels)
	{
		float width = ImGui::GetFontSize() * 6.0f;
		for (const char* label : a_labels)
		{
			if (label) { width = std::max(width, ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f); }
		}
		return width;
	}
}
