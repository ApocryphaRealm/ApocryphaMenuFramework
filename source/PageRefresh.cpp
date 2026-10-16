#include "PageRefresh.h"

#include "MfmCore.h"
#include "MfmPages.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>

namespace pagerefresh
{
	namespace
	{
		// Game Settings Override's own MFM entry (Config/Game Settings Override.toml in its download) names this function, type
		// MessageBox; it is listed here too so GSO's page reloads even when that entry file is not installed
		const refreshcore::Decl kGso{ "(Game Settings Override)", "ccld_GameSettingsOverride.dll", "ReloadConfig", 2 };

		std::mutex g_stringsLock;
		std::deque<std::string> g_strings;   // a deque never moves its elements, so a pointer into one stays good
	}

	Reload Find(const std::vector<std::string>& a_candidates)
	{
		std::vector<refreshcore::Decl> decls = mfmpages::Declarations();
		decls.push_back(kGso);
		Reload out;
		const int i = refreshcore::Match(decls, a_candidates);
		if (i >= 0)
		{
			out.found = true;
			out.decl = decls[static_cast<std::size_t>(i)];
			out.via = refreshcore::ShownName(out.decl);
		}
		return out;
	}

	bool Loaded(const Reload& a_r) { return a_r.found && ::GetModuleHandleW(mfm::Widen(a_r.decl.dll).c_str()) != nullptr; }

	Called Call(const Reload& a_r)
	{
		Called out;
		if (!a_r.found) { out.error = "no declared reload"; return out; }
		mfm::Function f;
		f.dll = a_r.decl.dll;
		f.api = a_r.decl.api;
		f.type = static_cast<mfm::Type>(a_r.decl.type);
		std::string missing;
		void* fn = mfm::Resolve(f, missing);
		if (!fn)
		{
			out.error = missing == f.dll ? std::string("not loaded") : "no export " + f.api;
			logger::info("refresh: {} ! {} ({}) - {}", f.dll, f.api, a_r.decl.rel, out.error);
			return out;
		}
		const mfm::CallResult c = mfm::Call(fn, f.type);
		out.ran = true;
		out.ok = c.ok;
		out.error = c.error;
		out.message = c.message;
		while (!out.message.empty() && (out.message.back() == '\n' || out.message.back() == '\r' || out.message.back() == ' ')) { out.message.pop_back(); }
		logger::info("refresh: {} ! {} ({}) called - {}{}", f.dll, f.api, a_r.decl.rel, c.ok ? "ok" : c.error, out.message.empty() ? std::string() : ": " + out.message);
		return out;
	}

	std::string CalledText(const Reload& a_r, const Called& a_c)
	{
		char buf[1024];
		if (a_c.ran && a_c.ok)
		{
			std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshDone", "Reloaded in game by %s: %s"), a_r.via.c_str(),
						  a_c.message.empty() ? strings::TR("AMF_RefreshNoMessage", "(no message)") : a_c.message.c_str());
		}
		else if (a_c.error == "not loaded")
		{
			std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshNotLoaded", "%s is not loaded, so nothing reloaded."), a_r.via.c_str());
		}
		else { std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshFailed", "%s's reload could not run: %s"), a_r.via.c_str(), a_c.error.c_str()); }
		return buf;
	}

	std::string MethodText(Kind a_kind, const std::string& a_via)
	{
		char buf[512];
		switch (a_kind)
		{
		case Kind::kReload:
			std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshReload", "Changes: reloaded in game (via %s) after each save."), a_via.c_str());
			return buf;
		case Kind::kLive:
			return strings::TR("AMF_RefreshLive", "Changes: applied live - set in the game's own settings at once. The game uses a setting at once "
									  "wherever it reads it live; a few are read only when the game starts.");
		default: return strings::TR("AMF_RefreshNext", "Changes: apply the next time the game starts.");
		}
	}

	RE::Setting* EngineIni(const std::string& a_key, const std::string& a_section)
	{
		const std::string name = refreshcore::EngineIniName(a_key, a_section);
		if (auto* ini = RE::INISettingCollection::GetSingleton())
		{
			if (RE::Setting* s = ini->GetSetting(name)) { return s; }
		}
		if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton())
		{
			if (RE::Setting* s = prefs->GetSetting(name)) { return s; }
		}
		return nullptr;
	}

	char* KeepString(const std::string& a_s)
	{
		std::scoped_lock l(g_stringsLock);
		g_strings.push_back(a_s);
		return g_strings.back().data();
	}

	bool SetFromText(RE::Setting* a_setting, const std::string& a_text)
	{
		if (!a_setting) { return false; }
		const char* name = a_setting->GetName();
		if (!name || !*name) { return false; }
		const std::string t = a_text;
		char* end = nullptr;
		switch (name[0])
		{
		case 'b':
		{
			std::string l = refreshcore::Lower(t);
			if (l == "1" || l == "true") { a_setting->data.b = true; return true; }
			if (l == "0" || l == "false") { a_setting->data.b = false; return true; }
			return false;
		}
		case 'f':
		{
			const double v = std::strtod(t.c_str(), &end);
			if (t.empty() || !end || *end) { return false; }
			a_setting->data.f = static_cast<float>(v);
			return true;
		}
		case 'i':
		{
			const long v = std::strtol(t.c_str(), &end, 10);
			if (t.empty() || !end || *end) { return false; }
			a_setting->data.i = static_cast<std::int32_t>(v);
			return true;
		}
		case 'u':
		{
			const unsigned long v = std::strtoul(t.c_str(), &end, 10);
			if (t.empty() || !end || *end || t[0] == '-') { return false; }
			a_setting->data.u = static_cast<std::uint32_t>(v);
			return true;
		}
		case 's':
		case 'S': a_setting->data.s = KeepString(t); return true;
		default: return false;
		}
	}
}
