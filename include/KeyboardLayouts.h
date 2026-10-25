#pragma once

// ============================================================================================
// THE ON-SCREEN KEYBOARD'S LAYOUTS (2.3.0, the universal keyboard - plan 4. plans\amf-universal-keyboard, 4g).
//
// Pure C++ (no ImGui, no game), so tools\keyboard_layout_check.cpp runs the very code the game runs. One row set per
// layout, four character rows, keys separated by single spaces, UTF-8. Shift gives the second row set (capitals,
// katakana, Korean double consonants). LB / RB on the pad step through the layouts; "auto" starts on the game's language.
//
// The three East Asian languages (owner decision D3, 2026-10-23, taken as the plan proposed):
//   * Korean: a 2-set (Dubeolsik) jamo layout with our own syllable composition (HangulComposer below) - each jamo key
//     either starts a syllable or rewrites the one being composed, exactly as a Korean phone keyboard does.
//   * Japanese: a hiragana grid, katakana on Shift, with the voicing marks (゛ ゜) applied to the character before the
//     caret. No kanji conversion - that needs an input method.
//   * Chinese: the Latin layout; Chinese players keep the PC IME path 2.2.1 added (TextInput.cpp).
// Every layout keeps a Latin page one bumper away (the English layout is in the cycle).
// ============================================================================================

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace kblayout
{
	enum class Kind : int { kPlain = 0, kHangul = 1, kKana = 2 };

	struct Layout
	{
		const char* id;      // short id, the INI's sKeyboardLayout value ("en", "de" ...); also what DevBench names
		const char* label;   // the layout's own name, shown on the keyboard (a layout's name in its own script)
		Kind        kind;
		const char* rows[4];
		const char* shifted[4];   // nullptr = computed (kana: katakana)
	};

	// clang-format off
	inline constexpr Layout kLayouts[] = {   // nonnull: constexpr table, every id/label/row is a string literal
		{ "en", "English", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0", "q w e r t y u i o p", "a s d f g h j k l", "z x c v b n m - '" },
		  { "1 2 3 4 5 6 7 8 9 0", "Q W E R T Y U I O P", "A S D F G H J K L", "Z X C V B N M - '" } },
		{ "de", "Deutsch", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0 \xC3\x9F", "q w e r t z u i o p \xC3\xBC", "a s d f g h j k l \xC3\xB6 \xC3\xA4", "y x c v b n m - '" },
		  { "1 2 3 4 5 6 7 8 9 0 \xC3\x9F", "Q W E R T Z U I O P \xC3\x9C", "A S D F G H J K L \xC3\x96 \xC3\x84", "Y X C V B N M - '" } },
		{ "fr", "Fran\xC3\xA7" "ais", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0", "a z e r t y u i o p \xC3\xA2 \xC3\xAA", "q s d f g h j k l m \xC3\xB4 \xC3\xB9", "w x c v b n \xC3\xA9 \xC3\xA8 \xC3\xA0 \xC3\xA7 - '" },
		  { "1 2 3 4 5 6 7 8 9 0", "A Z E R T Y U I O P \xC3\x82 \xC3\x8A", "Q S D F G H J K L M \xC3\x94 \xC3\x99", "W X C V B N \xC3\x89 \xC3\x88 \xC3\x80 \xC3\x87 - '" } },
		{ "es", "Espa\xC3\xB1ol", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0 \xC2\xBF \xC2\xA1", "q w e r t y u i o p \xC3\xA1 \xC3\xA9", "a s d f g h j k l \xC3\xB1 \xC3\xAD \xC3\xB3", "z x c v b n m \xC3\xBA \xC3\xBC - '" },
		  { "1 2 3 4 5 6 7 8 9 0 \xC2\xBF \xC2\xA1", "Q W E R T Y U I O P \xC3\x81 \xC3\x89", "A S D F G H J K L \xC3\x91 \xC3\x8D \xC3\x93", "Z X C V B N M \xC3\x9A \xC3\x9C - '" } },
		{ "it", "Italiano", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0", "q w e r t y u i o p \xC3\xA8 \xC3\xA9", "a s d f g h j k l \xC3\xB2 \xC3\xA0", "z x c v b n m \xC3\xAC \xC3\xB9 - '" },
		  { "1 2 3 4 5 6 7 8 9 0", "Q W E R T Y U I O P \xC3\x88 \xC3\x89", "A S D F G H J K L \xC3\x92 \xC3\x80", "Z X C V B N M \xC3\x8C \xC3\x99 - '" } },
		{ "pl", "Polski", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0", "q w e r t y u i o p \xC4\x85 \xC4\x99", "a s d f g h j k l \xC5\x82 \xC5\x9B \xC4\x87", "z x c v b n m \xC5\xBC \xC5\xBA \xC5\x84 \xC3\xB3 -" },
		  { "1 2 3 4 5 6 7 8 9 0", "Q W E R T Y U I O P \xC4\x84 \xC4\x98", "A S D F G H J K L \xC5\x81 \xC5\x9A \xC4\x86", "Z X C V B N M \xC5\xBB \xC5\xB9 \xC5\x83 \xC3\x93 -" } },
		{ "cs", "\xC4\x8C" "e\xC5\xA1tina", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0 \xC4\x9B \xC5\xA1 \xC4\x8D \xC5\x99 \xC5\xBE", "q w e r t z u i o p \xC3\xBD \xC3\xA1 \xC3\xAD \xC3\xA9", "a s d f g h j k l \xC3\xBA \xC5\xAF \xC4\x8F \xC5\xA5", "y x c v b n m \xC5\x88 \xC3\xB3 - '" },
		  { "1 2 3 4 5 6 7 8 9 0 \xC4\x9A \xC5\xA0 \xC4\x8C \xC5\x98 \xC5\xBD", "Q W E R T Z U I O P \xC3\x9D \xC3\x81 \xC3\x8D \xC3\x89", "A S D F G H J K L \xC3\x9A \xC5\xAE \xC4\x8E \xC5\xA4", "Y X C V B N M \xC5\x87 \xC3\x93 - '" } },
		{ "ru", "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", Kind::kPlain,
		  { "1 2 3 4 5 6 7 8 9 0 \xD1\x91",
			"\xD0\xB9 \xD1\x86 \xD1\x83 \xD0\xBA \xD0\xB5 \xD0\xBD \xD0\xB3 \xD1\x88 \xD1\x89 \xD0\xB7 \xD1\x85 \xD1\x8A",
			"\xD1\x84 \xD1\x8B \xD0\xB2 \xD0\xB0 \xD0\xBF \xD1\x80 \xD0\xBE \xD0\xBB \xD0\xB4 \xD0\xB6 \xD1\x8D",
			"\xD1\x8F \xD1\x87 \xD1\x81 \xD0\xBC \xD0\xB8 \xD1\x82 \xD1\x8C \xD0\xB1 \xD1\x8E - '" },
		  { "1 2 3 4 5 6 7 8 9 0 \xD0\x81",
			"\xD0\x99 \xD0\xA6 \xD0\xA3 \xD0\x9A \xD0\x95 \xD0\x9D \xD0\x93 \xD0\xA8 \xD0\xA9 \xD0\x97 \xD0\xA5 \xD0\xAA",
			"\xD0\xA4 \xD0\xAB \xD0\x92 \xD0\x90 \xD0\x9F \xD0\xA0 \xD0\x9E \xD0\x9B \xD0\x94 \xD0\x96 \xD0\xAD",
			"\xD0\xAF \xD0\xA7 \xD0\xA1 \xD0\x9C \xD0\x98 \xD0\xA2 \xD0\xAC \xD0\x91 \xD0\xAE - '" } },
		// Japanese: hiragana; Shift = katakana (computed); ゛ and ゜ voice the character before the caret
		{ "ja", "\xE3\x81\x8B\xE3\x81\xAA", Kind::kKana,
		  { "\xE3\x81\x82 \xE3\x81\x84 \xE3\x81\x86 \xE3\x81\x88 \xE3\x81\x8A \xE3\x81\x8B \xE3\x81\x8D \xE3\x81\x8F \xE3\x81\x91 \xE3\x81\x93 \xE3\x81\x95 \xE3\x81\x97 \xE3\x81\x99 \xE3\x81\x9B \xE3\x81\x9D",
			"\xE3\x81\x9F \xE3\x81\xA1 \xE3\x81\xA4 \xE3\x81\xA6 \xE3\x81\xA8 \xE3\x81\xAA \xE3\x81\xAB \xE3\x81\xAC \xE3\x81\xAD \xE3\x81\xAE \xE3\x81\xAF \xE3\x81\xB2 \xE3\x81\xB5 \xE3\x81\xB8 \xE3\x81\xBB",
			"\xE3\x81\xBE \xE3\x81\xBF \xE3\x82\x80 \xE3\x82\x81 \xE3\x82\x82 \xE3\x82\x84 \xE3\x82\x86 \xE3\x82\x88 \xE3\x82\x89 \xE3\x82\x8A \xE3\x82\x8B \xE3\x82\x8C \xE3\x82\x8D \xE3\x82\x8F \xE3\x82\x92",
			"\xE3\x82\x93 \xE3\x82\x83 \xE3\x82\x85 \xE3\x82\x87 \xE3\x81\xA3 \xE3\x81\x81 \xE3\x81\x83 \xE3\x81\x85 \xE3\x81\x87 \xE3\x81\x89 \xE3\x83\xBC \xE3\x82\x9B \xE3\x82\x9C \xE3\x80\x81 \xE3\x80\x82" },
		  { nullptr, nullptr, nullptr, nullptr } },
		// Korean 2-set (Dubeolsik); Shift = the double consonants and ㅒ ㅖ
		{ "ko", "\xED\x95\x9C\xEA\xB8\x80", Kind::kHangul,
		  { "1 2 3 4 5 6 7 8 9 0",
			"\xE3\x85\x82 \xE3\x85\x88 \xE3\x84\xB7 \xE3\x84\xB1 \xE3\x85\x85 \xE3\x85\x9B \xE3\x85\x95 \xE3\x85\x91 \xE3\x85\x90 \xE3\x85\x94",
			"\xE3\x85\x81 \xE3\x84\xB4 \xE3\x85\x87 \xE3\x84\xB9 \xE3\x85\x8E \xE3\x85\x97 \xE3\x85\x93 \xE3\x85\x8F \xE3\x85\xA3",
			"\xE3\x85\x8B \xE3\x85\x8C \xE3\x85\x8A \xE3\x85\x8D \xE3\x85\xA0 \xE3\x85\x9C \xE3\x85\xA1 - '" },
		  { "1 2 3 4 5 6 7 8 9 0",
			"\xE3\x85\x83 \xE3\x85\x89 \xE3\x84\xB8 \xE3\x84\xB2 \xE3\x85\x86 \xE3\x85\x9B \xE3\x85\x95 \xE3\x85\x91 \xE3\x85\x92 \xE3\x85\x96",
			"\xE3\x85\x81 \xE3\x84\xB4 \xE3\x85\x87 \xE3\x84\xB9 \xE3\x85\x8E \xE3\x85\x97 \xE3\x85\x93 \xE3\x85\x8F \xE3\x85\xA3",
			"\xE3\x85\x8B \xE3\x85\x8C \xE3\x85\x8A \xE3\x85\x8D \xE3\x85\xA0 \xE3\x85\x9C \xE3\x85\xA1 - '" } },
	};
	// clang-format on
	inline constexpr int kLayoutCount = static_cast<int>(sizeof(kLayouts) / sizeof(kLayouts[0]));

	inline int FindLayout(std::string_view a_id)
	{
		for (int i = 0; i < kLayoutCount; ++i)
		{
			// nonnull: kLayouts is a constexpr table; every id is a string literal
			if (a_id == kLayouts[i].id) { return i; }
		}
		return -1;
	}

	// The layout a language starts on (strings::Language() / the game's sLanguage, lower case). Chinese -> English (D3).
	inline const char* LayoutForLanguage(std::string_view a_language)
	{
		struct Map { const char* lang; const char* id; };
		static constexpr Map kMap[] = { { "german", "de" }, { "french", "fr" }, { "spanish", "es" }, { "italian", "it" },
			{ "polish", "pl" }, { "czech", "cs" }, { "russian", "ru" }, { "japanese", "ja" }, { "korean", "ko" } };
		for (const auto& m : kMap)
		{
			// nonnull: m is an element of the constexpr kMap above, whose lang fields are string literals
			if (a_language == m.lang) { return m.id; }
		}
		return "en";
	}

	// ---- the CJK fallback in the GAME's text boxes (2.3.0, batch 12 T12) ------------------------------
	// The game's own font has no Hangul or kana (한글 and が arrived in its boxes and drew as empty boxes), so in a game box the
	// Korean and Japanese layouts give way to the Latin one (index 0, "en") unless the player turned bKeyboardNativeCjkInGame on.
	// AMF's own window (a_gameBox false) keeps every layout. Chinese is typed on the Latin layout everywhere (D3).
	inline bool FallsBack(bool a_gameBox, bool a_nativeCjkInGame) { return a_gameBox && !a_nativeCjkInGame; }

	inline int EffectiveLayout(int a_chosen, bool a_gameBox, bool a_nativeCjkInGame)
	{
		if (a_chosen < 0 || a_chosen >= kLayoutCount) { return 0; }
		if (FallsBack(a_gameBox, a_nativeCjkInGame) && kLayouts[a_chosen].kind != Kind::kPlain) { return 0; }
		return a_chosen;
	}

	// LB / RB: the next chosen layout a_dir away, stepping over the Korean and Japanese ones where they fall back.
	inline int NextLayout(int a_chosen, int a_dir, bool a_gameBox, bool a_nativeCjkInGame)
	{
		int i = (a_chosen + a_dir % kLayoutCount + kLayoutCount) % kLayoutCount;
		for (int guard = 0; FallsBack(a_gameBox, a_nativeCjkInGame) && kLayouts[i].kind != Kind::kPlain && guard < kLayoutCount; ++guard)
		{
			i = (i + a_dir % kLayoutCount + kLayoutCount) % kLayoutCount;
		}
		return i;
	}

	// ---- UTF-8 helpers ----------------------------------------------------------------------------
	inline std::string Utf8(char32_t a_cp)
	{
		std::string out;
		if (a_cp < 0x80) { out += static_cast<char>(a_cp); }
		else if (a_cp < 0x800) { out += static_cast<char>(0xC0 | (a_cp >> 6)); out += static_cast<char>(0x80 | (a_cp & 0x3F)); }
		else if (a_cp < 0x10000)
		{
			out += static_cast<char>(0xE0 | (a_cp >> 12));
			out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (a_cp & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (a_cp >> 18));
			out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (a_cp & 0x3F));
		}
		return out;
	}

	// First code point of a UTF-8 string (0 for empty). Only well-formed input reaches here (our own tables).
	inline char32_t FirstCodePoint(std::string_view a_s)
	{
		if (a_s.empty()) { return 0; }
		const auto b0 = static_cast<unsigned char>(a_s[0]);
		auto cont = [&](std::size_t i) -> char32_t { return i < a_s.size() ? static_cast<unsigned char>(a_s[i]) & 0x3Fu : 0u; };
		if (b0 < 0x80) { return b0; }
		if ((b0 & 0xE0) == 0xC0) { return ((b0 & 0x1Fu) << 6) | cont(1); }
		if ((b0 & 0xF0) == 0xE0) { return ((b0 & 0x0Fu) << 12) | (cont(1) << 6) | cont(2); }
		return ((b0 & 0x07u) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3);
	}

	// Removes the last code point of a UTF-8 string (Backspace on the expected-text model).
	inline void PopCodePoint(std::string& a_s)
	{
		while (!a_s.empty())
		{
			const auto b = static_cast<unsigned char>(a_s.back());
			a_s.pop_back();
			if ((b & 0xC0) != 0x80) { break; }
		}
	}

	inline std::vector<std::string> SplitKeys(const char* a_row)
	{
		std::vector<std::string> keys;
		if (!a_row) { return keys; }
		std::string cur;
		for (const char* p = a_row; *p; ++p)
		{
			if (*p == ' ') { if (!cur.empty()) { keys.push_back(cur); cur.clear(); } }
			else { cur += *p; }
		}
		if (!cur.empty()) { keys.push_back(cur); }
		return keys;
	}

	// ---- Japanese --------------------------------------------------------------------------------
	inline char32_t ToKatakana(char32_t a_c) { return (a_c >= 0x3041 && a_c <= 0x3096) ? a_c + 0x60 : a_c; }

	inline constexpr char32_t kDakuten = 0x309B;      // ゛
	inline constexpr char32_t kHandakuten = 0x309C;   // ゜

	// The voiced (semi = false) or half-voiced (semi = true) form of a kana, hiragana or katakana; 0 when it has none.
	inline char32_t KanaVoice(char32_t a_c, bool a_semi)
	{
		const bool kata = a_c >= 0x30A1 && a_c <= 0x30F6;
		const char32_t h = kata ? a_c - 0x60 : a_c;
		char32_t out = 0;
		if (!a_semi)
		{
			// か..ぢ and つ..ど: the voiced form is the next code point for the unvoiced ones on an odd/even step
			static constexpr char32_t kPairs[][2] = {
				{ 0x304B, 0x304C }, { 0x304D, 0x304E }, { 0x304F, 0x3050 }, { 0x3051, 0x3052 }, { 0x3053, 0x3054 },   // かきくけこ
				{ 0x3055, 0x3056 }, { 0x3057, 0x3058 }, { 0x3059, 0x305A }, { 0x305B, 0x305C }, { 0x305D, 0x305E },   // さしすせそ
				{ 0x305F, 0x3060 }, { 0x3061, 0x3062 }, { 0x3064, 0x3065 }, { 0x3066, 0x3067 }, { 0x3068, 0x3069 },   // たちつてと
				{ 0x306F, 0x3070 }, { 0x3072, 0x3073 }, { 0x3075, 0x3076 }, { 0x3078, 0x3079 }, { 0x307B, 0x307C },   // はひふへほ
				{ 0x3046, 0x3094 } };                                                                             // う -> ゔ
			for (const auto& p : kPairs) { if (p[0] == h) { out = p[1]; break; } }
		}
		else
		{
			static constexpr char32_t kPairs[][2] = { { 0x306F, 0x3071 }, { 0x3072, 0x3074 }, { 0x3075, 0x3077 }, { 0x3078, 0x307A }, { 0x307B, 0x307D } };
			for (const auto& p : kPairs) { if (p[0] == h) { out = p[1]; break; } }
		}
		if (out == 0) { return 0; }
		return kata ? out + 0x60 : out;
	}

	// ---- Korean ---------------------------------------------------------------------------------
	// What a key press does to the field: delete this many characters before the caret, then insert this text.
	struct Edit
	{
		int backspaces = 0;
		std::string insert;
		bool operator==(const Edit& a_o) const { return backspaces == a_o.backspaces && insert == a_o.insert; }
	};

	class HangulComposer
	{
	public:
		// One compatibility jamo (U+3131..U+3163). Returns the edit that shows the new state.
		Edit Feed(char32_t a_jamo)
		{
			const bool composing = Composing();
			const std::string before = composing ? Render() : std::string();
			std::string committed;
			if (IsVowel(a_jamo)) { FeedVowel(a_jamo, committed); }
			else { FeedConsonant(a_jamo, committed); }
			Edit e;
			e.backspaces = composing ? 1 : 0;
			e.insert = composing ? committed + Render() : committed + Render();
			(void)before;
			return e;
		}

		// Backspace while composing: the last jamo comes off the syllable. Returns false (and does nothing) when nothing is
		// being composed - the caller then sends a plain Backspace.
		bool Backspace(Edit& a_out)
		{
			if (!Composing()) { return false; }
			std::vector<char32_t> inputs = m_inputs;
			inputs.pop_back();
			Reset();
			std::string committed;
			for (char32_t j : inputs)
			{
				if (IsVowel(j)) { FeedVowel(j, committed); }
				else { FeedConsonant(j, committed); }
			}
			a_out.backspaces = 1;
			a_out.insert = committed + Render();
			return true;
		}

		void Reset() { m_l = m_v = m_t = -1; m_inputs.clear(); }
		bool Composing() const { return m_l >= 0 || m_v >= 0; }

		static bool IsJamo(char32_t a_c) { return a_c >= 0x3131 && a_c <= 0x3163; }
		static bool IsVowel(char32_t a_c) { return a_c >= 0x314F && a_c <= 0x3163; }

	private:
		// compatibility jamo -> choseong index (19)
		static int Cho(char32_t a_c)
		{
			static constexpr char32_t k[] = { 0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
				0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			for (int i = 0; i < 19; ++i) { if (k[i] == a_c) { return i; } }
			return -1;
		}
		static char32_t ChoJamo(int a_i)
		{
			static constexpr char32_t k[] = { 0x3131, 0x3132, 0x3134, 0x3137, 0x3138, 0x3139, 0x3141, 0x3142, 0x3143, 0x3145,
				0x3146, 0x3147, 0x3148, 0x3149, 0x314A, 0x314B, 0x314C, 0x314D, 0x314E };
			return k[a_i];
		}
		// jongseong index 1..27 for a compatibility jamo, 0 if it cannot be a final
		static int Jong(char32_t a_c)
		{
			static constexpr char32_t k[] = { 0, 0x3131, 0x3132, 0x3133, 0x3134, 0x3135, 0x3136, 0x3137, 0x3139, 0x313A, 0x313B,
				0x313C, 0x313D, 0x313E, 0x313F, 0x3140, 0x3141, 0x3142, 0x3144, 0x3145, 0x3146, 0x3147, 0x3148, 0x314A, 0x314B,
				0x314C, 0x314D, 0x314E };
			for (int i = 1; i < 28; ++i) { if (k[i] == a_c) { return i; } }
			return 0;
		}
		static char32_t CompoundVowel(char32_t a, char32_t b)
		{
			if (a == 0x3157 && b == 0x314F) { return 0x3158; }   // ㅗ+ㅏ=ㅘ
			if (a == 0x3157 && b == 0x3150) { return 0x3159; }   // ㅗ+ㅐ=ㅙ
			if (a == 0x3157 && b == 0x3163) { return 0x315A; }   // ㅗ+ㅣ=ㅚ
			if (a == 0x315C && b == 0x3153) { return 0x315D; }   // ㅜ+ㅓ=ㅝ
			if (a == 0x315C && b == 0x3154) { return 0x315E; }   // ㅜ+ㅔ=ㅞ
			if (a == 0x315C && b == 0x3163) { return 0x315F; }   // ㅜ+ㅣ=ㅟ
			if (a == 0x3161 && b == 0x3163) { return 0x3162; }   // ㅡ+ㅣ=ㅢ
			return 0;
		}
		struct Pair { char32_t a, b, c; };
		static constexpr Pair kFinals[] = { { 0x3131, 0x3145, 0x3133 }, { 0x3134, 0x3148, 0x3135 }, { 0x3134, 0x314E, 0x3136 },
			{ 0x3139, 0x3131, 0x313A }, { 0x3139, 0x3141, 0x313B }, { 0x3139, 0x3142, 0x313C }, { 0x3139, 0x3145, 0x313D },
			{ 0x3139, 0x314C, 0x313E }, { 0x3139, 0x314D, 0x313F }, { 0x3139, 0x314E, 0x3140 }, { 0x3142, 0x3145, 0x3144 } };
		static char32_t CompoundFinal(char32_t a, char32_t b)
		{
			for (const auto& p : kFinals) { if (p.a == a && p.b == b) { return p.c; } }
			return 0;
		}
		static bool SplitFinal(char32_t c, char32_t& a, char32_t& b)
		{
			for (const auto& p : kFinals) { if (p.c == c) { a = p.a; b = p.b; return true; } }
			return false;
		}

		std::string Render() const
		{
			if (m_l >= 0 && m_v >= 0)
			{
				const int vi = static_cast<int>(m_vJamo - 0x314F);
				const int ti = m_t >= 0 ? Jong(m_tJamo) : 0;
				return Utf8(static_cast<char32_t>(0xAC00 + (m_l * 21 + vi) * 28 + ti));
			}
			if (m_l >= 0) { return Utf8(ChoJamo(m_l)); }
			if (m_v >= 0) { return Utf8(m_vJamo); }
			return {};
		}

		void Commit(std::string& a_committed)
		{
			a_committed += Render();
			Reset();
		}

		void FeedConsonant(char32_t a_c, std::string& a_committed)
		{
			if (m_l >= 0 && m_v >= 0 && m_t < 0 && Jong(a_c) != 0)
			{
				m_t = 1; m_tJamo = a_c; m_inputs.push_back(a_c); return;
			}
			if (m_l >= 0 && m_v >= 0 && m_t >= 0)
			{
				if (const char32_t c = CompoundFinal(m_tJamo, a_c); c != 0) { m_tJamo = c; m_inputs.push_back(a_c); return; }
			}
			if (Composing()) { Commit(a_committed); }
			m_l = Cho(a_c);
			if (m_l < 0) { a_committed += Utf8(a_c); return; }   // not a choseong (a lone compound final) - typed as itself
			m_inputs.push_back(a_c);
		}

		void FeedVowel(char32_t a_v, std::string& a_committed)
		{
			if (m_l >= 0 && m_v >= 0 && m_t >= 0)
			{
				// the final moves to the new syllable: 각 + ㅏ -> 가가; 닭 + ㅏ -> 달가
				char32_t keep = 0, move = m_tJamo;
				char32_t a = 0, b = 0;
				if (SplitFinal(m_tJamo, a, b)) { keep = a; move = b; }
				if (keep) { m_tJamo = keep; } else { m_t = -1; }
				Commit(a_committed);
				m_l = Cho(move);
				m_inputs.push_back(move);
				m_v = 1; m_vJamo = a_v; m_inputs.push_back(a_v);
				return;
			}
			if (m_v >= 0 && m_t < 0)
			{
				if (const char32_t c = CompoundVowel(m_vJamo, a_v); c != 0) { m_vJamo = c; m_inputs.push_back(a_v); return; }
				Commit(a_committed);
				m_v = 1; m_vJamo = a_v; m_inputs.push_back(a_v);
				return;
			}
			if (m_l >= 0 && m_v < 0) { m_v = 1; m_vJamo = a_v; m_inputs.push_back(a_v); return; }
			m_v = 1; m_vJamo = a_v; m_inputs.push_back(a_v);
		}

		int m_l = -1;               // choseong index, -1 none
		int m_v = -1;               // >= 0 when a vowel is held (m_vJamo)
		int m_t = -1;               // >= 0 when a final is held (m_tJamo)
		char32_t m_vJamo = 0;
		char32_t m_tJamo = 0;
		std::vector<char32_t> m_inputs;   // the jamo typed into the syllable being composed, for Backspace
	};
}
