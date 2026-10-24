// Off-game check of the on-screen keyboard's layouts (include/KeyboardLayouts.h - the same code the framework runs, 2.3.0):
// every layout's rows split into single-character keys that fit the key grid, Shift rows match their rows key for key,
// the languages pick their layout, Korean jamo compose into syllables (and Backspace takes them apart), the kana
// voicing marks voice the character before the caret, and the UTF-8 helpers.
//   tools\keyboard_layout_check.bat
// Prints one line per check and "keyboard_layout_check: N of N passed"; exits 1 on any failure.
#include "../include/KeyboardLayouts.h"

#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	// the field, as the keyboard's edits leave it
	struct Field
	{
		std::string text;
		void Apply(const kblayout::Edit& a_e)
		{
			for (int i = 0; i < a_e.backspaces; ++i) { kblayout::PopCodePoint(text); }
			text += a_e.insert;
		}
	};

	std::string TypeJamo(std::initializer_list<char32_t> a_jamo)
	{
		kblayout::HangulComposer h;
		Field f;
		for (char32_t j : a_jamo) { f.Apply(h.Feed(j)); }
		return f.text;
	}

	void Layouts()
	{
		using namespace kblayout;
		for (int i = 0; i < kLayoutCount; ++i)
		{
			const Layout& l = kLayouts[i];
			bool fits = true, single = true, shiftMatch = true;
			for (int r = 0; r < 4; ++r)
			{
				const auto keys = SplitKeys(l.rows[r]);
				if (keys.empty() || keys.size() > 16) { fits = false; }
				for (const auto& k : keys)
				{
					if (Utf8(FirstCodePoint(k)) != k) { single = false; }
				}
				if (l.shifted[r] && SplitKeys(l.shifted[r]).size() != keys.size()) { shiftMatch = false; }
			}
			Check(fits, std::string("layout ") + l.id + ": four rows of 1-16 keys");
			Check(single, std::string("layout ") + l.id + ": every key is one character");
			Check(shiftMatch, std::string("layout ") + l.id + ": Shift rows match key for key");
			Check(FindLayout(l.id) == i, std::string("layout ") + l.id + ": found by its id");
		}
		Check(std::string(LayoutForLanguage("german")) == "de", "language german -> de");
		Check(std::string(LayoutForLanguage("korean")) == "ko", "language korean -> ko");
		Check(std::string(LayoutForLanguage("japanese")) == "ja", "language japanese -> ja");
		Check(std::string(LayoutForLanguage("chinese")) == "en", "language chinese -> en (D3: Chinese keeps the IME)");
		Check(std::string(LayoutForLanguage("english")) == "en", "language english -> en");
		Check(FindLayout("xx") == -1, "an unknown layout id is not found");
	}

	void Hangul()
	{
		// ㅎ ㅏ ㄴ ㄱ ㅡ ㄹ -> 한글
		Check(TypeJamo({ 0x314E, 0x314F, 0x3134, 0x3131, 0x3161, 0x3139 }) == "\xED\x95\x9C\xEA\xB8\x80", "hangul: h a n g eu l -> han-geul");
		// ㄱ ㅏ -> 가
		Check(TypeJamo({ 0x3131, 0x314F }) == "\xEA\xB0\x80", "hangul: g a -> ga");
		// ㄱ ㅏ ㄱ ㅏ -> 가가 (the final moves to the next syllable)
		Check(TypeJamo({ 0x3131, 0x314F, 0x3131, 0x314F }) == "\xEA\xB0\x80\xEA\xB0\x80", "hangul: g a g a -> ga ga, the final moves on");
		// ㄷ ㅏ ㄹ ㄱ ㅏ -> 달가 (a compound final splits)
		Check(TypeJamo({ 0x3137, 0x314F, 0x3139, 0x3131, 0x314F }) == "\xEB\x8B\xAC\xEA\xB0\x80", "hangul: d a l g a -> dal ga, the compound final splits");
		// ㄷ ㅏ ㄹ ㄱ -> 닭
		Check(TypeJamo({ 0x3137, 0x314F, 0x3139, 0x3131 }) == "\xEB\x8B\xAD", "hangul: d a l g -> dalg (compound final)");
		// ㄱ ㅗ ㅏ -> 과 (a compound vowel)
		Check(TypeJamo({ 0x3131, 0x3157, 0x314F }) == "\xEA\xB3\xBC", "hangul: g o a -> gwa (compound vowel)");
		// ㄱ ㄱ -> ㄱㄱ (two lone consonants)
		Check(TypeJamo({ 0x3131, 0x3131 }) == "\xE3\x84\xB1\xE3\x84\xB1", "hangul: g g -> two lone jamo");
		// ㄸ cannot be a final: 따 + ㄸ -> 따ㄸ
		Check(TypeJamo({ 0x3138, 0x314F, 0x3138 }) == "\xEB\x94\xB0\xE3\x84\xB8", "hangul: dd a dd -> dda + lone dd (no dd final)");
		// Backspace: 과 -> 고 -> ㄱ -> nothing, then a plain Backspace
		{
			kblayout::HangulComposer h;
			Field f;
			for (char32_t j : { 0x3131, 0x3157, 0x314F }) { f.Apply(h.Feed(j)); }
			kblayout::Edit e;
			bool ok = h.Backspace(e); f.Apply(e);
			Check(ok && f.text == "\xEA\xB3\xA0", "hangul: Backspace on gwa -> go");
			ok = h.Backspace(e); f.Apply(e);
			Check(ok && f.text == "\xE3\x84\xB1", "hangul: Backspace on go -> lone g");
			ok = h.Backspace(e); f.Apply(e);
			Check(ok && f.text.empty(), "hangul: Backspace on lone g -> empty");
			Check(!h.Backspace(e), "hangul: Backspace with nothing composed is a plain Backspace");
		}
		// a lone vowel
		Check(TypeJamo({ 0x314F }) == "\xE3\x85\x8F", "hangul: a lone vowel stays itself");
	}

	void Kana()
	{
		using namespace kblayout;
		Check(KanaVoice(0x304B, false) == 0x304C, "kana: ka + dakuten -> ga");
		Check(KanaVoice(0x306F, true) == 0x3071, "kana: ha + handakuten -> pa");
		Check(KanaVoice(0x30AB, false) == 0x30AC, "kana: katakana KA + dakuten -> GA");
		Check(KanaVoice(0x3042, false) == 0, "kana: a has no voiced form");
		Check(KanaVoice(0x304B, true) == 0, "kana: ka has no half-voiced form");
		Check(KanaVoice(0x3046, false) == 0x3094, "kana: u + dakuten -> vu");
		Check(ToKatakana(0x3042) == 0x30A2, "kana: a -> katakana A");
		Check(ToKatakana(0x30FC) == 0x30FC, "kana: the long mark stays");
	}

	void Utf()
	{
		using namespace kblayout;
		std::string s = "a\xC3\xA9\xE3\x81\x82";
		PopCodePoint(s);
		Check(s == "a\xC3\xA9", "utf8: pop a 3-byte character");
		PopCodePoint(s);
		Check(s == "a", "utf8: pop a 2-byte character");
		PopCodePoint(s); PopCodePoint(s);
		Check(s.empty(), "utf8: pop past empty is safe");
		Check(Utf8(0x1F600) == "\xF0\x9F\x98\x80" && FirstCodePoint("\xF0\x9F\x98\x80") == 0x1F600, "utf8: 4-byte round trip");
	}
}

int main()
{
	Layouts();
	Hangul();
	Kana();
	Utf();
	std::cout << "keyboard_layout_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
