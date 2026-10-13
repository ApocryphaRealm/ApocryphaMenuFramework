#pragma once

// ============================================================================================
// TYPING IN OTHER LANGUAGES (2.2.1) - the pure half, with no game, Windows or ImGui code, so the offline check
// (tools\text_input_check.cpp) runs the same functions the framework does.
//
// WHY THIS EXISTS. The engine never reads WM_CHAR. Disassembly of BSWin32KeyboardDevice::Poll (SkyrimSE.exe 1.7.104 on
// disk, 0x140E127C0; the 1.5.97 code dump, 0x140C1A130 - the same code) shows every RE::CharEvent made from a DirectInput
// key-down: the scan code goes through MapVirtualKeyA and ToUnicode with a keyboard state the device builds itself, and a
// character is queued only when ToUnicode returns exactly one. Its window procedure leaves WM_CHAR to DefWindowProcA, and
// answers WM_IME_SETCONTEXT with DefWindowProcA(..., lParam 0), which hides the IME's composition and candidate windows.
// So an IME's text (which arrives only as WM_IME_* / WM_CHAR messages), a SendInput KEYEVENTF_UNICODE character (VK_PACKET,
// which DirectInput never sees) and a posted WM_CHAR could never become a CharEvent - which is exactly what the
// 2026-10-13 test saw. Only Ctrl+V reached the box, because ImGui reads the clipboard itself.
//
// THE FIX (TextInput.cpp) puts a window procedure in front of the game's. While an AMF text box wants text it
// feeds WM_CHAR and the IME's finished text to ImGui. Real keys then arrive TWICE - as the engine's CharEvent (from
// DirectInput) and as WM_CHAR (from the message loop) - in either order, a frame or so apart. CharArbiter decides which one
// types: WM_CHAR always does (it is Windows' real text path: dead keys, AltGr, the IME), and an engine character is dropped
// when a WM_CHAR from a real key answers it; one no WM_CHAR answers within a short window is typed after all (so typing
// still works if the window procedure never sees WM_CHAR), unless the IME was busy - then it was a raw key the IME took.
// ============================================================================================

#include <cstdint>
#include <deque>
#include <string_view>
#include <vector>

namespace textcore
{
	inline constexpr char32_t kReplacement = 0xFFFD;

	// UTF-8 -> code points. A byte that does not start a valid sequence, a truncated or overlong sequence, an encoded
	// surrogate and anything above U+10FFFF each become one U+FFFD, and decoding resumes at the next byte - so a stray
	// byte never swallows the characters after it.
	inline std::vector<char32_t> DecodeUtf8(std::string_view a_text)
	{
		std::vector<char32_t> out;
		out.reserve(a_text.size());
		const auto* s = reinterpret_cast<const unsigned char*>(a_text.data());
		const std::size_t n = a_text.size();
		std::size_t i = 0;
		while (i < n)
		{
			const unsigned char c = s[i];
			if (c < 0x80) { out.push_back(c); ++i; continue; }
			int len = 0;
			char32_t cp = 0;
			char32_t min = 0;
			if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
			else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
			else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
			else { out.push_back(kReplacement); ++i; continue; }
			if (i + static_cast<std::size_t>(len) > n) { out.push_back(kReplacement); ++i; continue; }
			bool ok = true;
			for (int k = 1; k < len; ++k)
			{
				const unsigned char cc = s[i + static_cast<std::size_t>(k)];
				if ((cc & 0xC0) != 0x80) { ok = false; break; }
				cp = (cp << 6) | (cc & 0x3F);
			}
			if (!ok || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { out.push_back(kReplacement); ++i; continue; }
			out.push_back(cp);
			i += static_cast<std::size_t>(len);
		}
		return out;
	}

	// One code point as UTF-16 units: 1 for the BMP, 2 (a surrogate pair) above U+FFFF. A surrogate or out-of-range
	// value becomes U+FFFD. ImGui here is built with 16-bit ImWchar (the SMF consumer ABI), so a pair handed to
	// io.AddInputCharacterUTF16 arrives as U+FFFD rather than as a wrong BMP character - the honest result.
	inline int ToUtf16(char32_t a_cp, std::uint16_t a_out[2])
	{
		if (a_cp > 0x10FFFF || (a_cp >= 0xD800 && a_cp <= 0xDFFF)) { a_cp = kReplacement; }
		if (a_cp < 0x10000) { a_out[0] = static_cast<std::uint16_t>(a_cp); return 1; }
		const char32_t v = a_cp - 0x10000;
		a_out[0] = static_cast<std::uint16_t>(0xD800 + (v >> 10));
		a_out[1] = static_cast<std::uint16_t>(0xDC00 + (v & 0x3FF));
		return 2;
	}

	inline bool IsLowSurrogate(std::uint32_t a_u) { return a_u >= 0xDC00 && a_u <= 0xDFFF; }

	// ImGui's display coordinates -> the game window's client pixels. They differ when the game renders at one size and
	// shows at another (a resolution scale, a borderless upscale). A zero or negative size on either side leaves the
	// point unscaled rather than dividing by zero.
	struct ClientPoint { long x; long y; };
	inline ClientPoint DisplayToClient(float a_x, float a_y, float a_displayW, float a_displayH, long a_clientW, long a_clientH)
	{
		float sx = 1.0f, sy = 1.0f;
		if (a_displayW > 0.0f && a_displayH > 0.0f && a_clientW > 0 && a_clientH > 0)
		{
			sx = static_cast<float>(a_clientW) / a_displayW;
			sy = static_cast<float>(a_clientH) / a_displayH;
		}
		const float x = a_x * sx, y = a_y * sy;
		return { static_cast<long>(x + (x >= 0.0f ? 0.5f : -0.5f)), static_cast<long>(y + (y >= 0.0f ? 0.5f : -0.5f)) };
	}

	// "Did something happen within the last a_ms?" - 0 means never.
	inline bool Within(std::uint64_t a_nowMs, std::uint64_t a_atMs, std::uint64_t a_ms)
	{
		return a_atMs != 0 && a_nowMs >= a_atMs && a_nowMs - a_atMs < a_ms;
	}

	// WM_KEYDOWN's lParam -> the DirectInput scan code the engine reports for the same key (bit 24 = extended -> 0x80).
	inline std::uint32_t DikFromKeyLParam(std::uint64_t a_lParam)
	{
		const std::uint32_t scan = static_cast<std::uint32_t>((a_lParam >> 16) & 0xFF);
		const bool extended = ((a_lParam >> 24) & 1) != 0;
		return extended ? (scan | 0x80) : scan;
	}

	struct ArbiterStats
	{
		std::uint64_t windowFed = 0;          // WM_CHAR units from real keys, typed
		std::uint64_t syntheticFed = 0;       // WM_CHAR units with no scan code (SendInput unicode, posted), typed
		std::uint64_t imeFed = 0;             // IME result units, typed
		std::uint64_t directFed = 0;          // DevBench op=type / anything typed straight in, units
		std::uint64_t engineFed = 0;          // engine characters typed at once (our window procedure not live)
		std::uint64_t enginePaired = 0;       // engine characters dropped because a WM_CHAR typed that key
		std::uint64_t engineImeDropped = 0;   // engine characters dropped because the IME had the key
		std::uint64_t engineFallbackFed = 0;  // engine characters typed after the window with no WM_CHAR
	};

	// Who types a character. Everything it decides to type is appended to a_out as UTF-16 units, in order, for
	// io.AddInputCharacterUTF16. Render thread only (not thread-safe by itself).
	class CharArbiter
	{
	public:
		// How long an engine character waits for its WM_CHAR, and a WM_CHAR for its engine character: both a number of
		// frames AND a time, so neither a fast nor a slow frame rate can close the window before the other half arrives
		// (the message loop and the input poll run on the same game frame, at most about one frame apart).
		static constexpr int kWindowFrames = 4;
		static constexpr std::uint64_t kWindowMs = 60;

		// The engine's CharEvent. a_subclassLive: our window procedure is installed, so a WM_CHAR can answer it.
		// a_imeBusy: the IME is composing or took a key moments ago.
		void Engine(std::uint32_t a_cp, bool a_subclassLive, bool a_imeBusy, std::uint64_t a_nowMs, std::vector<std::uint16_t>& a_out)
		{
			if (!a_subclassLive) { Emit(a_cp, a_out); ++stats.engineFed; return; }
			if (a_imeBusy) { ++stats.engineImeDropped; return; }
			if (!m_credits.empty()) { m_credits.pop_front(); ++stats.enginePaired; return; }
			m_pending.push_back({ a_cp, 0, a_nowMs });
		}

		// A WM_CHAR unit. a_realKey: it carries a scan code, so the engine saw the same key in DirectInput.
		void Window(std::uint16_t a_unit, bool a_realKey, std::uint64_t a_nowMs, std::vector<std::uint16_t>& a_out)
		{
			a_out.push_back(a_unit);
			if (!a_realKey) { ++stats.syntheticFed; return; }
			++stats.windowFed;
			if (IsLowSurrogate(a_unit)) { return; }   // the second half of one character, already counted
			if (!m_pending.empty()) { m_pending.pop_front(); ++stats.enginePaired; return; }
			m_credits.push_back({ a_unit, 0, a_nowMs });
		}

		// The IME's finished text, one UTF-16 unit at a time: always typed, never paired.
		void Ime(std::uint16_t a_unit, std::vector<std::uint16_t>& a_out)
		{
			a_out.push_back(a_unit);
			++stats.imeFed;
		}

		// A code point typed straight in (DevBench op=type): always typed, never paired.
		void Direct(char32_t a_cp, std::vector<std::uint16_t>& a_out) { stats.directFed += static_cast<std::uint64_t>(Emit(a_cp, a_out)); }

		// Once per drained frame, after that frame's records. A pending engine character whose window closed is typed
		// (no WM_CHAR came for it) - unless the IME is busy, when every pending one was a raw key the IME took.
		void EndFrame(bool a_imeBusy, std::uint64_t a_nowMs, std::vector<std::uint16_t>& a_out)
		{
			for (auto& p : m_pending) { ++p.frames; }
			for (auto& c : m_credits) { ++c.frames; }
			if (a_imeBusy && !m_pending.empty())
			{
				stats.engineImeDropped += m_pending.size();
				m_pending.clear();
			}
			while (!m_pending.empty() && Expired(m_pending.front(), a_nowMs))
			{
				Emit(m_pending.front().cp, a_out);
				++stats.engineFallbackFed;
				++m_fallbacksThisHold;
				m_pending.pop_front();
			}
			while (!m_credits.empty() && Expired(m_credits.front(), a_nowMs)) { m_credits.pop_front(); }
		}

		// The menu opened or a text box let go: nothing waiting carries over.
		void Reset()
		{
			m_pending.clear();
			m_credits.clear();
			m_fallbacksThisHold = 0;
		}

		std::size_t Pending() const { return m_pending.size(); }
		std::size_t Credits() const { return m_credits.size(); }
		std::uint64_t FallbacksThisHold() const { return m_fallbacksThisHold; }

		ArbiterStats stats;

	private:
		struct Waiting { std::uint32_t cp; int frames; std::uint64_t at; };

		static bool Expired(const Waiting& a_w, std::uint64_t a_nowMs)
		{
			return a_w.frames >= kWindowFrames && a_nowMs >= a_w.at + kWindowMs;
		}

		static int Emit(char32_t a_cp, std::vector<std::uint16_t>& a_out)
		{
			std::uint16_t u[2];
			const int n = ToUtf16(a_cp, u);
			for (int k = 0; k < n; ++k) { a_out.push_back(u[k]); }
			return n;
		}

		std::deque<Waiting> m_pending;   // engine characters waiting for their WM_CHAR
		std::deque<Waiting> m_credits;   // WM_CHARs from real keys waiting for their engine character
		std::uint64_t m_fallbacksThisHold = 0;
	};
}
