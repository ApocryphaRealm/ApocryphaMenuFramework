#pragma once

// EXPERIMENTAL (exp/mcm-loader). Helpers shared by the MCM Helper loader (McmLoader.cpp, phases 1-2) and the SkyUI
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
	std::string KeyName(std::int32_t a_code);

	// Interface\Translations\<plugin>_<language>.txt (game language first, English as the fallback), read through the
	// game's resource streams. Main thread, once per mod.
	Table LoadTranslations(const std::string& a_plugin);

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
