#pragma once

// ============================================================================================
// THE ON-SCREEN KEYBOARD'S KEYS (2.3.0, moved out of Keyboard.cpp). The layout and the loop that draws the keys, with
// only ImGui here, so the offline render (tools\keyboard_theme_render.cpp) draws the keys with the very code the game
// runs. Keyboard.cpp keeps the state, the input and the window.
//
// Every key is an ImGui button, so the art hook draws it with the theme's Button part. The key under the D-pad cursor
// is the same button with its face in the nav blue in EVERY state - rest, under the mouse and held - so the cursor
// never drops back to the plain face when the mouse is over it (before 2.3.0 only the rest colour was swapped). The
// caller turns on the shaped outline (arthooks::SetShapedButtons) around DrawRows, so every key's outline follows the
// theme's button shape - the shape the cursor's blue face already showed (the owner, 2026-10-23).
//
// The MOUSE never reaches these buttons as a click (2.3.0, the 2026-10-23 run): ImGui's text field lets go of input on
// any left click that is not on itself (InputTextEx "Release focus when we click outside"), so a key clicked through
// ImGui closed the keyboard with its field and typed nothing. Keyboard.cpp takes left-button presses over the keyboard
// before ImGui sees them, so the caller passes the hover and held state here (KeyMouse) and gets each key's rectangle
// back (KeyRect) to hit-test the press; the colours below draw the hover and the held wash exactly as ImGui's own
// hovered / active colours would.
// ============================================================================================

#include <imgui.h>

#include <cstring>

namespace keyboardkeys
{
	// Four character rows and one row of actions. The action row is index 4; its columns are fixed so the D-pad lands on
	// them predictably.
	inline constexpr const char* kRows[4] = { "1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm-'" };
	inline constexpr int kActionRow = 4;
	enum Action : int { kShift = 0, kSpace, kBack, kClear, kDone, kActionCount };
	inline constexpr int kRowCount = 5;

	inline int RowLength(int a_row) { return a_row == kActionRow ? kActionCount : static_cast<int>(std::strlen(kRows[a_row])); }

	// The action row's labels, already translated by the caller.
	struct ActionLabels
	{
		const char* shift;
		const char* space;
		const char* back;
		const char* clear;
		const char* done;
	};

	// The widest row has 10 keys; the rectangle table is sized with room to spare.
	inline constexpr int kMaxCols = 16;

	// What the mouse is doing, from the caller (nullptr = no mouse state, as in the offline render).
	struct KeyMouse
	{
		bool hover = false;     // the pointer counts (keyboard/mouse in use)
		ImVec2 pos{};           // the pointer
		int heldRow = -1;       // the key the left button went down on, while it is down
		int heldCol = -1;
	};

	struct KeyRect
	{
		ImVec2 min{};
		ImVec2 max{};
		bool Contains(const ImVec2& a_p) const { return a_p.x >= min.x && a_p.y >= min.y && a_p.x < max.x && a_p.y < max.y; }
	};

	// A key's width for key size a_k.
	inline float KeyWidth(int a_row, int a_col, float a_k)
	{
		if (a_row != kActionRow) { return a_k; }
		switch (a_col)
		{
		case kSpace: return a_k * 4.0F;
		case kBack:
		case kClear: return a_k * 1.8F;
		default: return a_k * 1.6F;   // Shift, Done
		}
	}

	// The width of the keyboard window for key size a_k (10 keys across the widest row).
	inline float WindowWidth(float a_k)
	{
		const ImGuiStyle& st = ImGui::GetStyle();
		return 10.0f * a_k + 9.0f * st.ItemSpacing.x + st.WindowPadding.x * 2.0f;
	}

	// Draws the five rows inside the current window. a_row / a_col: the cursor. Returns true when a key was clicked and
	// gives its row and column. a_mouse: the hover / held state the caller tracks (see above); a_rects: filled with every
	// key's screen rectangle when given.
	inline bool DrawRows(const ActionLabels& a_labels, int a_row, int a_col, bool a_shift, float a_k, int& a_outRow, int& a_outCol,
		const KeyMouse* a_mouse = nullptr, KeyRect (*a_rects)[kMaxCols] = nullptr)
	{
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const ImVec4 hi = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
		const ImVec4 hoverCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
		const ImVec4 heldCol = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
		bool any = false;
		for (int r = 0; r < kRowCount; ++r)
		{
			const int len = RowLength(r);
			// centre the shorter rows
			const float rowWidth = (r == kActionRow) ? (a_k * 1.6F + a_k * 4.0F + a_k * 1.8F + a_k * 1.8F + a_k * 1.6F + 4.0F * gap) : (len * a_k + (len - 1) * gap);
			ImGui::SetCursorPosX((ImGui::GetWindowSize().x - rowWidth) * 0.5F);
			for (int c = 0; c < len; ++c)
			{
				const bool cursor = (r == a_row && c == a_col);
				const ImVec2 at = ImGui::GetCursorScreenPos();
				const KeyRect rect{ at, ImVec2(at.x + KeyWidth(r, c, a_k), at.y + a_k) };
				if (a_rects && c < kMaxCols) { a_rects[r][c] = rect; }
				const bool over = a_mouse && a_mouse->hover && rect.Contains(a_mouse->pos);
				const bool held = over && r == a_mouse->heldRow && c == a_mouse->heldCol;
				int pushed = 0;
				if (cursor)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, hi);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hi);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, hi);
					pushed = 3;
				}
				else if (held)
				{
					// the held wash: every state in the active colour, so the face is the one ImGui draws for a held button
					ImGui::PushStyleColor(ImGuiCol_Button, heldCol);
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, heldCol);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, heldCol);
					pushed = 3;
				}
				else if (over)
				{
					// hovered, also while a text field holds ImGui's active id (ImGui then hovers nothing else)
					ImGui::PushStyleColor(ImGuiCol_Button, hoverCol);
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, hoverCol);
					pushed = 2;
				}
				ImGui::PushID(r * 32 + c);
				bool pressed = false;
				if (r != kActionRow)
				{
					char label[2] = { kRows[r][c], '\0' };
					if (a_shift && label[0] >= 'a' && label[0] <= 'z') { label[0] = static_cast<char>(label[0] - 'a' + 'A'); }
					pressed = ImGui::Button(label, ImVec2(a_k, a_k));
				}
				else
				{
					switch (c)
					{
					case kShift: pressed = ImGui::Button(a_labels.shift, ImVec2(a_k * 1.6F, a_k)); break;
					case kSpace: pressed = ImGui::Button(a_labels.space, ImVec2(a_k * 4.0F, a_k)); break;
					case kBack:  pressed = ImGui::Button(a_labels.back, ImVec2(a_k * 1.8F, a_k)); break;
					case kClear: pressed = ImGui::Button(a_labels.clear, ImVec2(a_k * 1.8F, a_k)); break;
					case kDone:  pressed = ImGui::Button(a_labels.done, ImVec2(a_k * 1.6F, a_k)); break;
					default: break;
					}
				}
				ImGui::PopID();
				if (pushed) { ImGui::PopStyleColor(pushed); }
				if (pressed) { a_outRow = r; a_outCol = c; any = true; }
				if (c + 1 < len) { ImGui::SameLine(); }
			}
		}
		return any;
	}
}
