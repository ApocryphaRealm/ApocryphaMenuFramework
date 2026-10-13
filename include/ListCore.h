#pragma once

// ============================================================================================================
// HOW THE MOD LIST COMPARES NAMES (2.2.0, after the final5 CJK test) - for the A-Z / Z-A sort, a newcomer's place, the
// search box and the Filter words. No game or ImGui code, so tools\list_order_check runs the same functions.
//
// Before this, names were lowered with std::tolower one BYTE at a time. That folds ASCII only, and it follows the C
// runtime's locale, which is process-wide under the shared UCRT: any module that calls setlocale changes it, and under a
// single-byte code page tolower rewrites UTF-8 lead bytes (0xC0-0xDE - every Cyrillic and Latin-1 letter's first byte).
// Fold() is locale-free and works on whole UTF-8 characters: the upper case letters of ASCII, Latin-1, Latin Extended-A
// (Polish, Czech), Greek and Cyrillic become lower case; every other character - kana, CJK, Hangul - is kept byte for byte.
//
// THE ORDER: folded names compared byte for byte, unsigned, which for UTF-8 is code point order. So Latin names come
// first (accented Latin letters after z), then Greek, then Cyrillic, then Japanese kana, then CJK ideographs, then Hangul.
// It is one rule for every language, needs no locale and never changes with the language picked.
// ============================================================================================================

#include <cstdint>
#include <string>
#include <string_view>

namespace listcore
{
	// The lower case of one code point, for the scripts named above; any other code point comes back unchanged.
	inline std::uint32_t FoldCodePoint(std::uint32_t c)
	{
		if (c >= 'A' && c <= 'Z') { return c + 0x20; }
		if (c < 0xC0) { return c; }
		if (c <= 0xDE) { return c == 0xD7 ? c : c + 0x20; }                          // Latin-1 (not the multiplication sign)
		if (c >= 0x100 && c <= 0x17F)                                                // Latin Extended-A
		{
			if ((c <= 0x12F || (c >= 0x132 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) && (c % 2 == 0)) { return c + 1; }
			if (((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) && (c % 2 == 1)) { return c + 1; }
			return c;
		}
		if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) { return c + 0x20; }            // Greek
		if (c >= 0x400 && c <= 0x40F) { return c + 0x50; }                           // Cyrillic Ѐ-Џ
		if (c >= 0x410 && c <= 0x42F) { return c + 0x20; }                           // Cyrillic А-Я
		return c;
	}

	// The name folded for comparing. Every case pair above is two bytes in UTF-8 on both sides (ASCII stays one), so a
	// folded name keeps its length; bytes that are not valid UTF-8 are copied as they are.
	inline std::string Fold(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size());
		for (std::size_t i = 0; i < a_text.size();)
		{
			const auto b0 = static_cast<unsigned char>(a_text[i]);
			if (b0 < 0x80)
			{
				out.push_back(static_cast<char>(FoldCodePoint(b0)));
				++i;
				continue;
			}
			if (b0 >= 0xC2 && b0 <= 0xDF && i + 1 < a_text.size() && (static_cast<unsigned char>(a_text[i + 1]) & 0xC0) == 0x80)
			{
				const std::uint32_t c = FoldCodePoint(((b0 & 0x1Fu) << 6) | (static_cast<unsigned char>(a_text[i + 1]) & 0x3Fu));
				out.push_back(static_cast<char>(0xC0 | (c >> 6)));
				out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
				i += 2;
				continue;
			}
			out.push_back(a_text[i]);   // a 3- or 4-byte character (kana, CJK, Hangul...) or a stray byte: unchanged
			++i;
		}
		return out;
	}

	// The list's A-Z order of two shown names (see THE ORDER above); false for names that fold alike.
	inline bool Less(std::string_view a_lhs, std::string_view a_rhs)
	{
		return Fold(a_lhs) < Fold(a_rhs);   // std::string compares char_traits<char>, which is unsigned bytes
	}

	// The search box and the Filter words: a_needle anywhere in a_name, letter case aside. An empty needle matches.
	inline bool Contains(std::string_view a_name, std::string_view a_needle)
	{
		return Fold(a_name).find(Fold(a_needle)) != std::string::npos;
	}
}
