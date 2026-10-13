#pragma once

// ============================================================================================
// TYPING IN OTHER LANGUAGES (2.2.1; the owner, 2026-10-13: "Add typing for other languages.")
//
// A window procedure in front of the game's (ANSI, like the window - TextInput.cpp says why), live only for what AMF's
// own text boxes need. The engine makes its CharEvents from DirectInput keys through ToUnicode and never reads WM_CHAR, and its window procedure hides the IME's
// windows (TextInputCore.h has the disassembly). So while an ImGui text field in AMF's context wants text, this:
//   * feeds WM_CHAR to ImGui, read back from the input language's code page - dead keys, AltGr, SendInput unicode;
//   * takes the IME's finished text (WM_IME_COMPOSITION, GCS_RESULTSTR, read with ImmGetCompositionStringW) and feeds it,
//     without letting DefWindowProc turn it into WM_IME_CHAR / WM_CHAR again;
//   * shows the IME's own composition and candidate windows (the game's WM_IME_SETCONTEXT handler hides them) and puts
//     them at the text cursor, from ImGui's io.SetPlatformImeDataFn (replacing ImGui's default, which ran on the render
//     thread with unscaled coordinates);
//   * associates an input context if the window has none, and puts the window back as it was when the field lets go.
// With no AMF text field wanting text, every message goes to the game's procedure untouched. Every IMM call runs on the
// window's own thread: the render thread only posts a private registered message.
// ============================================================================================

#include <cstdint>
#include <string>

namespace textinput
{
	// Render thread, once, right after ImGui_ImplWin32_Init: subclasses the game window and installs the IME hook.
	// Safe to call again (does nothing); a null or dead window is logged and leaves typing on the engine's path.
	void Install(void* a_hwnd);

	// Render thread, every frame: does an AMF text field want text now (renderer::WantsTextInput's value)? Posts the
	// window thread a sync on each change - nothing is posted on a frame where nothing changed.
	void OnFrame(bool a_wantsText);

	// The window procedure is installed and its window still exists (so a WM_CHAR can answer an engine character).
	bool SubclassLive();

	// The IME is composing, or took a key (VK_PROCESSKEY) moments ago - a raw key the engine read is not text.
	bool ImeBusy();

	// The IME took this DirectInput scan code moments ago, or is composing: the key belongs to the IME (Enter that
	// commits, Backspace inside the composition), so ImGui must not also act on it.
	bool ImeOwnsKey(std::uint32_t a_dik);

	// JSON for DevBench amf.menu op=ime (thread-safe; ASCII only).
	std::string StateJson();
}
