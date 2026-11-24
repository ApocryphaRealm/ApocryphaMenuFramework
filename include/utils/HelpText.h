#pragma once

// 2.5.0 (polish B1): a help paragraph under a control.
//
// AMF's own settings pages explain nearly every control with a paragraph beneath it - sixty-four of them - and until now
// each was drawn in the same colour and weight as the control's own label, so a page read as a wall of text with the
// controls lost inside it (a Nexus comment, 2026-11: too much wording). Every theme already carries a quieter Help
// colour (the "help" role: Settings > Appearance > Colours, a theme's sTextHelp); it was used for marks and page notes
// only. Help paragraphs are drawn in it, so the labels and values stand out and the explanation sits behind them. No
// wording changes, so no translation changes either.
//
// A NOTICE - something the player has to know now, such as "FLICK is not installed" - stays at full brightness and is
// drawn with ImGui::TextWrapped as before; against dimmed help it now stands out as it should.

#include "Theme.h"

#include <imgui.h>

namespace widgets
{
	inline void HelpPara(const char* a_text)
	{
		if (!a_text) { return; }
		ImGui::PushStyleColor(ImGuiCol_Text, static_cast<ImU32>(theme::HelpTextColor()));
		ImGui::TextWrapped("%s", a_text);
		ImGui::PopStyleColor();
	}
}
