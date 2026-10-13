// Off-game check of typing in other languages (include/TextInputCore.h - the same functions the framework runs):
// UTF-8 decoding for DevBench op=type / injectchar, UTF-16 for characters above U+FFFF, display -> client scaling for the
// IME's candidate window, the WM_KEYDOWN -> scan code mapping, and the arbiter that decides whether the engine's
// CharEvent or the window's WM_CHAR types a key (never both, never neither).
//   tools\text_input_check.bat
// Prints one line per check and "text_input_check: N of N passed"; exits 1 on any failure.
#include "../include/TextInputCore.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	using U16 = std::vector<std::uint16_t>;

	void Utf8()
	{
		using textcore::DecodeUtf8;
		// U+51FD U+6570 = the test's search word, which op=type used to push as six bytes (mojibake)
		const auto hanshu = DecodeUtf8("\xE5\x87\xBD\xE6\x95\xB0");
		Check(hanshu == std::vector<char32_t>{ 0x51FD, 0x6570 }, "utf8: two CJK characters -> 2 code points, not 6 bytes");
		Check(DecodeUtf8("abc") == std::vector<char32_t>{ 'a', 'b', 'c' }, "utf8: ASCII unchanged");
		Check(DecodeUtf8("\xC3\xA9\xC3\xB1") == std::vector<char32_t>{ 0xE9, 0xF1 }, "utf8: e-acute, n-tilde");
		Check(DecodeUtf8("\xD0\x96\xD1\x8F") == std::vector<char32_t>{ 0x416, 0x44F }, "utf8: Cyrillic");
		Check(DecodeUtf8("\xF0\x9F\x98\x80") == std::vector<char32_t>{ 0x1F600 }, "utf8: 4-byte sequence above U+FFFF");
		Check(DecodeUtf8("\xE5\x87") == std::vector<char32_t>{ 0xFFFD, 0xFFFD }, "utf8: truncated sequence -> replacements, no read past end");
		Check(DecodeUtf8("\xC0\xAF" "a") == std::vector<char32_t>{ 0xFFFD, 0xFFFD, 'a' }, "utf8: overlong rejected, next char kept");
		Check(DecodeUtf8("\xED\xA0\x80") == std::vector<char32_t>{ 0xFFFD, 0xFFFD, 0xFFFD }, "utf8: encoded surrogate rejected");
		Check(DecodeUtf8("\xFF" "b") == std::vector<char32_t>{ 0xFFFD, 'b' }, "utf8: invalid lead byte");
		Check(DecodeUtf8("").empty(), "utf8: empty");
	}

	void Utf16()
	{
		std::uint16_t u[2]{};
		Check(textcore::ToUtf16(0x51FD, u) == 1 && u[0] == 0x51FD, "utf16: BMP is one unit");
		Check(textcore::ToUtf16(0x1F600, u) == 2 && u[0] == 0xD83D && u[1] == 0xDE00, "utf16: U+1F600 -> D83D DE00");
		Check(textcore::ToUtf16(0x10FFFF, u) == 2 && u[0] == 0xDBFF && u[1] == 0xDFFF, "utf16: U+10FFFF -> DBFF DFFF");
		Check(textcore::ToUtf16(0xD800, u) == 1 && u[0] == 0xFFFD, "utf16: lone surrogate -> U+FFFD");
		Check(textcore::ToUtf16(0x110000, u) == 1 && u[0] == 0xFFFD, "utf16: above U+10FFFF -> U+FFFD");
	}

	void Scale()
	{
		auto p = textcore::DisplayToClient(960.0f, 540.0f, 1920.0f, 1080.0f, 3840, 2160);
		Check(p.x == 1920 && p.y == 1080, "scale: 1080p display on a 4K client doubles");
		p = textcore::DisplayToClient(100.4f, 200.6f, 1920.0f, 1080.0f, 1920, 1080);
		Check(p.x == 100 && p.y == 201, "scale: same size rounds to nearest");
		p = textcore::DisplayToClient(50.0f, 60.0f, 0.0f, 0.0f, 1920, 1080);
		Check(p.x == 50 && p.y == 60, "scale: zero display size leaves the point unscaled");
		p = textcore::DisplayToClient(50.0f, 60.0f, 1920.0f, 1080.0f, 0, 0);
		Check(p.x == 50 && p.y == 60, "scale: minimised (zero client) leaves the point unscaled");
	}

	void KeyParam()
	{
		// lParam for 'A' down: repeat 1, scan 0x1E
		Check(textcore::DikFromKeyLParam(0x001E0001ull) == 0x1E, "dik: plain scan code");
		// Enter on the keypad: scan 0x1C, extended -> DIK 0x9C (kKP_Enter)
		Check(textcore::DikFromKeyLParam(0x011C0001ull) == 0x9C, "dik: extended key -> | 0x80");
		Check(textcore::Within(1000, 900, 300) && !textcore::Within(1000, 600, 300) && !textcore::Within(1000, 0, 300),
			  "within: inside, outside, never");
	}

	// ---- the arbiter ----------------------------------------------------------------------------------
	// A frame-stepped model: per frame, a list of events in arrival order, then EndFrame. Time advances 16 ms a frame.
	struct Ev
	{
		char kind;            // 'E' engine char, 'W' WM_CHAR real key, 'S' WM_CHAR synthetic, 'I' IME unit, 'D' direct
		std::uint32_t code;
	};

	U16 Run(const std::vector<std::vector<Ev>>& a_frames, bool a_live, textcore::CharArbiter& a_arb,
		const std::vector<bool>& a_imeBusy = {}, int a_tailFrames = 8)
	{
		U16 out;
		std::uint64_t now = 1000;
		const std::size_t total = a_frames.size() + static_cast<std::size_t>(a_tailFrames);
		for (std::size_t f = 0; f < total; ++f)
		{
			const bool busy = f < a_imeBusy.size() ? a_imeBusy[f] : false;
			if (f < a_frames.size())
			{
				for (const Ev& e : a_frames[f])
				{
					switch (e.kind)
					{
					case 'E': a_arb.Engine(e.code, a_live, busy, now, out); break;
					case 'W': a_arb.Window(static_cast<std::uint16_t>(e.code), true, now, out); break;
					case 'S': a_arb.Window(static_cast<std::uint16_t>(e.code), false, now, out); break;
					case 'I': a_arb.Ime(static_cast<std::uint16_t>(e.code), out); break;
					case 'D': a_arb.Direct(e.code, out); break;
					}
				}
			}
			a_arb.EndFrame(busy, now, out);
			now += 16;
		}
		return out;
	}

	U16 S(const char* a) { U16 v; for (; *a; ++a) v.push_back(static_cast<unsigned char>(*a)); return v; }

	void Arbiter()
	{
		{
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'a' } }, { { 'W', 'a' } }, { { 'W', 'b' }, { 'E', 'b' } }, { { 'E', 'c' }, { 'W', 'c' } } }, true, a);
			Check(out == S("abc"), "arbiter: ASCII typed once whichever path arrives first (no doubled letters)");
			Check(a.stats.enginePaired == 3 && a.stats.engineFallbackFed == 0, "arbiter: all three engine chars paired, none fell back");
		}
		{
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'x' } }, {}, { { 'W', 'x' } } }, true, a);
			Check(out == S("x"), "arbiter: WM_CHAR two frames after the engine char still pairs");
		}
		{
			// the window procedure never sees WM_CHAR: the engine path must still type, after the window
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'h' } }, { { 'E', 'i' } } }, true, a);
			Check(out == S("hi"), "arbiter: no WM_CHAR at all -> engine chars typed (fallback), in order");
			Check(a.stats.engineFallbackFed == 2 && a.FallbacksThisHold() == 2, "arbiter: fallback counted");
		}
		{
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'q' } } }, false, a);
			Check(out == S("q") && a.stats.engineFed == 1, "arbiter: window procedure not installed -> engine char typed at once (old behaviour)");
		}
		{
			// dead key: e-acute arrives as WM_CHAR U+00E9, the engine's ToUnicode gave plain 'e' - WM_CHAR wins
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'e' } }, { { 'W', 0xE9 } } }, true, a);
			Check(out == U16{ 0xE9 }, "arbiter: dead-key e-acute from WM_CHAR replaces the engine's 'e'");
		}
		{
			// Chinese IME: pinyin keys reach DirectInput as letters while the IME composes; only the result is typed
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'h' } }, { { 'E', 'a' } }, { { 'E', 'n' } }, { { 'I', 0x51FD } } }, true, a, { false, true, true, true });
			Check(out == U16{ 0x51FD }, "arbiter: IME composing -> raw pinyin letters dropped, result typed");
			Check(a.stats.engineImeDropped == 3, "arbiter: the first letter (seen before the IME said busy) dropped once it did");
		}
		{
			// auto-repeat: Windows repeats WM_CHAR, DirectInput reports one key-down
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 'z' }, { 'W', 'z' } }, { { 'W', 'z' } }, { { 'W', 'z' } } }, true, a);
			Check(out == S("zzz"), "arbiter: held key repeats through WM_CHAR");
		}
		{
			// SendInput KEYEVENTF_UNICODE / posted WM_CHAR: no scan code, no engine char - typed, nothing paired
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'S', 0x51FD } }, { { 'E', 'k' } } }, true, a);
			Check(out == U16{ 0x51FD, 'k' }, "arbiter: synthetic WM_CHAR typed and does not swallow the next engine char");
		}
		{
			// a surrogate pair from a real key is one character: one pairing, not two
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'E', 0x1F600 }, { 'W', 0xD83D }, { 'W', 0xDE00 } } }, true, a);
			Check(out == U16{ 0xD83D, 0xDE00 }, "arbiter: surrogate pair from WM_CHAR typed as its two units, engine copy dropped");
		}
		{
			textcore::CharArbiter a;
			const U16 out = Run({ { { 'D', 0x51FD }, { 'D', 0x6570 }, { 'D', 0x1F600 } } }, true, a);
			Check(out == U16{ 0x51FD, 0x6570, 0xD83D, 0xDE00 }, "arbiter: direct code points (op=type) typed; above U+FFFF as a pair");
		}
		{
			// a WM_CHAR credit expires: a later engine char (whose WM_CHAR is lost) is still typed
			textcore::CharArbiter a;
			std::vector<std::vector<Ev>> fr{ { { 'W', 'r' } } };
			for (int i = 0; i < 10; ++i) fr.push_back({});
			fr.push_back({ { 'E', 's' } });
			const U16 out = Run(fr, true, a);
			Check(out == S("rs"), "arbiter: an old WM_CHAR does not swallow a much later engine char");
		}
		{
			textcore::CharArbiter a;
			U16 out;
			a.Engine('m', true, false, 1000, out);
			a.Reset();
			for (int i = 0; i < 10; ++i) a.EndFrame(false, 1000 + 16 * static_cast<std::uint64_t>(i), out);
			Check(out.empty() && a.Pending() == 0, "arbiter: Reset drops what was waiting (menu reopened)");
		}
	}
}

int main()
{
	Utf8();
	Utf16();
	Scale();
	KeyParam();
	Arbiter();
	std::cout << "text_input_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
