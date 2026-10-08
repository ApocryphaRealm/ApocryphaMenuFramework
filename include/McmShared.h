#pragma once

// MCM loader. Helpers shared by the MCM Helper loader (McmLoader.cpp, phases 1-2) and the SkyUI
// script-menu loader (McmScripts.cpp, phase 3). Internal to AMF - not part of its API.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mcmloader::detail
{
	using Table = std::unordered_map<std::string, std::string>;  // $KEY -> text

	std::string Lower(std::string a_s);
	std::string Trim(const std::string& a_s);
	std::string Narrow(std::wstring_view a_w);
	std::string StripTags(const std::string& a_s);
	// 2.1.5 (the owner, 2026-10-07: the "$" in section headings and "underscores instead of spaces"). A $KEY no
	// translation file knows, made readable: the "$", a mod's own capitals prefix ("ATLAS_") and the underscores go and
	// camelCase words are spaced - "$ATLAS_GlobalMarkerSettings" -> "Global Marker Settings". The last resort only.
	std::string ReadableKey(const std::string& a_key);
	// 2.1.5: a translation's "\n" - two characters, as the files write it - is a line break, as SkyUI shows it.
	std::string Unescape(const std::string& a_s);
	std::string KeyName(std::int32_t a_code);

	// Interface\Translations\<plugin>_<language>.txt (game language first, English as the fallback), read through the
	// game's resource streams. Main thread, once per mod.
	Table LoadTranslations(const std::string& a_plugin);
	// The language a mod's own translation file is read in (2.1.1): the one AMF shows - picked on Appearance, or
	// the game's - so a converted MCM page follows the framework's language when the mod ships that file.
	std::string TextLanguage();
	// The mod's own file read in the GAME's language, then English - for the names a menu registers under (entry and tab).
	// Those must not change with the language picked in AMF: the entry name keys the player's order, renames and learned
	// placements (2.1.1).
	Table LoadNameTranslations(const std::string& a_plugin);

	// A script variable by name through the type chain (MCM Helper's ScriptObject::GetVariable). Null when absent.
	RE::BSScript::Variable* ScriptVar(const RE::BSTSmartPointer<RE::BSScript::Object>& a_object, std::string_view a_name);

	// Whether a menu comes into AMF (the player's choice per menu; menus never set follow [MCM] bImportNewMenus). Keys
	// are the ledger's: "mcmhelper|<mod>" and "script|<plugin>|<ModName>". Any thread.
	bool IsImported(const std::string& a_key);
	// 2.1.6: the same list for another converter's menus (Prisma: "prisma|<ModID>"). Writes McmImport.txt; the caller
	// shows or hides its own pages.
	void SetImported(const std::string& a_key, bool a_on);

	// SkyUI's config manager script object (SkyUI_SE.esp), or empty. Main thread.
	RE::BSTSmartPointer<RE::BSScript::Object> FindSkyUIManager();

	// A Papyrus argument list built from Variables (MCM Helper's Function::FunctionArguments::Make).
	class VarArgs : public RE::BSScript::IFunctionArguments
	{
	public:
		RE::BSScrapArray<RE::BSScript::Variable> args;
		bool operator()(RE::BSScrapArray<RE::BSScript::Variable>& a_dst) const override
		{
			a_dst = args;
			return true;
		}
	};

	// Runs a function once a dispatched Papyrus call has FINISHED (on a VM thread), with its result.
	class ResultFn : public RE::BSScript::IStackCallbackFunctor
	{
	public:
		explicit ResultFn(std::function<void(const RE::BSScript::Variable&)> a_fn) : _fn(std::move(a_fn)) {}
		void operator()(RE::BSScript::Variable a_result) override
		{
			if (_fn) { _fn(a_result); }
		}
		bool CanSave() const override { return false; }
		void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

	private:
		std::function<void(const RE::BSScript::Variable&)> _fn;
	};
}
