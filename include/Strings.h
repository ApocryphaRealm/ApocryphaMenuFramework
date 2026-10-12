#pragma once

// ============================================================================================
// LANGUAGE SUPPORT (1.6.4, the author: "the foreign language compatible fomod for amf").
//
// Every string the framework itself draws - its Settings, Controls and Help pages and the
// window chrome - goes through TR("key", "English text"). The English text is the compiled
// fallback; the shown text comes from
//     Data\Interface\Translations\ApocryphaMenuFramework_<language>.txt
// the same file shape SkyUI and SKSE use (UTF-16LE with BOM, one "$key<TAB>text" per line), so
// translators already know it and every language file installs beside the others.
//
// Which language: the INI's sLanguage when set ("german", "russian", ...), otherwise the game's
// own sLanguage:General. A file for that language is read; anything it lacks falls back to the
// English file, then to the compiled text - a half-finished translation never shows a raw key.
//
// Consumer mods' pages are theirs to translate; this covers only what the framework draws.
// ============================================================================================

#include <string>
#include <vector>

namespace strings
{
	// Reads the language file(s). Call at kDataLoaded (the game's INI is readable by then) and
	// again after SetLanguage. Safe to call with no file present - English compiled text is used.
	void Load();

	// The text for a key in the active language, UTF-8, or a_english when no file has it.
	// Pointers stay valid until the next Load().
	const char* TR(const char* a_key, const char* a_english);

	// A COUNTED PHRASE IN THE RIGHT FORM (v4; the owner, 2026-10-12: the INI list said "1 tabs" and "1 settings").
	// The plural form a count takes in the shown language, by CLDR's cardinal rules for the eleven languages AMF ships:
	// "One" (English, German, Italian, Spanish: 1; French: 0 and 1; Czech, Polish: 1; Russian: 1, 21, 31 ... not 11),
	// "Few" (Czech: 2-4; Polish and Russian: 2-4, 22-24 ... not 12-14), else "Other" (Polish and Russian's "many" too).
	// Chinese, Japanese and Korean have one form: always "Other".
	const char* PluralForm(long long a_count);

	// The text for a_key + "_" + PluralForm(a_count) ("AMF_IniSettings_One"); a missing "_Few" falls back to "_Other",
	// a missing "_One" or "_Other" to the English given. The text is a format with the count's %d.
	const char* TRN(const char* a_key, long long a_count, const char* a_englishOne, const char* a_englishOther);

	// "english", "german", ... - what Load() resolved (after the override and the game's INI).
	const std::string& Language();

	// The game's own sLanguage:General, lower-case ("english" when unset) - the language its item
	// and spell names are in, which can differ from Language() when the INI overrides it (1.8.9).
	std::string GameLanguageSetting();

	// The languages for which a translation file exists on disk, lower-case, sorted - the
	// settings page's combo. "english" is always listed first.
	std::vector<std::string> Available();

	// Sets the INI override ("" = follow the game), saves it, reloads the strings and asks the
	// renderer to rebuild its font atlas for the new glyphs. Also the DevBench "language" op.
	void SetLanguage(const std::string& a_language);

	// Every loaded text concatenated - the font builder feeds it to the glyph-range builder so the
	// atlas holds exactly the characters this language needs (Cyrillic, kana, hanzi, ...).
	const std::string& AllText();
}
