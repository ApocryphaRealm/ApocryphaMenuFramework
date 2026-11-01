#include "RealSmf.h"

#include "utils/Logger.h"

#include <Windows.h>

#include <cwchar>
#include <string>

// See RealSmf.h. AMF's own imports are never redirected by the alias, so the GetFileAttributesW
// here is the real one (under Mod Organizer, usvfs's view of Data).

namespace
{
	struct Decision
	{
		bool present = false;
		std::wstring path;
	};

	Decision Decide() noexcept
	{
		Decision d;
		wchar_t path[MAX_PATH * 2]{};
		const DWORD len = ::GetModuleFileNameW(nullptr, path, static_cast<DWORD>(std::size(path)));
		if (len == 0 || len >= std::size(path)) {
			return d;
		}
		wchar_t* slash = std::wcsrchr(path, L'\\');
		if (!slash) {
			return d;
		}
		slash[1] = L'\0';
		if (::wcscat_s(path, L"Data\\SKSE\\Plugins\\SKSEMenuFramework.dll") != 0) {
			return d;
		}
		d.path = path;
		const DWORD attributes = ::GetFileAttributesW(path);
		d.present = attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
		return d;
	}

	const Decision& Get() noexcept
	{
		static const Decision s_decision = Decide();   // thread-safe once; asked from the alias hooks on any thread
		return s_decision;
	}

	std::string Narrow(const std::wstring& a_text)
	{
		if (a_text.empty()) {
			return {};
		}
		const int len = ::WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (len <= 1) {
			return {};
		}
		std::string out(static_cast<std::size_t>(len), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, a_text.c_str(), -1, out.data(), len, nullptr, nullptr);
		out.resize(static_cast<std::size_t>(len) - 1);
		return out;
	}
}

namespace realsmf
{
	bool Present() noexcept { return Get().present; }

	void Report()
	{
		const auto& d = Get();
		if (d.present) {
			logger::info("SMF alias: the real SKSE Menu Framework is installed ({}) - lookups of SKSEMenuFramework (module name and file) are left to it, so its consumers bind to it and not to AMF",
						 Narrow(d.path));
		} else {
			logger::info("SMF alias: the real SKSE Menu Framework is not installed ({} is absent) - AMF answers to the SKSEMenuFramework name",
						 d.path.empty() ? std::string("its path could not be built") : Narrow(d.path));
		}
	}
}
