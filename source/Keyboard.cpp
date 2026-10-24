#include "PCH.h"

#include "Keyboard.h"
#include "KeyboardKeys.h"
#include "KeyboardLayouts.h"

#include "ArtHooks.h"

#include "GameField.h"
#include "Input.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "TextInputCore.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <vector>

namespace keyboard
{
	using strings::TR;

	namespace
	{
		// ---- the layout (include/KeyboardKeys.h, shared with the offline render) ---------------
		using namespace keyboardkeys;   // kActionRow, kRowCount and the Action names

		// ---- state (render thread; the input translation runs on it too, before NewFrame) ------
		std::unordered_set<std::uint32_t> g_fieldsThisFrame;   // text-field item ids seen this frame
		std::unordered_set<std::uint32_t> g_fieldsLastFrame;
		std::uint32_t g_target = 0;      // the ImGui text field being typed into
		std::atomic<bool> g_capturing{ false };   // read by the input translation (ImGui targets only)
		bool g_open = false;
		bool g_forced = false;           // AMF_ShowKeyboard(): open even outside controller mode
		bool g_shift = false;
		int g_row = 1, g_col = 0;
		std::chrono::steady_clock::time_point g_stickAt{};
		int g_stickDirX = 0, g_stickDirY = 0;
		std::uint32_t g_typed = 0, g_activations = 0;

		// ---- 2.3.0: the universal keyboard - a GAME text field as the target (GameField.h) ---------
		enum class TargetKind { kImGui, kGame };
		TargetKind g_kind = TargetKind::kImGui;
		bool g_parked = false;           // a game target after one B: the panel is away, the field keeps typing
		gamefield::Target g_gameTarget;

		// ---- 2.3.0: layouts (KeyboardLayouts.h) - LB / RB step through them -------------------------
		int g_layout = -1;               // index into kblayout::kLayouts; -1 = not chosen yet
		std::string g_layoutSetting;     // the sKeyboardLayout it was chosen from
		kblayout::HangulComposer g_hangul;
		KeyGrid g_grid;
		int g_gridLayout = -2;
		bool g_gridShift = false;

		// ---- 2.3.0: edits into an ImGui field, one step per two frames (a Backspace is released a frame later, and a
		// character queued with it would be typed BEFORE it is applied - ImGui's InputText reads the characters first) ----
		struct Step { int kind; std::string text; };   // 0 Backspace, 1 text, 2 Clear
		std::deque<Step> g_steps;
		int g_stepFrame = -100;

		// ---- DevBench commands, from the listener thread to this one --------------------------------
		struct Cmd { std::string what, arg; int row = -1, col = -1; };
		std::mutex g_cmdLock;
		std::vector<Cmd> g_cmds;
		std::mutex g_snapLock;
		std::string g_snapshot = "{}";

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
				for (int c = 0; c < g_grid.Length(r) && c < kMaxCols; ++c)
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

		const kblayout::Layout& CurrentLayout()
		{
			const std::string& want = settings::Get().keyboardLayout;
			if (g_layout < 0 || want != g_layoutSetting)
			{
				g_layoutSetting = want;
				const std::string id = (want.empty() || want == "auto") ? kblayout::LayoutForLanguage(strings::GameLanguageSetting()) : want;
				g_layout = kblayout::FindLayout(id);
				if (g_layout < 0)
				{
					logger::warn("keyboard: sKeyboardLayout '{}' is not a layout - English used", want);
					g_layout = 0;
				}
				logger::debug("keyboard: layout {} ({})", kblayout::kLayouts[g_layout].id, want);
				g_hangul.Reset();
			}
			return kblayout::kLayouts[g_layout];
		}

		const KeyGrid& Grid()
		{
			const auto& lay = CurrentLayout();
			if (g_gridLayout != g_layout || g_gridShift != g_shift)
			{
				g_gridLayout = g_layout;
				g_gridShift = g_shift;
				for (int r = 0; r < 4; ++r)
				{
					if (g_shift && lay.shifted[r]) { g_grid.rows[r] = kblayout::SplitKeys(lay.shifted[r]); }
					else
					{
						g_grid.rows[r] = kblayout::SplitKeys(lay.rows[r]);
						if (g_shift && lay.kind == kblayout::Kind::kKana)
						{
							for (auto& key : g_grid.rows[r]) { key = kblayout::Utf8(kblayout::ToKatakana(kblayout::FirstCodePoint(key))); }
						}
					}
				}
			}
			return g_grid;
		}

		void StepLayout(int a_dir)
		{
			CurrentLayout();
			g_layout = (g_layout + a_dir + kblayout::kLayoutCount) % kblayout::kLayoutCount;
			g_hangul.Reset();
			g_gridLayout = -2;
			const int len = g_row == kActionRow ? kActionCount : static_cast<int>(Grid().rows[g_row].size());
			if (g_col >= len) { g_col = len - 1; }
			logger::debug("keyboard: layout -> {}", kblayout::kLayouts[g_layout].id);
		}

		void Move(int a_dx, int a_dy)
		{
			const KeyGrid& grid = Grid();
			if (a_dy != 0)
			{
				g_row = (g_row + a_dy + kRowCount) % kRowCount;
				// Keep the column inside the new row: the rows are not all the same length.
				const int len = grid.Length(g_row);
				const int prevLen = grid.Length((g_row - a_dy + kRowCount) % kRowCount);
				if (prevLen > 0) { g_col = static_cast<int>(static_cast<float>(g_col) / prevLen * len); }
				if (g_col >= len) { g_col = len - 1; }
				if (g_col < 0) { g_col = 0; }
			}
			if (a_dx != 0)
			{
				const int len = grid.Length(g_row);
				if (len > 0) { g_col = (g_col + a_dx + len) % len; }
			}
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

		// The current text of the ImGui target, from ImGui's own edit state (only while it is active).
		std::string TargetText()
		{
			ImGuiContext& g = *GImGui;
			if (g_target == 0 || g.ActiveId != g_target) { return {}; }
			ImGuiInputTextState* st = ImGui::GetInputTextState(g_target);
			if (!st) { return {}; }
			return std::string(st->TextA.Data ? st->TextA.Data : "", st->TextA.Data ? std::strlen(st->TextA.Data) : 0);
		}

		std::string CurrentText() { return g_kind == TargetKind::kGame ? gamefield::FieldText() : TargetText(); }

		bool RunStep(const Step& a_s)
		{
			switch (a_s.kind)
			{
			case 0: SendKeyTap(ImGuiKey_Backspace); break;
			case 1: ImGui::GetIO().AddInputCharactersUTF8(a_s.text.c_str()); g_typed += static_cast<std::uint32_t>(textcore::DecodeUtf8(a_s.text).size()); break;
			case 2:
				// Select everything through the edit state itself, then one Backspace deletes it; two
				// taps (Ctrl+A, Backspace) cannot be sequenced within one frame.
				if (ImGuiInputTextState* st = ImGui::GetInputTextState(g_target)) { st->SelectAll(); }
				SendKeyTap(ImGuiKey_Backspace);
				break;
			default: break;
			}
			g_stepFrame = ImGui::GetFrameCount();
			return true;
		}

		void PumpSteps()
		{
			if (g_steps.empty() || !g_pendingRelease.empty()) { return; }
			if (ImGui::GetFrameCount() < g_stepFrame + 2) { return; }
			RunStep(g_steps.front());
			g_steps.pop_front();
		}

		// One key press as an edit: delete a_e.backspaces before the caret, then type a_e.insert - into whichever target is open.
		void Apply(const kblayout::Edit& a_e)
		{
			if (a_e.backspaces <= 0 && a_e.insert.empty()) { return; }
			if (g_kind == TargetKind::kGame)
			{
				g_typed += static_cast<std::uint32_t>(textcore::DecodeUtf8(a_e.insert).size());
				gamefield::ApplyEdit(a_e.backspaces, a_e.insert);
				return;
			}
			for (int i = 0; i < a_e.backspaces; ++i) { g_steps.push_back({ 0, {} }); }
			if (!a_e.insert.empty()) { g_steps.push_back({ 1, a_e.insert }); }
			PumpSteps();
		}

		void Clear()
		{
			g_hangul.Reset();
			if (g_kind == TargetKind::kGame)
			{
				const auto n = textcore::DecodeUtf8(gamefield::FieldText()).size();
				Apply({ static_cast<int>(n), {} });
				return;
			}
			g_steps.push_back({ 2, {} });
			PumpSteps();
		}

		void Backspace()
		{
			kblayout::Edit e;
			if (CurrentLayout().kind == kblayout::Kind::kHangul && g_hangul.Backspace(e)) { Apply(e); return; }
			Apply({ 1, {} });
		}

		// A code point typed by a key (or by DevBench's op=keyboard type): Korean jamo compose, the kana voicing marks act on
		// the character before the caret, everything else is typed as itself.
		void TypeCodePoint(char32_t a_cp)
		{
			const auto& lay = CurrentLayout();
			if (lay.kind == kblayout::Kind::kHangul && kblayout::HangulComposer::IsJamo(a_cp))
			{
				Apply(g_hangul.Feed(a_cp));
				return;
			}
			g_hangul.Reset();
			if (a_cp == kblayout::kDakuten || a_cp == kblayout::kHandakuten)
			{
				const auto cps = textcore::DecodeUtf8(CurrentText());
				if (!cps.empty())
				{
					if (const char32_t v = kblayout::KanaVoice(cps.back(), a_cp == kblayout::kHandakuten); v != 0)
					{
						Apply({ 1, kblayout::Utf8(v) });
						return;
					}
				}
			}
			Apply({ 0, kblayout::Utf8(a_cp) });
		}

		void Close(const char* a_why);
		void Finish();

		void Press()
		{
			const KeyGrid& grid = Grid();
			if (g_row != kActionRow)
			{
				if (g_col < 0 || g_col >= static_cast<int>(grid.rows[g_row].size())) { return; }
				TypeCodePoint(kblayout::FirstCodePoint(grid.rows[g_row][g_col]));
				// Shift is one-shot (2.3.0): it lets go after the next character, as on a phone's keyboard - it used
				// to stay on until pressed again ("qQ" then "W W" in the 2026-10-23 run).
				g_shift = false;
				return;
			}
			switch (g_col)
			{
			case kShift: g_shift = !g_shift; break;
			case kSpace: g_hangul.Reset(); Apply({ 0, " " }); break;
			case kBack:  Backspace(); break;
			case kClear: Clear(); break;
			case kDone:  Finish(); break;
			default: break;
			}
		}

		void Close(const char* a_why)
		{
			if (g_open) { logger::debug("keyboard: closed ({})", a_why); }
			const bool wasGame = g_kind == TargetKind::kGame;
			g_open = false;
			g_forced = false;
			g_capturing = false;
			g_parked = false;
			g_windowValid = false;   // a held press stays ours until its release (g_mouseHeld), so ImGui never sees half a click
			g_hangul.Reset();
			g_steps.clear();
			if (wasGame)
			{
				gamefield::SetClaim(gamefield::Claim::kNone);
				gamefield::PublishRect(false, 0, 0, 0, 0);
				g_kind = TargetKind::kImGui;
			}
		}

		// Done / Start: the ImGui target just closes; a game field gets Enter, then its adapter ends the input.
		void Finish()
		{
			if (g_kind == TargetKind::kGame)
			{
				g_hangul.Reset();
				gamefield::SendEnter();
				gamefield::EndInput("Done");
				Close("Done (game field)");
				return;
			}
			Close("Done / AMF_HideKeyboard");
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

		void OpenGame(const gamefield::Target& a_t)
		{
			g_kind = TargetKind::kGame;
			g_gameTarget = a_t;
			g_open = true;
			g_capturing = false;   // the ImGui translation is not involved: the game thread hands us the pad (GameField.h)
			g_parked = false;
			g_forced = a_t.explicitOpen;
			g_row = 1; g_col = 0;
			g_shift = false;
			g_hangul.Reset();
			g_steps.clear();
			gamefield::SetClaim(gamefield::Claim::kFull);
			logger::debug("keyboard: opened for game field {} in {} (adapter {})", a_t.path, a_t.menu.empty() ? a_t.url : a_t.menu, a_t.adapter);
		}

		// The pad while the keyboard is open, for both kinds of target. True = the press was the keyboard's.
		bool HandleOpenPad(std::uint32_t a_mask, bool a_down)
		{
			const bool game = g_kind == TargetKind::kGame;
			if (game && g_parked)
			{
				// parked: A brings the panel back, B again ends the input (the menu's own B works after that)
				if (a_mask == 0x1000 && a_down) { g_parked = false; gamefield::SetClaim(gamefield::Claim::kFull); logger::debug("keyboard: back from parked (A)"); }
				if (a_mask == 0x2000 && a_down) { gamefield::EndInput("B twice"); Close("B twice (game field)"); }
				return true;
			}
			switch (a_mask)
			{
			case 0x0001: if (a_down) { Move(0, -1); } return true;   // D-pad up
			case 0x0002: if (a_down) { Move(0, +1); } return true;   // down
			case 0x0004: if (a_down) { Move(-1, 0); } return true;   // left
			case 0x0008: if (a_down) { Move(+1, 0); } return true;   // right
			case 0x1000: if (a_down) { Press(); } return true;       // A = press the key
			case 0x2000:                                              // B = back to the box
				if (a_down)
				{
					if (game)
					{
						// the field keeps typing (a PC keyboard still works); the pad goes back to the menu, but A and B
						g_parked = true;
						g_windowValid = false;
						gamefield::SetClaim(gamefield::Claim::kParked);
						gamefield::PublishRect(false, 0, 0, 0, 0);
						logger::debug("keyboard: parked (B) - A reopens, B again ends the input");
						return true;
					}
					Close("B");
					// The highlight goes back onto the text box, which stays in input mode so the
					// page's own navigation resumes from there.
					if (GImGui) { ImGui::SetNavID(g_target, GImGui->NavLayer, 0, ImRect()); }
				}
				return true;
			case 0x4000: if (a_down) { g_shift = !g_shift; } return true;   // X = shift
			case 0x8000: if (a_down) { Backspace(); } return true;          // Y = backspace
			// 2.3.0 (owner decision D5): LT = Backspace, RT = Space, LB / RB = previous / next layout
			case 0x0009: if (a_down) { Backspace(); } return true;
			case 0x000A: if (a_down) { g_hangul.Reset(); Apply({ 0, " " }); } return true;
			case 0x0100: if (a_down) { StepLayout(-1); } return true;
			case 0x0200: if (a_down) { StepLayout(+1); } return true;
			case 0x0010:   // Start: Done on a game field (in AMF's window Start closes the menu before it reaches here)
				if (game) { if (a_down) { Finish(); } return true; }
				return false;
			default:
				return game;   // a game menu sees no pad button while the keyboard is up; in AMF's window the rest is the page's
			}
		}

		std::string EscapeJson(const std::string& a_s)
		{
			std::string esc;
			for (char ch : a_s) { if (ch == '"' || ch == '\\') { esc += '\\'; } if (static_cast<unsigned char>(ch) >= 0x20) { esc += ch; } }
			return esc;
		}

		void RunCommands()
		{
			std::vector<Cmd> cmds;
			{
				std::scoped_lock l(g_cmdLock);
				cmds.swap(g_cmds);
			}
			for (const Cmd& c : cmds)
			{
				logger::debug("keyboard: devbench {} '{}' {},{}", c.what, c.arg, c.row, c.col);
				if (c.what == "close") { if (g_kind == TargetKind::kGame) { gamefield::EndInput("devbench close"); } Close("devbench"); continue; }
				if (c.what == "open")
				{
					if (renderer::IsMainWindowVisible()) { Show(); }
					continue;
				}
				if (!g_open) { continue; }
				if (c.what == "press")
				{
					if (c.row >= 0 && c.row < kRowCount && c.col >= 0 && c.col < Grid().Length(c.row)) { g_row = c.row; g_col = c.col; Press(); }
				}
				else if (c.what == "type")
				{
					for (const char32_t cp : textcore::DecodeUtf8(c.arg)) { TypeCodePoint(cp); }
				}
				else if (c.what == "key")
				{
					if (c.arg == "back") { Backspace(); }
					else if (c.arg == "space") { g_hangul.Reset(); Apply({ 0, " " }); }
					else if (c.arg == "clear") { Clear(); }
					else if (c.arg == "shift") { g_shift = !g_shift; }
					else if (c.arg == "enter" || c.arg == "done") { Finish(); }
					else if (c.arg == "b") { HandleOpenPad(0x2000, true); }
					else if (c.arg == "a") { HandleOpenPad(0x1000, true); }
				}
				else if (c.what == "layout")
				{
					if (c.arg == "next") { StepLayout(+1); }
					else if (c.arg == "prev") { StepLayout(-1); }
					else if (const int i = kblayout::FindLayout(c.arg); i >= 0) { CurrentLayout(); g_layout = i; g_gridLayout = -2; g_hangul.Reset(); }
				}
			}
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

	void Show()
	{
		if (g_kind == TargetKind::kGame) { Close("AMF_ShowKeyboard over a game field"); }
		g_forced = true; g_open = true; g_capturing = true;
		if (g_target == 0) { g_target = GImGui ? GImGui->NavId : 0; }
	}
	void Hide() { Close("Done / AMF_HideKeyboard"); }

	bool HandleGamepad(std::uint32_t a_mask, bool a_down)
	{
		if (!SettingOn()) { return false; }
		ImGuiContext* g = GImGui;
		if (!g) { return false; }
		if (g_open && g_kind == TargetKind::kGame) { return false; }   // the game thread hands a game target its pad

		// Not capturing: A on a highlighted text field opens the keyboard and starts input.
		if (!g_open)
		{
			if (a_mask == 0x1000 && a_down && IsTextField(g->NavId) && renderer::IsMainWindowVisible())
			{
				g_kind = TargetKind::kImGui;
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
		return HandleOpenPad(a_mask, a_down);
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
		if (g_kind == TargetKind::kGame && g_parked) { return false; }
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

		RunCommands();

		// 2.3.0: the game half - its close and open requests, then the pad the game thread claimed for it
		{
			std::string why;
			if (gamefield::TakeCloseRequest(why) && g_open && g_kind == TargetKind::kGame) { Close(why.c_str()); }
			gamefield::Target t;
			if (gamefield::TakeOpenRequest(t))
			{
				if (SettingOn() && settings::Get().keyboardGameFields && !renderer::IsMainWindowVisible()) { OpenGame(t); }
				else { gamefield::SetClaim(gamefield::Claim::kNone); }
			}
			std::vector<gamefield::PadEvent> pad;
			gamefield::DrainPad(pad);
			for (const auto& e : pad)
			{
				if (!g_open || g_kind != TargetKind::kGame) { break; }
				if (e.kind == 0) { HandleOpenPad(e.code, e.down); }
				else if (e.kind == 1) { HandleStick(e.code, e.x, e.y); }
				else if (e.kind == 2) { HandleMouseButton(0, e.down, e.x, e.y); }
			}
		}

		g_windowValid = false;
		if (g_kind == TargetKind::kGame)
		{
			if (g_open && (!SettingOn() || !settings::Get().keyboardGameFields || renderer::IsMainWindowVisible()))
			{
				gamefield::EndInput("setting off or AMF's window opened");
				Close("setting off or AMF's window opened");
			}
		}
		else if (!SettingOn() || !renderer::IsMainWindowVisible()) { Close("setting off or window closed"); }

		// the state for DevBench, every frame (cheap), so the listener thread never reads this thread's variables
		{
			std::string text = CurrentText();
			const auto& lay = CurrentLayout();
			ImGuiContext* g = GImGui;
			char buf[2048];
			std::snprintf(buf, sizeof(buf), "{\"open\":%s,\"capturing\":%s,\"forced\":%s,\"targetKind\":\"%s\",\"parked\":%s,\"target\":\"0x%08X\",\"targetActive\":%s,\"navOnTextField\":%s,\"row\":%d,\"col\":%d,\"shift\":%s,\"layout\":\"%s\",\"composing\":%s,\"typed\":%u,\"mouseTyped\":%u,\"mouseHeld\":%s,\"held\":[%d,%d],\"activations\":%u,\"fields\":%zu,\"pendingSteps\":%zu,\"text\":\"%s\"}",
						  g_open ? "true" : "false", Capturing() ? "true" : "false", g_forced ? "true" : "false", g_kind == TargetKind::kGame ? "game" : "imgui",
						  g_parked ? "true" : "false", g_target, (g && g->ActiveId == g_target && g_target) ? "true" : "false", (g && IsTextField(g->NavId)) ? "true" : "false",
						  g_row, g_col, g_shift ? "true" : "false", lay.id, g_hangul.Composing() ? "true" : "false", g_typed, g_mouseTyped, g_mouseHeld ? "true" : "false",
						  g_heldRow, g_heldCol, g_activations, g_fieldsLastFrame.size(), g_steps.size(), EscapeJson(text).c_str());
			std::scoped_lock l(g_snapLock);
			g_snapshot = buf;
		}

		if (!g_open) { return; }

		ImGuiContext& g = *GImGui;
		if (g_kind == TargetKind::kImGui)
		{
			// The target lost input mode to something we did not do (a mouse click elsewhere, the
			// page rebuilt): the keyboard closes rather than typing into nothing. It is given a few
			// frames after opening, because activation lands on the frame after the request.
			static int s_graceFrames = 0;
			if (g.ActiveId == g_target) { s_graceFrames = 0; }
			else if (++s_graceFrames > 30 && !g_forced) { Close("the text field is no longer taking input"); s_graceFrames = 0; return; }
			PumpSteps();
		}
		else if (g_parked) { return; }   // the panel is away while parked; the field keeps its text

		const ImGuiIO& io = ImGui::GetIO();
		const KeyGrid& grid = Grid();
		const float scale = settings::Get().keyboardScale;
		const float k = ImGui::GetFrameHeight() * 1.6F * (scale >= 0.5F && scale <= 2.0F ? scale : 1.0F);
		const float width = keyboardkeys::WindowWidth(k, grid.Widest());
		const bool top = settings::Get().keyboardPosition == 1;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, top ? k * 0.5F : io.DisplaySize.y - k * 0.5F), ImGuiCond_Always, ImVec2(0.5F, top ? 0.0F : 1.0F));
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
			if (g_kind == TargetKind::kGame) { gamefield::PublishRect(true, wr.Min.x, wr.Min.y, wr.Max.x, wr.Max.y); }
		}

		if (g_kind == TargetKind::kGame)
		{
			ImGui::TextDisabled("%s", TR("AMF_OskHintGame", "D-pad moves, A types, B goes back to the box (twice: done), X shift, Y backspace, Start done"));
		}
		else
		{
			ImGui::TextDisabled("%s", TR("AMF_OskHint", "D-pad moves, A types, B goes back to the box, X shift, Y backspace"));
		}
		ImGui::TextDisabled("%s  |  %s: %s", TR("AMF_OskHintMore", "LT backspace, RT space, LB / RB layout"), TR("AMF_OskLayout", "Layout"),
							kblayout::kLayouts[g_layout].label);
		if (g_kind == TargetKind::kGame)
		{
			// the echo: what the game's box holds, read back from it (its box may sit under the keyboard)
			ImGui::TextUnformatted((std::string(TR("AMF_OskText", "Text")) + ": " + gamefield::FieldText()).c_str());
		}
		const keyboardkeys::ActionLabels labels{ TR("AMF_OskShift", "Shift"), TR("AMF_OskSpace", "Space"), TR("AMF_OskBack", "Back"),
												TR("AMF_OskClear", "Clear"), TR("AMF_OskDone", "Done") };
		int pressedRow = -1, pressedCol = -1;
		KeyMouse mouse;
		mouse.hover = g_kind == TargetKind::kImGui ? !input::UsingController() : settings::Get().keyboardWithMouse;
		mouse.pos = io.MousePos;
		if (g_kind == TargetKind::kGame)
		{
			if (auto* cursor = RE::MenuCursor::GetSingleton()) { mouse.pos = ImVec2(cursor->GetRuntimeData().cursorPosX, cursor->GetRuntimeData().cursorPosY); }
		}
		if (g_mouseHeld) { mouse.heldRow = g_heldRow; mouse.heldCol = g_heldCol; }
		// 2.3.0: the keys' outlines follow the theme's button shape while they draw (the owner, 2026-10-23)
		arthooks::SetShapedButtons(true);
		const bool pressed = keyboardkeys::DrawGrid(labels, grid, g_row, g_col, k, pressedRow, pressedCol, &mouse, g_keyRects);
		arthooks::SetShapedButtons(false);
		// ImGui itself never sees a left press over the keyboard now (HandleMouseButton), so this fires only for a click
		// that reached it some other way - kept so such a click still types.
		if (pressed) { g_row = pressedRow; g_col = pressedCol; Press(); }
		ImGui::End();
	}

	std::string StateJson()
	{
		std::scoped_lock l(g_snapLock);
		return g_snapshot;
	}

	std::string ToolJson(const std::string& a_what, const std::string& a_arg, int a_row, int a_col)
	{
		if (a_what == "state" || a_what.empty())
		{
			return "{\"ok\":true,\"op\":\"keyboard\",\"keyboard\":" + StateJson() + ",\"game\":" + gamefield::StateJson() + "}";
		}
		if (a_what == "open" && !a_arg.empty())
		{
			// a game menu: its adapter's start gesture (startInput / showLocationFinder), then the field is found
			return gamefield::ToolJson("{\"do\":\"start\",\"menu\":\"" + a_arg + "\"}");
		}
		if (a_what == "open" && !renderer::IsMainWindowVisible())
		{
			return gamefield::ToolJson("{\"do\":\"detect\"}");
		}
		if (a_what != "open" && a_what != "close" && a_what != "press" && a_what != "type" && a_what != "key" && a_what != "layout")
		{
			return "{\"ok\":false,\"op\":\"keyboard\",\"error\":\"unknown do '" + a_what + "' (state|open [menu]|close|press row col|type text|key name|layout name)\"}";
		}
		{
			std::scoped_lock l(g_cmdLock);
			g_cmds.push_back({ a_what, a_arg, a_row, a_col });
		}
		return "{\"ok\":true,\"op\":\"keyboard\",\"do\":\"" + a_what + "\",\"note\":\"queued for the next frame; read do=state after it\"}";
	}
}

// The generated cimgui exports call this after every text field they draw (tools/harden-cimgui.py).
void amf_NoteTextField(unsigned int a_itemId)
{
	keyboard::NoteTextField(a_itemId);
}
