#include "TextInput.h"

#include "Input.h"
#include "Renderer.h"
#include "TextInputCore.h"
#include "utils/Logger.h"
#include "utils/NullGuard.h"

#include <imgui.h>

#include <Windows.h>
#include <imm.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>

#pragma comment(lib, "imm32.lib")

// See TextInput.h for what this is and TextInputCore.h for why the engine needs it (the disassembly). Threads: Install,
// OnFrame and the IME hook run on the render thread; WindowProc and Sync on the game window's thread (the one that pumps
// its messages); the accessors are read by the input drain (render thread) and DevBench (its listener thread). Everything
// shared is an atomic or behind g_imeLock.

namespace textinput
{
	namespace
	{
		std::atomic<HWND> g_hwnd{ nullptr };
		std::atomic<WNDPROC> g_prev{ nullptr };   // the game's procedure (or a handle CallWindowProcA translates)
		std::atomic<bool> g_installed{ false };
		std::atomic<int> g_unicodeBefore{ -1 };   // IsWindowUnicode before / after our subclass (-1 = not read)
		std::atomic<int> g_unicodeAfter{ -1 };
		UINT g_msgSync = 0;                       // RegisterWindowMessageW; written once in Install, before any post

		std::atomic<bool> g_wantRequested{ false };   // render thread: an AMF text field wants text
		std::atomic<bool> g_wantApplied{ false };     // window thread: the IME's windows are shown for it
		std::atomic<bool> g_syncPosted{ false };      // one sync message in flight at a time
		std::atomic<int> g_hasContext{ -1 };          // the window had an input context at the last sync (-1 unknown)
		std::atomic<bool> g_weAssociated{ false };    // we gave the window the default context; taken back on release
		std::atomic<int> g_imeOpen{ -1 };             // ImmGetOpenStatus at the last sync

		std::atomic<bool> g_composing{ false };
		std::atomic<std::uint64_t> g_lastProcessKeyMs{ 0 };
		std::array<std::atomic<std::uint64_t>, 256> g_keyTakenMs{};

		// Counters for DevBench op=ime - the in-game test reads which path each character took.
		std::atomic<std::uint64_t> g_wmCharSeen{ 0 }, g_wmCharFed{ 0 }, g_imeResults{ 0 }, g_imeUnitsFed{ 0 }, g_imeCharFed{ 0 },
			g_processKeys{ 0 }, g_setContextKept{ 0 }, g_syncs{ 0 }, g_positionsApplied{ 0 };
		std::atomic<std::uint32_t> g_lastWmChar{ 0 }, g_lastWmCharByte{ 0 }, g_lastWmCharScan{ 0 }, g_lastImeUnit{ 0 };
		// THE INPUT LANGUAGE'S CODE PAGE (window thread writes). The game pumps with PeekMessageA, so every WM_CHAR reaches
		// any procedure as ANSI bytes in the code page of the keyboard layout in use (a DBCS character as two WM_CHARs,
		// lead byte first); this turns them back into UTF-16. 0 = not read yet.
		std::atomic<std::uint32_t> g_inputCodePage{ 0 };
		unsigned char g_leadByte = 0;   // window thread: a DBCS lead byte waiting for its trail byte
		std::atomic<std::uint64_t> g_lastPacketKeyLParam{ 0 };
		std::atomic<long> g_lastCompX{ 0 }, g_lastCompY{ 0 }, g_lastCandY{ 0 };

		struct ImeData
		{
			bool visible = false;
			float x = 0.0f, y = 0.0f, lineH = 0.0f;
			float displayW = 0.0f, displayH = 0.0f;
		};
		std::mutex g_imeLock;
		ImeData g_ime;
		std::atomic<bool> g_imeDirty{ false };

		// One log line per hold for each kind of arrival (window thread only).
		bool g_loggedCharThisHold = false;
		bool g_loggedImeThisHold = false;
		bool g_loggedPositionThisHold = false;

		constexpr std::uint64_t kImeBusyMs = 300;
		constexpr std::uint64_t kImeKeyMs = 500;
		constexpr LPARAM kResultFlags = GCS_RESULTSTR | GCS_RESULTREADSTR | GCS_RESULTCLAUSE | GCS_RESULTREADCLAUSE;

		std::uint64_t NowMs() { return GetTickCount64(); }

		// Window thread (GetKeyboardLayout(0) is the calling thread's layout). The ANSI code page of the input language.
		std::uint32_t ReadInputCodePage(HKL a_hkl)
		{
			const LANGID lang = LOWORD(reinterpret_cast<std::uintptr_t>(a_hkl));
			DWORD cp = 0;
			if (!GetLocaleInfoW(MAKELCID(lang, SORT_DEFAULT), LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
					reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(WCHAR)) ||
				cp == 0)
			{
				cp = GetACP();   // a language with no ANSI code page (Georgian, Armenian): the system's, as user32 falls back to
			}
			return static_cast<std::uint32_t>(cp);
		}

		std::uint32_t InputCodePage()
		{
			std::uint32_t cp = g_inputCodePage.load(std::memory_order_acquire);
			if (cp == 0)
			{
				cp = ReadInputCodePage(GetKeyboardLayout(0));
				g_inputCodePage.store(cp, std::memory_order_release);
			}
			return cp;
		}

		// ANSI bytes (1, or 2 for a DBCS character) -> UTF-16 units appended to a_out. Returns how many.
		int AnsiToUnits(const char* a_bytes, int a_len, std::uint16_t a_out[4])
		{
			AMF_REQUIRE(a_bytes, 0);
			wchar_t w[4]{};
			int n = MultiByteToWideChar(InputCodePage(), 0, a_bytes, a_len, w, 4);
			if (n <= 0) { n = MultiByteToWideChar(CP_ACP, 0, a_bytes, a_len, w, 4); }
			if (n <= 0) { return 0; }
			for (int i = 0; i < n; ++i) { a_out[i] = static_cast<std::uint16_t>(w[i]); }
			return n;
		}

		void PostSync()
		{
			const HWND hwnd = g_hwnd.load(std::memory_order_acquire);
			if (!hwnd || g_msgSync == 0) { return; }
			if (g_syncPosted.exchange(true, std::memory_order_acq_rel)) { return; }
			if (!PostMessageW(hwnd, g_msgSync, 0, 0))
			{
				g_syncPosted.store(false, std::memory_order_release);
				static bool s_said = false;
				if (!s_said)
				{
					s_said = true;
					logger::warn("typing: could not post the IME sync to the game window (error {}) - IME windows stay as the game set them",
								 GetLastError());
				}
			}
		}

		// Window thread. Puts the IME's composition window at the text cursor and its candidate list just below the line.
		void ApplyImePosition(HWND a_hwnd)
		{
			ImeData d;
			{
				std::scoped_lock l(g_imeLock);
				d = g_ime;
			}
			if (!d.visible) { return; }
			RECT rc{};
			if (!GetClientRect(a_hwnd, &rc)) { return; }
			const long cw = rc.right - rc.left, ch = rc.bottom - rc.top;
			const auto top = textcore::DisplayToClient(d.x, d.y, d.displayW, d.displayH, cw, ch);
			const auto bottom = textcore::DisplayToClient(d.x, d.y + d.lineH, d.displayW, d.displayH, cw, ch);
			const HIMC himc = ImmGetContext(a_hwnd);
			if (!himc)
			{
				static bool s_said = false;
				if (!s_said)
				{
					s_said = true;
					logger::warn("typing: the game window has no input context when placing the IME window - nothing placed");
				}
				return;
			}
			COMPOSITIONFORM comp{};
			comp.dwStyle = CFS_FORCE_POSITION;
			comp.ptCurrentPos = { top.x, top.y };
			const BOOL compOk = ImmSetCompositionWindow(himc, &comp);
			CANDIDATEFORM cand{};
			cand.dwIndex = 0;
			cand.dwStyle = CFS_EXCLUDE;
			cand.ptCurrentPos = { top.x, bottom.y };
			cand.rcArea = { top.x, top.y, top.x + 1, bottom.y };   // the text cursor: the list never covers the line typed in
			const BOOL candOk = ImmSetCandidateWindow(himc, &cand);
			ImmReleaseContext(a_hwnd, himc);
			g_lastCompX.store(top.x, std::memory_order_relaxed);
			g_lastCompY.store(top.y, std::memory_order_relaxed);
			g_lastCandY.store(bottom.y, std::memory_order_relaxed);
			g_positionsApplied.fetch_add(1, std::memory_order_relaxed);
			if (!g_loggedPositionThisHold)
			{
				g_loggedPositionThisHold = true;
				logger::info("typing: IME window placed at the text cursor - display ({:.0f}, {:.0f}) line {:.0f} of {:.0f}x{:.0f} -> client ({}, {}) "
							 "of {}x{}, candidates below y {} (composition {}, candidate {})",
							 d.x, d.y, d.lineH, d.displayW, d.displayH, top.x, top.y, cw, ch, bottom.y,
							 compOk ? "set" : "refused", candOk ? "set" : "refused");
			}
		}

		// Window thread: the private message. Applies the render thread's "a field wants text" state and IME position.
		void Sync(HWND a_hwnd)
		{
			g_syncPosted.store(false, std::memory_order_release);
			g_syncs.fetch_add(1, std::memory_order_relaxed);
			const bool want = g_wantRequested.load(std::memory_order_acquire);

			static bool s_probed = false;
			if (!s_probed)
			{
				s_probed = true;
				const HIMC probe = ImmGetContext(a_hwnd);
				if (probe) { ImmReleaseContext(a_hwnd, probe); }
				g_hasContext.store(probe ? 1 : 0, std::memory_order_relaxed);
				const std::uint32_t cp = ReadInputCodePage(GetKeyboardLayout(0));
				g_inputCodePage.store(cp, std::memory_order_release);
				logger::info("typing: on the game window's thread ({}): input context {}, window unicode {}, input code page {}", GetCurrentThreadId(),
							 probe ? "present" : "NONE (the game or another mod removed it; AMF associates the default one while a text box wants text)",
							 IsWindowUnicode(a_hwnd) ? "yes" : "no", cp);
			}

			if (want != g_wantApplied.load(std::memory_order_acquire))
			{
				HIMC himc = ImmGetContext(a_hwnd);
				const bool had = himc != nullptr;
				int open = -1;
				if (himc)
				{
					open = ImmGetOpenStatus(himc) ? 1 : 0;
					ImmReleaseContext(a_hwnd, himc);
				}
				g_hasContext.store(had ? 1 : 0, std::memory_order_relaxed);
				g_imeOpen.store(open, std::memory_order_relaxed);
				const bool active = GetActiveWindow() == a_hwnd;
				// Two lines per click into a box at info; past 50 holds in a session they drop to debug, so a field that
				// somehow toggled every frame could never flood the log.
				static int s_holds = 0;
				const auto level = ++s_holds <= 100 ? spdlog::level::info : spdlog::level::debug;
				if (want)
				{
					bool associated = false;
					if (!had && ImmAssociateContextEx(a_hwnd, nullptr, IACE_DEFAULT))
					{
						associated = true;
						g_weAssociated.store(true, std::memory_order_release);
					}
					g_wantApplied.store(true, std::memory_order_release);
					g_loggedCharThisHold = false;
					g_loggedImeThisHold = false;
					g_loggedPositionThisHold = false;
					// The game answered WM_IME_SETCONTEXT with lParam 0, which hid the IME's windows: show them again.
					if (active) { DefWindowProcA(a_hwnd, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(ISC_SHOWUIALL)); }
					logger::at_level(level, "typing: an AMF text box wants text - IME windows shown (window active {}, input context {}, IME open {})",
								 active ? "yes" : "no", had ? "present" : (associated ? "associated by AMF" : "NONE - association failed"),
								 open < 0 ? "unknown" : (open ? "yes" : "no"));
				}
				else
				{
					// A composition the field left half-typed is cancelled, not committed into whatever has focus next.
					if (g_composing.load(std::memory_order_acquire))
					{
						if (HIMC c = ImmGetContext(a_hwnd))
						{
							ImmNotifyIME(c, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
							ImmReleaseContext(a_hwnd, c);
						}
					}
					g_composing.store(false, std::memory_order_release);
					g_wantApplied.store(false, std::memory_order_release);
					if (active) { DefWindowProcA(a_hwnd, WM_IME_SETCONTEXT, TRUE, 0); }   // the game's own state: IME windows hidden
					if (g_weAssociated.exchange(false, std::memory_order_acq_rel)) { ImmAssociateContext(a_hwnd, nullptr); }
					logger::at_level(level, "typing: the AMF text box let go - IME windows hidden as the game keeps them (so far: WM_CHAR {} typed of {} seen, "
								 "IME results {} = {} unit(s), keys the IME took {})",
								 g_wmCharFed.load(), g_wmCharSeen.load(), g_imeResults.load(), g_imeUnitsFed.load(), g_processKeys.load());
				}
			}

			if (want && g_imeDirty.exchange(false, std::memory_order_acq_rel)) { ApplyImePosition(a_hwnd); }
		}

		// Window thread: the finished text of a composition, as UTF-16, straight from the IME (no code page in between).
		void ReadResult(HWND a_hwnd)
		{
			const HIMC himc = ImmGetContext(a_hwnd);
			if (!himc)
			{
				logger::warn("typing: WM_IME_COMPOSITION carried a result but the window has no input context - result lost");
				return;
			}
			const LONG bytes = ImmGetCompositionStringW(himc, GCS_RESULTSTR, nullptr, 0);
			if (bytes > 0 && bytes < 4096)
			{
				std::wstring text(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
				ImmGetCompositionStringW(himc, GCS_RESULTSTR, text.data(), static_cast<DWORD>(bytes));
				for (const wchar_t u : text)
				{
					input::QueueTypedUnit(static_cast<std::uint16_t>(u), 2);
					g_lastImeUnit.store(static_cast<std::uint32_t>(u), std::memory_order_relaxed);
				}
				g_imeResults.fetch_add(1, std::memory_order_relaxed);
				g_imeUnitsFed.fetch_add(text.size(), std::memory_order_relaxed);
				if (!g_loggedImeThisHold)
				{
					g_loggedImeThisHold = true;
					logger::info("typing: IME result of {} UTF-16 unit(s) typed (first U+{:04X})", text.size(),
								 text.empty() ? 0u : static_cast<unsigned>(text[0]));
				}
			}
			else if (bytes < 0)
			{
				logger::warn("typing: ImmGetCompositionStringW(GCS_RESULTSTR) failed ({})", bytes);
			}
			ImmReleaseContext(a_hwnd, himc);
		}

		LRESULT CALLBACK WindowProc(HWND a_hwnd, UINT a_msg, WPARAM a_wParam, LPARAM a_lParam)
		{
			const WNDPROC prev = g_prev.load(std::memory_order_acquire);
			const auto pass = [&]() -> LRESULT {
				return prev ? CallWindowProcA(prev, a_hwnd, a_msg, a_wParam, a_lParam) : DefWindowProcA(a_hwnd, a_msg, a_wParam, a_lParam);
			};

			if (g_msgSync != 0 && a_msg == g_msgSync)
			{
				Sync(a_hwnd);
				return 0;
			}

			// An AMF text field (or a mod's text box in AMF's context) holds the keyboard right now.
			const bool typing = renderer::WantsTextInput();

			switch (a_msg)
			{
			case WM_CHAR:
				{
					g_wmCharSeen.fetch_add(1, std::memory_order_relaxed);
					const auto scan = static_cast<std::uint32_t>((static_cast<std::uint64_t>(a_lParam) >> 16) & 0xFF);
					g_lastWmCharByte.store(static_cast<std::uint32_t>(a_wParam & 0xFFFF), std::memory_order_relaxed);
					g_lastWmCharScan.store(scan, std::memory_order_relaxed);
					if (!typing)
					{
						g_leadByte = 0;
						break;
					}
					// ANSI, in the input language's code page (the game's message loop is ANSI). A DBCS character comes as
					// two WM_CHARs, lead byte first; one packed in the high byte is taken as a pair too.
					std::uint16_t units[4]{};
					int count = 0;
					const auto byte = static_cast<unsigned char>(a_wParam & 0xFF);
					if (a_wParam > 0xFF)
					{
						const char pair[2] = { static_cast<char>((a_wParam >> 8) & 0xFF), static_cast<char>(byte) };
						count = AnsiToUnits(pair, 2, units);
						g_leadByte = 0;
					}
					else if (g_leadByte != 0)
					{
						const char pair[2] = { static_cast<char>(g_leadByte), static_cast<char>(byte) };
						count = AnsiToUnits(pair, 2, units);
						g_leadByte = 0;
					}
					else if (byte >= 0x80 && IsDBCSLeadByteEx(InputCodePage(), byte))
					{
						g_leadByte = byte;
						return 0;   // the trail byte follows
					}
					else
					{
						const char one = static_cast<char>(byte);
						count = AnsiToUnits(&one, 1, units);
					}
					// scan 0 = no physical key (SendInput unicode, a posted WM_CHAR): the engine made no CharEvent for it
					for (int i = 0; i < count; ++i) { input::QueueTypedUnit(units[i], scan != 0 ? 0 : 1); }
					if (count > 0) { g_lastWmChar.store(units[0], std::memory_order_relaxed); }
					g_wmCharFed.fetch_add(1, std::memory_order_relaxed);
					if (!g_loggedCharThisHold)
					{
						g_loggedCharThisHold = true;
						logger::info("typing: WM_CHAR reaches AMF's window procedure - byte 0x{:02X} scan 0x{:02X} -> U+{:04X} (code page {}) typed",
									 static_cast<unsigned>(a_wParam & 0xFFFF), scan, count > 0 ? units[0] : 0u, InputCodePage());
					}
					return 0;   // the game makes no text from WM_CHAR; nothing downstream needs it
				}
			case WM_IME_CHAR:
				if (typing)
				{
					// Only made by DefWindowProc from a GCS_RESULTSTR we strip below - so this one came from elsewhere: type it.
					// ANSI: a DBCS character has its lead byte in bits 8-15.
					std::uint16_t units[4]{};
					int count = 0;
					if (a_wParam > 0xFF)
					{
						const char pair[2] = { static_cast<char>((a_wParam >> 8) & 0xFF), static_cast<char>(a_wParam & 0xFF) };
						count = AnsiToUnits(pair, 2, units);
					}
					else
					{
						const char one = static_cast<char>(a_wParam & 0xFF);
						count = AnsiToUnits(&one, 1, units);
					}
					for (int i = 0; i < count; ++i) { input::QueueTypedUnit(units[i], 2); }
					g_imeCharFed.fetch_add(1, std::memory_order_relaxed);
					return 0;
				}
				break;
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				if (a_wParam == VK_PROCESSKEY)
				{
					const std::uint64_t now = NowMs();
					g_lastProcessKeyMs.store(now, std::memory_order_release);
					g_keyTakenMs[textcore::DikFromKeyLParam(static_cast<std::uint64_t>(a_lParam)) & 0xFF].store(now, std::memory_order_release);
					g_processKeys.fetch_add(1, std::memory_order_relaxed);
				}
				else if (a_wParam == VK_PACKET)
				{
					g_lastPacketKeyLParam.store(static_cast<std::uint64_t>(a_lParam), std::memory_order_relaxed);
				}
				break;
			case WM_IME_SETCONTEXT:
				if (!a_wParam) { g_composing.store(false, std::memory_order_release); }
				if (g_wantApplied.load(std::memory_order_acquire))
				{
					// The game would pass lParam 0 (no IME windows). While AMF types, keep the IME's own windows.
					g_setContextKept.fetch_add(1, std::memory_order_relaxed);
					return DefWindowProcA(a_hwnd, a_msg, a_wParam, a_lParam);
				}
				break;
			case WM_IME_STARTCOMPOSITION:
				if (typing)
				{
					g_composing.store(true, std::memory_order_release);
					ApplyImePosition(a_hwnd);
					return DefWindowProcA(a_hwnd, a_msg, a_wParam, a_lParam);
				}
				break;
			case WM_IME_COMPOSITION:
				if (typing)
				{
					if (a_lParam & GCS_RESULTSTR) { ReadResult(a_hwnd); }
					// What remains (the composition string being edited) still goes to the IME's window to be drawn; the
					// result flags do not, or DefWindowProc would send the same text again as WM_IME_CHAR.
					const LPARAM rest = a_lParam & ~kResultFlags;
					return rest ? DefWindowProcA(a_hwnd, a_msg, a_wParam, rest) : 0;
				}
				break;
			case WM_IME_ENDCOMPOSITION:
				g_composing.store(false, std::memory_order_release);
				if (typing) { return DefWindowProcA(a_hwnd, a_msg, a_wParam, a_lParam); }
				break;
			case WM_KILLFOCUS:
				g_composing.store(false, std::memory_order_release);
				break;
			case WM_ACTIVATEAPP:
				if (!a_wParam) { g_composing.store(false, std::memory_order_release); }
				break;
			case WM_INPUTLANGCHANGE:
				{
					const std::uint32_t cp = ReadInputCodePage(reinterpret_cast<HKL>(a_lParam));
					g_inputCodePage.store(cp, std::memory_order_release);
					g_leadByte = 0;
					logger::debug("typing: input language changed (keyboard layout 0x{:X}) - WM_CHAR read in code page {}",
								  static_cast<std::uint64_t>(a_lParam), cp);
				}
				break;
			default:
				break;
			}
			return pass();
		}

		// Render thread, from ImGui::EndFrame when the text cursor moves or a field starts / stops editing.
		void ImeDataFn(ImGuiViewport* a_viewport, ImGuiPlatformImeData* a_data)
		{
			if (!a_data) { return; }
			const ImGuiIO& io = ImGui::GetIO();
			const float ox = a_viewport ? a_viewport->Pos.x : 0.0f;
			const float oy = a_viewport ? a_viewport->Pos.y : 0.0f;
			{
				std::scoped_lock l(g_imeLock);
				g_ime.visible = a_data->WantVisible;
				g_ime.x = a_data->InputPos.x - ox;
				g_ime.y = a_data->InputPos.y - oy;
				g_ime.lineH = a_data->InputLineHeight;
				g_ime.displayW = io.DisplaySize.x;
				g_ime.displayH = io.DisplaySize.y;
			}
			if (a_data->WantVisible)
			{
				g_imeDirty.store(true, std::memory_order_release);
				PostSync();
			}
		}
	}

	void Install(void* a_hwnd)
	{
		static bool s_done = false;
		if (s_done) { return; }
		s_done = true;

		const HWND hwnd = static_cast<HWND>(a_hwnd);
		if (!hwnd || !IsWindow(hwnd))
		{
			logger::warn("typing: no game window to subclass ({}) - typing stays on the engine's CharEvents (no IME, no SendInput unicode)",
						 static_cast<const void*>(hwnd));
			return;
		}
		g_msgSync = RegisterWindowMessageW(L"ApocryphaMenuFramework.TextInput.Sync");
		if (g_msgSync == 0)
		{
			logger::warn("typing: RegisterWindowMessageW failed ({}) - IME windows cannot be shown or placed", GetLastError());
		}
		const bool unicodeBefore = IsWindowUnicode(hwnd) != FALSE;
		g_unicodeBefore.store(unicodeBefore ? 1 : 0, std::memory_order_relaxed);
		DWORD pid = 0;
		const DWORD windowThread = GetWindowThreadProcessId(hwnd, &pid);

		// AN ANSI PROCEDURE, ON PURPOSE. The game's window is ANSI (RegisterClassA, CreateWindowExA, PeekMessageA /
		// DispatchMessageA), and other mods subclass it too. A Unicode procedure would make IsWindowUnicode() answer TRUE,
		// and Dear ImGui's Win32 backend - which other mods' menus run in their own procedures - reads WM_CHAR as UTF-16
		// when it does, while those ANSI procedures still receive ANSI bytes: Cyrillic typed into another mod's box would
		// turn into Latin-1 letters. And a Unicode procedure gains nothing here: the message loop is ANSI, so WM_CHAR has
		// already been through the code page before any procedure sees it. The IME's text never is - it is read with
		// ImmGetCompositionStringW below.
		// The previous procedure is known BEFORE ours goes in, so a message arriving between the swap and the store
		// below still has somewhere to go.
		g_prev.store(reinterpret_cast<WNDPROC>(GetWindowLongPtrA(hwnd, GWLP_WNDPROC)), std::memory_order_release);
		SetLastError(0);
		const LONG_PTR old = SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc));
		if (old == 0 && GetLastError() != 0)
		{
			logger::warn("typing: SetWindowLongPtrA on the game window failed ({}) - typing stays on the engine's CharEvents", GetLastError());
			return;
		}
		if (old != 0) { g_prev.store(reinterpret_cast<WNDPROC>(old), std::memory_order_release); }
		g_hwnd.store(hwnd, std::memory_order_release);
		g_installed.store(true, std::memory_order_release);
		const bool unicodeAfter = IsWindowUnicode(hwnd) != FALSE;
		g_unicodeAfter.store(unicodeAfter ? 1 : 0, std::memory_order_relaxed);

		// Ours replaces ImGui's default IME hook, which ran IMM calls on this (render) thread with unscaled coordinates.
		ImGui::GetIO().SetPlatformImeDataFn = &ImeDataFn;

		logger::info("typing: window procedure installed on the game window {} (window thread {}, this thread {}); window unicode "
					 "before {} / after {}; the game's procedure {} gets every message AMF does not type; ImGui's IME hook replaced",
					 static_cast<const void*>(hwnd), windowThread, GetCurrentThreadId(), unicodeBefore ? "yes" : "no",
					 unicodeAfter ? "yes" : "no", reinterpret_cast<const void*>(old));
		PostSync();   // the first sync logs, from the window's own thread, whether the window has an input context
	}

	void OnFrame(bool a_wantsText)
	{
		if (!g_installed.load(std::memory_order_acquire)) { return; }
		if (g_wantRequested.load(std::memory_order_acquire) == a_wantsText) { return; }
		g_wantRequested.store(a_wantsText, std::memory_order_release);
		PostSync();
	}

	bool SubclassLive()
	{
		if (!g_installed.load(std::memory_order_acquire)) { return false; }
		const HWND hwnd = g_hwnd.load(std::memory_order_acquire);
		return hwnd && IsWindow(hwnd);
	}

	bool ImeBusy()
	{
		return g_composing.load(std::memory_order_acquire) ||
			   textcore::Within(NowMs(), g_lastProcessKeyMs.load(std::memory_order_acquire), kImeBusyMs);
	}

	bool ImeOwnsKey(std::uint32_t a_dik)
	{
		return g_composing.load(std::memory_order_acquire) ||
			   textcore::Within(NowMs(), g_keyTakenMs[a_dik & 0xFF].load(std::memory_order_acquire), kImeKeyMs);
	}

	std::string StateJson()
	{
		const auto b = [](bool a_v) { return std::string(a_v ? "true" : "false"); };
		const auto tri = [](int a_v) { return std::string(a_v < 0 ? "null" : (a_v ? "true" : "false")); };
		const auto n = [](std::uint64_t a_v) { return std::to_string(a_v); };
		textcore::ArbiterStats s{};
		std::size_t pending = 0;
		input::GetTypingStats(s, pending);
		return "{\"ok\":true,\"op\":\"ime\",\"subclassInstalled\":" + b(g_installed.load()) + ",\"subclassLive\":" + b(SubclassLive()) +
			   ",\"windowUnicodeBefore\":" + tri(g_unicodeBefore.load()) + ",\"windowUnicodeAfter\":" + tri(g_unicodeAfter.load()) +
			   ",\"imeContext\":" + tri(g_hasContext.load()) + ",\"imeOpen\":" + tri(g_imeOpen.load()) +
			   ",\"weAssociated\":" + b(g_weAssociated.load()) + ",\"fieldWantsText\":" + b(renderer::WantsTextInput()) +
			   ",\"wantRequested\":" + b(g_wantRequested.load()) + ",\"imeWindowsShown\":" + b(g_wantApplied.load()) +
			   ",\"composing\":" + b(g_composing.load()) + ",\"imeBusy\":" + b(ImeBusy()) +
			   ",\"lastWmChar\":" + n(g_lastWmChar.load()) + ",\"lastWmCharByte\":" + n(g_lastWmCharByte.load()) +
			   ",\"lastWmCharScan\":" + n(g_lastWmCharScan.load()) + ",\"inputCodePage\":" + n(g_inputCodePage.load()) +
			   ",\"lastImeUnit\":" + n(g_lastImeUnit.load()) + ",\"lastPacketKeyLParam\":" + n(g_lastPacketKeyLParam.load()) +
			   ",\"wmCharSeen\":" + n(g_wmCharSeen.load()) + ",\"wmCharFed\":" + n(g_wmCharFed.load()) +
			   ",\"imeResults\":" + n(g_imeResults.load()) + ",\"imeUnitsFed\":" + n(g_imeUnitsFed.load()) +
			   ",\"imeCharFed\":" + n(g_imeCharFed.load()) + ",\"processKeys\":" + n(g_processKeys.load()) +
			   ",\"setContextKept\":" + n(g_setContextKept.load()) + ",\"syncs\":" + n(g_syncs.load()) +
			   ",\"positionsApplied\":" + n(g_positionsApplied.load()) + ",\"lastCompX\":" + std::to_string(g_lastCompX.load()) +
			   ",\"lastCompY\":" + std::to_string(g_lastCompY.load()) + ",\"lastCandY\":" + std::to_string(g_lastCandY.load()) +
			   ",\"typed\":{\"windowReal\":" + n(s.windowFed) + ",\"windowSynthetic\":" + n(s.syntheticFed) + ",\"ime\":" + n(s.imeFed) +
			   ",\"direct\":" + n(s.directFed) + ",\"engineNoSubclass\":" + n(s.engineFed) + ",\"engineFallback\":" + n(s.engineFallbackFed) +
			   "},\"dropped\":{\"enginePaired\":" + n(s.enginePaired) + ",\"engineImeBusy\":" + n(s.engineImeDropped) +
			   "},\"enginePending\":" + n(pending) + "}";
	}
}
