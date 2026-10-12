// AMF-Host-OAR - hosts Open Animation Replacer's live ImGui menu inside Apocrypha Menu Framework's page.
// Copyright (c) ApocryphaRealm. GPL-3.0-or-later (the repository's LICENSE).
//
// 4. plans\amf-imgui-hosting\FINDINGS.md, method b1 ("snap-docking"). Open Animation Replacer (OAR) draws its own menu
// with its own Dear ImGui 1.89.6, linked into its DLL. AMF (Dear ImGui 1.90.8) cannot draw it, so AMF leaves an empty
// rectangle in its page and this bridge - built against OAR's ImGui version, so OAR's ImGuiContext reads correctly here -
// moves OAR's main window into that rectangle once a frame. OAR then draws its real menu there, live, on top of AMF.
//
// HOW IT IS CALLED. At kPostLoad the bridge registers with AMF (AMF_RegisterHostBridge, AMF/API.h). From then on AMF calls
// OnFrame once a frame on the render thread, after AMF's own frame is drawn: AMF's present hook runs INSIDE OAR's (OAR's
// hook calls the game's present - and AMF - first, then does its own NewFrame ... Render), so the call always falls
// between two of OAR's frames.
//
// FAIL CLOSED (FINDINGS section 5). Before anything is written into OAR's context:
//   1. OAR's DLL - its file version AND its SHA-256 - is on kAllowlist (a build this bridge was checked against);
//   2. the context is Initialized and its IO.BackendRendererName is OAR's own "imgui_impl_dx11" string (inside OAR's
//      image) - the proof the struct is read at the right layout;
//   3. the window found by name carries OAR's title, its Pos / Size are finite, and the rectangle lies inside OAR's display;
//   4. the call is on the render thread (the first caller's thread, every time) and outside OAR's NewFrame -> Render
//      (ctx->WithinFrameScope is false);
//   5. the bridge's own current-context pointer is put back afterwards.
// Any failure turns hosting off for the session (one log line, AMF_HostBridgeFailed), and AMF falls back to its launcher.
// Nothing of OAR's code is used; OAR's public UI API header is (include/OAR, verbatim, with its notice).

#include <SKSE/SKSE.h>

#ifndef IMGUI_DEFINE_MATH_OPERATORS   // CMakeLists defines it, as OAR's PCH.h does - no layout effect, kept so the build matches OAR's
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui.h>
#include <imgui_internal.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <Windows.h>
#include <Psapi.h>
#include <bcrypt.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

#include "AMF/API.h"
#include "HostLogic.h"
#include "OAR/OpenAnimationReplacerAPI-UI.h"

namespace
{
	// Builds of Open Animation Replacer this bridge has been checked against: its ImGui pin (1.89.6, no docking), its
	// main window's title, and the context fields read below. A new OAR build is added only after the same check.
	struct Allowed
	{
		const char* version;   // the DLL's file version
		const char* sha256;    // of OpenAnimationReplacer.dll
	};
	constexpr Allowed kAllowlist[] = {
		{ "3.0.2.0", "a433cf83af7878dcad14177fd6ff209fbe5dba5059741f18b4df82e24400af4e" },   // Nexus 92109, 3.0.2 (2026-02-23; source db3cf0b)
	};

	using RegisterFn = bool (*)(const char*, std::uint32_t, AMF_HostFrameCallback, void*);
	using FailedFn = void (*)(const char*, const char*);

	FailedFn g_amfFailed = nullptr;
	OAR_API::UI::IUIInterface2* g_oar = nullptr;   // V2: GetImGuiContext (in OAR 3.0.2 and 3.2.1 alike)
	std::uintptr_t g_oarBase = 0, g_oarEnd = 0;
	std::string g_title;                            // "Open Animation Replacer 3.0.2" - OAR's main window

	bool g_disabled = false;
	DWORD g_renderThread = 0;
	bool g_noContextLogged = false;
	hostlogic::State g_state;

	void InitLog()
	{
		auto dir = SKSE::log::log_directory();
		if (!dir) { return; }
		const auto path = *dir / "AMF-Host-OAR.log";
		auto log = std::make_shared<spdlog::logger>("AMF-Host-OAR", std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
		log->set_level(spdlog::level::info);
		log->flush_on(spdlog::level::info);
		log->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
		spdlog::set_default_logger(std::move(log));
	}

	void Fail(const std::string& a_why)
	{
		if (g_disabled) { return; }
		g_disabled = true;
		spdlog::error("hosting OFF for this session: {} - AMF opens Open Animation Replacer with its launcher instead", a_why);
		if (g_amfFailed) { g_amfFailed("oar", a_why.c_str()); }
	}

	std::string Sha256File(const std::wstring& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		if (!f) { return std::string(); }
		BCRYPT_ALG_HANDLE alg = nullptr;
		BCRYPT_HASH_HANDLE hash = nullptr;
		std::string out;
		if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 && BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0)
		{
			std::vector<char> buf(1 << 16);
			bool ok = true;
			while (f)
			{
				f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
				const auto n = static_cast<ULONG>(f.gcount());
				if (n && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buf.data()), n, 0) != 0) { ok = false; break; }
			}
			unsigned char digest[32] = {};
			if (ok && BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
			{
				for (unsigned char b : digest) { out += std::format("{:02x}", b); }
			}
		}
		if (hash) { BCryptDestroyHash(hash); }
		if (alg) { BCryptCloseAlgorithmProvider(alg, 0); }
		return out;
	}

	// "3.0.2.0" from the version resource's fixed part; empty when there is none
	std::string FileVersion(const std::wstring& a_path, unsigned& a_major, unsigned& a_minor, unsigned& a_patch)
	{
		DWORD handle = 0;
		const DWORD size = GetFileVersionInfoSizeW(a_path.c_str(), &handle);
		if (size == 0) { return std::string(); }
		std::vector<BYTE> data(size);
		if (!GetFileVersionInfoW(a_path.c_str(), 0, size, data.data())) { return std::string(); }
		VS_FIXEDFILEINFO* fi = nullptr;
		UINT len = 0;
		if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) { return std::string(); }
		a_major = HIWORD(fi->dwFileVersionMS);
		a_minor = LOWORD(fi->dwFileVersionMS);
		a_patch = HIWORD(fi->dwFileVersionLS);
		return std::format("{}.{}.{}.{}", a_major, a_minor, a_patch, LOWORD(fi->dwFileVersionLS));
	}

	// The body of the frame call. AMF calls it inside an SEH guard; a fault there turns hosting off on AMF's side too.
	void OnFrame(AMF_HostFrame* a_f, void*)
	{
		if (!a_f || g_disabled || !g_oar) { return; }
		if (a_f->size < sizeof(AMF_HostFrame))
		{
			Fail("AMF's frame record is smaller than this bridge expects");
			return;
		}
		// 4. the render thread - the first caller's thread, every time
		const DWORD tid = GetCurrentThreadId();
		if (g_renderThread == 0) { g_renderThread = tid; }
		else if (tid != g_renderThread)
		{
			Fail(std::format("called on thread {} - the render thread is {}", tid, g_renderThread));
			return;
		}

		auto* ctx = static_cast<ImGuiContext*>(g_oar->GetImGuiContext());
		if (!ctx)
		{
			// OAR's UI is off (bEnableUI = false installs no UI) or not set up yet: nothing to read, nothing to host
			if (!g_noContextLogged)
			{
				g_noContextLogged = true;
				spdlog::info("Open Animation Replacer has no ImGui context (yet) - its menu cannot be hosted until it has one");
			}
			a_f->targetOpen = -1;
			return;
		}
		// 2-5. the context, OAR's window and the rectangle (HostLogic.h - the code tools\host_oar_check.cpp runs offline)
		const hostlogic::Result r = hostlogic::Frame(ctx, a_f, g_state, g_oarBase, g_oarEnd, g_title);
		for (const std::string& line : r.info) { spdlog::info("{}", line); }
		for (const std::string& line : r.warn) { spdlog::warn("{}", line); }
		if (!r.fail.empty()) { Fail(r.fail); }
	}

	void Setup()
	{
		HMODULE amf = GetModuleHandleW(L"!ApocryphaMenuFramework.dll");
		if (!amf) { amf = GetModuleHandleW(L"ApocryphaMenuFramework.dll"); }
		if (!amf)
		{
			spdlog::info("Apocrypha Menu Framework is not loaded - nothing to do");
			return;
		}
		const auto reg = reinterpret_cast<RegisterFn>(GetProcAddress(amf, "AMF_RegisterHostBridge"));
		g_amfFailed = reinterpret_cast<FailedFn>(GetProcAddress(amf, "AMF_HostBridgeFailed"));
		if (!reg || !g_amfFailed)
		{
			spdlog::info("this Apocrypha Menu Framework has no host bridge surface (it is older than this bridge) - nothing to do");
			return;
		}
		HMODULE oar = GetModuleHandleW(L"OpenAnimationReplacer.dll");
		if (!oar)
		{
			spdlog::info("Open Animation Replacer is not loaded - nothing to host");
			return;
		}
		MODULEINFO mi{};
		if (GetModuleInformation(GetCurrentProcess(), oar, &mi, sizeof(mi)))
		{
			g_oarBase = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
			g_oarEnd = g_oarBase + mi.SizeOfImage;
		}
		wchar_t pathBuf[MAX_PATH * 2] = {};
		const DWORD n = GetModuleFileNameW(oar, pathBuf, static_cast<DWORD>(std::size(pathBuf)));
		const std::wstring path(pathBuf, n);
		unsigned major = 0, minor = 0, patch = 0;
		const std::string version = FileVersion(path, major, minor, patch);
		const std::string sha = Sha256File(path);
		g_title = std::format("Open Animation Replacer {}.{}.{}", major, minor, patch);
		bool allowed = false;
		for (const Allowed& a : kAllowlist)
		{
			if (version == a.version && sha == a.sha256) { allowed = true; }
		}
		spdlog::info("Open Animation Replacer {} (sha256 {}) - {}", version.empty() ? "(no version)" : version, sha.empty() ? "(unreadable)" : sha,
					 allowed ? "a build this bridge was checked against" : "NOT a build this bridge was checked against");

		using Request = OAR_API::UI::IUIInterface* (*)(OAR_API::UI::InterfaceVersion, const char*, REL::Version);
		const auto request = reinterpret_cast<Request>(GetProcAddress(oar, "RequestPluginAPI_UI"));
		if (request) { g_oar = request(OAR_API::UI::InterfaceVersion::V2, "AMF-Host-OAR", REL::Version(1, 0, 0, 0)); }

		if (!reg("oar", AMF_HOST_BRIDGE_API, &OnFrame, nullptr))
		{
			spdlog::warn("Apocrypha Menu Framework refused the registration - see its log");
			return;
		}
		spdlog::info("registered with Apocrypha Menu Framework (host bridge api {})", AMF_HOST_BRIDGE_API);
		if (!g_oar) { Fail("Open Animation Replacer's RequestPluginAPI_UI gave no UI interface"); }
		else if (!allowed) { Fail(std::format("Open Animation Replacer {} is not a build this bridge was checked against", version)); }
		else { spdlog::info("ready: OAR's main window \"{}\" is hosted in AMF's page when AMF opens it there", g_title); }
	}

	// CommonLibSSE-NG's SKSE::Init opens the Address Library itself; without the file it stops the game with its own
	// error. AMF already reports a missing file, so the bridge just stays out of the way (loaded, inert).
	bool AddressLibraryPresent()
	{
		const auto& mod = REL::Module::get();
		const std::string v = mod.version().string();
		const std::string file = REL::Module::IsAE() ? std::format("Data/SKSE/Plugins/versionlib-{}.bin", v) : std::format("Data/SKSE/Plugins/version-{}.bin", v);
		std::error_code ec;
		return std::filesystem::exists(file, ec);
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	if (!AddressLibraryPresent()) { return true; }
	SKSE::Init(a_skse);
	InitLog();
	spdlog::info("AMF-Host-OAR 1.0.0 (line {}) loaded - Dear ImGui {} (Open Animation Replacer's pin)", AMF_RUNTIME_LINE, IMGUI_VERSION);
	if (auto* messaging = SKSE::GetMessagingInterface())
	{
		messaging->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
			if (a_msg && a_msg->type == SKSE::MessagingInterface::kPostLoad) { Setup(); }
		});
	}
	return true;
}
