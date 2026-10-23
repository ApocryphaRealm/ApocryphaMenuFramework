#include "PCH.h"

#include "Keyboard.h"
#include "KeyboardKeys.h"

#include "ArtHooks.h"

#include "Input.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace keyboard
{
	using strings::TR;

	namespace
	{
		// ---- the layout (include/KeyboardKeys.h, shared with the offline render) ---------------
		using namespace keyboardkeys;   // kRows, kActionRow, kRowCount, RowLength and the Action names

		// ---- state (render thread; the input translation runs on it too, before NewFrame) ------
		std::unordered_set<std::uint32_t> g_fieldsThisFrame;   // text-field item ids seen this frame
		std::unordered_set<std::uint32_t> g_fieldsLastFrame;
		std::uint32_t g_target = 0;      // the text field being typed into
		std::atomic<bool> g_capturing{ false };   // read by the input translation
		bool g_open = false;
		bool g_forced = false;           // AMF_ShowKeyboard(): open even outside controller mode
		bool g_shift = false;
		int g_row = 1, g_col = 0;
		std::chrono::steady_clock::time_point g_stickAt{};
		int g_stickDirX = 0, g_stickDirY = 0;
		std::uint32_t g_typed = 0, g_activations = 0;

		// ---- the mouse (2.3.0) ---------------------------------------------------------------------
		// A left press over the keyboard is taken here before ImGui sees it (HandleMouseButton): ImGui's text field lets
		// go of input on ANY left click that is not on itself, so a key clicked through ImGui closed the keyboard with
		// its field and typed nothing (the 2026-10-23 run: "text field ... lost the keyboard ... nav window now
		// ###amf-keyboard"). The key types on the release over the key it went down on, as a button does.
		KeyRect g_keyRects[kRowCount][kMaxCols]{};
		KeyRect g_windowRect{};
		bool g_windowValid = false;      // g_windowRect is the keyboard as drawn last frame
		bool g_mouseHeld = false;        // a left press of ours is down (its release is ours too)
		int g_heldRow = -1, g_heldCol = -1;
		std::uint32_t g_mouseTyped = 0;

		bool KeyAt(float a_x, float a_y, int& a_row, int& a_col)
		{
			const ImVec2 p(a_x, a_y);
			for (int r = 0; r < kRowCount; ++r)
			{
				for (int c = 0; c < RowLength(r) && c < kMaxCols; ++c)
				{
					if (g_keyRects[r][c].Contains(p)) { a_row = r; a_col = c; return true; }
				}
			}
			return false;
		}

		bool IsTextField_(std::uint32_t a_id)
		{
			return a_id != 0 && (g_fieldsLastFrame.count(a_id) != 0 || g_fieldsThisFrame.count(a_id) != 0);
		}

		bool SettingOn() { return settings::Get().onScreenKeyboard; }

		void Move(int a_dx, int a_dy)
		{
			if (a_dy != 0)
			{
				g_row = (g_row + a_dy + kRowCount) % kRowCount;
				// Keep the column inside the new row: the rows are not all the same length.
				const int len = RowLength(g_row);
				const int prevLen = RowLength((g_row - a_dy + kRowCount) % kRowCount);
				if (prevLen > 0) { g_col = static_cast<int>(static_cast<float>(g_col) / prevLen * len); }
				if (g_col >= len) { g_col = len - 1; }
				if (g_col < 0) { g_col = 0; }
			}
			if (a_dx != 0)
			{
				const int len = RowLength(g_row);
				g_col = (g_col + a_dx + len) % len;
			}
		}

		void SendChar(char a_c)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddInputCharacter(static_cast<unsigned int>(static_cast<unsigned char>(a_c)));
			++g_typed;
		}

		// A synthetic key press is RELEASED ONE FRAME LATER, never in the same call. The framework runs
		// ImGui with ConfigInputTrickleEventQueue OFF (Renderer.cpp), so a down and an up queued together
		// are applied in the same NewFrame and the key is never seen down - Back, Clear and Y did nothing
		// (the owner, 2026-09-18). The release is queued from Draw() once the press has been applied.
		struct PendingRelease { ImGuiKey key; bool ctrl; int atFrame; };
		std::vector<PendingRelease> g_pendingRelease;

		void SendKeyTap(ImGuiKey a_key, bool a_ctrl = false)
		{
			ImGuiIO& io = ImGui::GetIO();
			if (a_ctrl) { io.AddKeyEvent(ImGuiMod_Ctrl, true); }
			io.AddKeyEvent(a_key, true);
			g_pendingRelease.push_back({ a_key, a_ctrl, ImGui::GetFrameCount() + 1 });
		}

		void FlushReleases()
		{
			const int frame = ImGui::GetFrameCount();
			ImGuiIO& io = ImGui::GetIO();
			for (auto it = g_pendingRelease.begin(); it != g_pendingRelease.end();)
			{
				if (frame >= it->atFrame)
				{
					io.AddKeyEvent(it->key, false);
					if (it->ctrl) { io.AddKeyEvent(ImGuiMod_Ctrl, false); }
					it = g_pendingRelease.erase(it);
				}
				else { ++it; }
			}
		}

		void Press()
		{
			if (g_row != kActionRow)
			{
				char c = kRows[g_row][g_col];
				if (g_shift && c >= 'a' && c <= 'z') { c = static_cast<char>(c - 'a' + 'A'); }
				SendChar(c);
				// Shift is one-shot (2.3.0): it lets go after the next character, as on a phone's keyboard - it used
				// to stay on until pressed again ("qQ" then "W W" in the 2026-10-23 run).
				g_shift = false;
				return;
			}
			switch (g_col)
			{
			case kShift: g_shift = !g_shift; break;
			case kSpace: SendChar(' '); break;
			case kBack:  SendKeyTap(ImGuiKey_Backspace); break;
			case kClear:
				// Select everything through the edit state itself, then one Backspace deletes it; two
				// taps (Ctrl+A, Backspace) cannot be sequenced within one frame.
				if (ImGuiInputTextState* st = ImGui::GetInputTextState(g_target)) { st->SelectAll(); }
				SendKeyTap(ImGuiKey_Backspace);
				break;
			case kDone:  Hide(); break;
			default: break;
			}
		}

		void Close(const char* a_why)
		{
			if (g_open) { logger::debug("keyboard: closed ({})", a_why); }
			g_open = false;
			g_forced = false;
			g_capturing = false;
			g_windowValid = false;   // a held press stays ours until its release (g_mouseHeld), so ImGui never sees half a click
		}

		// Ask ImGui to put the target text field into input mode on its next frame - the same
		// thing its own "input" button (X on a pad) does, done for A here because a highlighted
		// search box is what a player expects A to open.
		void ActivateTarget()
		{
			ImGuiContext& g = *GImGui;
			g.NavNextActivateId = g_target;
			g.NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
			++g_activations;
		}

		// The current text of the target, from ImGui's own edit state (only while it is active).
		std::string TargetText()
		{
			ImGuiContext& g = *GImGui;
			if (g_target == 0 || g.ActiveId != g_target) { return {}; }
			ImGuiInputTextState* st = ImGui::GetInputTextState(g_target);
			if (!st) { return {}; }
			return std::string(st->TextA.Data ? st->TextA.Data : "", st->TextA.Data ? std::strlen(st->TextA.Data) : 0);
		}
	}

	bool IsTextField(std::uint32_t a_id)
	{
		return IsTextField_(a_id);
	}

	bool WasSubmittedLastFrame(std::uint32_t a_id)
	{
		return a_id != 0 && g_fieldsLastFrame.count(a_id) != 0;
	}

	void NoteTextField(std::uint32_t a_itemId)
	{
		if (a_itemId != 0) { g_fieldsThisFrame.insert(a_itemId); }
	}

	bool Capturing() { return g_capturing.load(std::memory_order_relaxed); }

	void Show() { g_forced = true; g_open = true; g_capturing = true; if (g_target == 0) { g_target = GImGui ? GImGui->NavId : 0; } }
	void Hide() { Close("Done / AMF_HideKeyboard"); }

	bool HandleGamepad(std::uint32_t a_mask, bool a_down)
	{
		if (!SettingOn()) { return false; }
		ImGuiContext* g = GImGui;
		if (!g) { return false; }

		// Not capturing: A on a highlighted text field opens the keyboard and starts input.
		if (!g_open)
		{
			if (a_mask == 0x1000 && a_down && IsTextField(g->NavId) && renderer::IsMainWindowVisible())
			{
				g_target = g->NavId;
				g_open = true;
				g_capturing = true;
				g_row = 1; g_col = 0;
				ActivateTarget();
				logger::debug("keyboard: opened for text field 0x{:08X}", g_target);
				return true;   // the A press is ours; ImGui would otherwise 'tweak' the field
			}
			return false;
		}

		// Capturing: the pad is the keyboard's.
		switch (a_mask)
		{
		case 0x0001: if (a_down) { Move(0, -1); } return true;   // D-pad up
		case 0x0002: if (a_down) { Move(0, +1); } return true;   // down
		case 0x0004: if (a_down) { Move(-1, 0); } return true;   // left
		case 0x0008: if (a_down) { Move(+1, 0); } return true;   // right
		case 0x1000: if (a_down) { Press(); } return true;       // A = press the key
		case 0x2000:                                              // B = back to the search box
			if (a_down)
			{
				Close("B");
				// The highlight goes back onto the text box, which stays in input mode so the
				// page's own navigation resumes from there.
				ImGui::SetNavID(g_target, g->NavLayer, 0, ImRect());
			}
			return true;
		case 0x4000: if (a_down) { g_shift = !g_shift; } return true;   // X = shift
		case 0x8000: if (a_down) { SendKeyTap(ImGuiKey_Backspace); } return true;   // Y = backspace
		default:
			return false;   // bumpers, triggers, Start: the page's
		}
	}

	bool HandleMouseButton(int a_button, bool a_down, float a_x, float a_y)
	{
		if (a_button != 0) { return false; }
		if (!a_down)
		{
			if (!g_mouseHeld) { return false; }   // the press went to ImGui, so does the release
			g_mouseHeld = false;
			int r = -1, c = -1;
			if (g_open && KeyAt(a_x, a_y, r, c) && r == g_heldRow && c == g_heldCol)
			{
				g_row = r; g_col = c;
				++g_mouseTyped;
				logger::debug("keyboard: mouse typed key {},{}", r, c);
				Press();
			}
			g_heldRow = g_heldCol = -1;
			return true;
		}
		if (!g_open || !g_windowValid || !g_windowRect.Contains(ImVec2(a_x, a_y))) { return false; }
		g_mouseHeld = true;
		g_heldRow = g_heldCol = -1;
		KeyAt(a_x, a_y, g_heldRow, g_heldCol);
		logger::debug("keyboard: mouse down at ({:.0f},{:.0f}) on key {},{} - kept from ImGui so the text field keeps input", a_x, a_y, g_heldRow, g_heldCol);
		return true;   // over the keyboard, a key or not: the text field must not see a click outside itself
	}

	bool HandleStick(std::uint32_t a_stick, float a_x, float a_y)
	{
		if (!g_open) { return false; }
		// The RIGHT stick is swallowed outright: with the target text box active the page would otherwise
		// hand it to ImGui as the value-tweak axis (the owner, 2026-09-18: lock the pad to the keyboard
		// until they exit or switch to mouse).
		if (a_stick != 0) { return true; }
		// The left stick works as a repeating D-pad: a push past the deadzone steps once, and
		// holding it repeats every 180 ms.
		constexpr float dz = 0.6F;
		const int dx = a_x > dz ? 1 : (a_x < -dz ? -1 : 0);
		const int dy = a_y > dz ? -1 : (a_y < -dz ? 1 : 0);   // y>0 is up in Skyrim's convention
		const auto now = std::chrono::steady_clock::now();
		if (dx == 0 && dy == 0) { g_stickDirX = g_stickDirY = 0; return true; }
		const bool changed = dx != g_stickDirX || dy != g_stickDirY;
		const bool due = std::chrono::duration<double, std::milli>(now - g_stickAt).count() > 180.0;
		if (changed || due)
		{
			Move(dx, dy);
			g_stickAt = now;
			g_stickDirX = dx; g_stickDirY = dy;
		}
		return true;   // swallowed either way: the stick must not also move the page's highlight
	}

	void Draw()
	{
		FlushReleases();
		// This frame's text fields are complete (the page has drawn); keep them for the input
		// translation, which runs before the next frame draws.
		g_fieldsLastFrame.swap(g_fieldsThisFrame);
		g_fieldsThisFrame.clear();

		g_windowValid = false;
		if (!SettingOn() || !renderer::IsMainWindowVisible()) { Close("setting off or window closed"); return; }
		if (!g_open) { return; }

		ImGuiContext& g = *GImGui;
		// The target lost input mode to something we did not do (a mouse click elsewhere, the
		// page rebuilt): the keyboard closes rather than typing into nothing. It is given a few
		// frames after opening, because activation lands on the frame after the request.
		static int s_graceFrames = 0;
		if (g.ActiveId == g_target) { s_graceFrames = 0; }
		else if (++s_graceFrames > 30 && !g_forced) { Close("the text field is no longer taking input"); s_graceFrames = 0; return; }

		const ImGuiIO& io = ImGui::GetIO();
		const float k = ImGui::GetFrameHeight() * 1.6F;
		const float width = keyboardkeys::WindowWidth(k);
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y - k * 0.5F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		ImGui::SetNextWindowSize(ImVec2(width, 0.0F), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.92F);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
									   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
									   ImGuiWindowFlags_NoSavedSettings;
		if (!ImGui::Begin("###amf-keyboard", nullptr, flags)) { ImGui::End(); return; }
		// In front of the framework window: the keyboard is what the player is using, and a tall
		// window otherwise covers its top rows (seen in the first proof, 2026-09-18).
		ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
		{
			const ImRect wr = ImGui::GetCurrentWindow()->Rect();
			g_windowRect = KeyRect{ wr.Min, wr.Max };
			g_windowValid = true;
		}

		ImGui::TextDisabled("%s", TR("AMF_OskHint", "D-pad moves, A types, B goes back to the box, X shift, Y backspace"));
		const keyboardkeys::ActionLabels labels{ TR("AMF_OskShift", "Shift"), TR("AMF_OskSpace", "Space"), TR("AMF_OskBack", "Back"),
												TR("AMF_OskClear", "Clear"), TR("AMF_OskDone", "Done") };
		int pressedRow = -1, pressedCol = -1;
		KeyMouse mouse;
		mouse.hover = !input::UsingController();   // a pointer left over a key does not wash it while the pad is in use
		mouse.pos = io.MousePos;
		if (g_mouseHeld) { mouse.heldRow = g_heldRow; mouse.heldCol = g_heldCol; }
		// 2.3.0: the keys' outlines follow the theme's button shape while they draw (the owner, 2026-10-23)
		arthooks::SetShapedButtons(true);
		const bool pressed = keyboardkeys::DrawRows(labels, g_row, g_col, g_shift, k, pressedRow, pressedCol, &mouse, g_keyRects);
		arthooks::SetShapedButtons(false);
		// ImGui itself never sees a left press over the keyboard now (HandleMouseButton), so this fires only for a click
		// that reached it some other way - kept so such a click still types.
		if (pressed) { g_row = pressedRow; g_col = pressedCol; Press(); }
		ImGui::End();
	}

	std::string StateJson()
	{
		std::string text = TargetText();
		std::string esc;
		for (char ch : text) { if (ch == '"' || ch == '\\') { esc += '\\'; } if (static_cast<unsigned char>(ch) >= 0x20) { esc += ch; } }
		ImGuiContext* g = GImGui;
		char buf[400];
		std::snprintf(buf, sizeof(buf), "{\"open\":%s,\"capturing\":%s,\"forced\":%s,\"target\":\"0x%08X\",\"targetActive\":%s,\"navOnTextField\":%s,\"row\":%d,\"col\":%d,\"shift\":%s,\"typed\":%u,\"mouseTyped\":%u,\"mouseHeld\":%s,\"held\":[%d,%d],\"activations\":%u,\"fields\":%zu,\"text\":\"%s\"}",
					  g_open ? "true" : "false", Capturing() ? "true" : "false", g_forced ? "true" : "false", g_target,
					  (g && g->ActiveId == g_target && g_target) ? "true" : "false", (g && IsTextField(g->NavId)) ? "true" : "false",
					  g_row, g_col, g_shift ? "true" : "false", g_typed, g_mouseTyped, g_mouseHeld ? "true" : "false", g_heldRow, g_heldCol, g_activations, g_fieldsLastFrame.size(), esc.c_str());
		return buf;
	}
}

// The generated cimgui exports call this after every text field they draw (tools/harden-cimgui.py).
void amf_NoteTextField(unsigned int a_itemId)
{
	keyboard::NoteTextField(a_itemId);
}
