#include "NullSelfTest.h"

#include "NullSelfTestCore.h"

#include "AMF/API.h"
#include "ConsumerSurface.h"
#include "Registry.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// SKSE Menu Framework's API as Compat.cpp exports it (extern "C": the linkage is the name; the types are Compat.cpp's own)
extern "C"
{
	void         AddSectionItem(const char* a_path, void (*a_render)());
	bool         RenameSection(const char* a_path, const char* a_newName);
	bool         DeleteSection(const char* a_path);
	std::int64_t RegisterInpoutEvent(bool (*a_callback)(RE::InputEvent*));
	std::int64_t RegisterEventPriority(void (*a_callback)(int), float a_priority);
	void*        AddWindow(void (*a_render)());
	void*        LoadTexture(const char* a_path, ImVec2* a_outSize);
}

namespace nullselftest
{
	namespace
	{
		struct Pending
		{
			std::mutex              m;
			std::condition_variable cv;
			bool                    done = false;
			std::string             json;
		};

		std::mutex               g_lock;
		std::shared_ptr<Pending> g_pending;   // the run the render thread owes, if any

		void NoopPage() {}   // a valid page callback, for the cases where only a NAME is null

		// The framework's own exports: SKSE Menu Framework's API (Compat.cpp) and AMF's C API (main.cpp). Any thread; the
		// registry and the consumer window list are locked inside them.
		std::vector<Case> RunApiCases()
		{
			std::vector<Case> out;
			auto add = [&](const char* a_call, const char* a_kind, const std::string& a_expected, const std::string& a_got) {
				out.push_back({ a_call, a_kind, a_expected, a_got, a_expected == a_got });
			};
			auto ptr = [](const void* a_p) { return std::string(a_p ? "a pointer" : "NULL"); };
			const std::size_t pagesBefore = registry::Snapshot().size();
			const std::size_t windowsBefore = consumer::WindowCount();

			AddSectionItem(nullptr, nullptr);
			add("AddSectionItem(NULL, NULL)", "name + callback", "refused", registry::Snapshot().size() == pagesBefore ? "refused" : "REGISTERED");
			AddSectionItem("AMF null self-test/Page", nullptr);
			add("AddSectionItem(\"AMF null self-test/Page\", NULL)", "callback", "refused",
				registry::Snapshot().size() == pagesBefore ? "refused" : "REGISTERED");
			add("RenameSection(NULL, \"x\")", "name", "false", Bool(RenameSection(nullptr, "x")));
			add("DeleteSection(NULL)", "name", "false", Bool(DeleteSection(nullptr)));
			add("RegisterInpoutEvent(NULL)", "callback", "0", std::to_string(RegisterInpoutEvent(nullptr)));
			add("RegisterEventPriority(NULL, 0)", "callback", "0", std::to_string(RegisterEventPriority(nullptr, 0.0f)));
			add("AddWindow(NULL)", "callback", "NULL", ptr(AddWindow(nullptr)));
			add("LoadTexture(NULL, NULL)", "name + pOut", "NULL", ptr(LoadTexture(nullptr, nullptr)));
			add("AMF_RegisterPage(NULL, NULL, NULL)", "names + callback", "false", Bool(AMF_RegisterPage(nullptr, nullptr, nullptr)));
			add("AMF_RegisterPage(NULL, \"Page\", cb)", "name", "false", Bool(AMF_RegisterPage(nullptr, "Page", &NoopPage)));
			add("AMF_RegisterPage(\"AMF null self-test\", NULL, cb)", "name", "false", Bool(AMF_RegisterPage("AMF null self-test", nullptr, &NoopPage)));
			add("AMF_RegisterPage(\"AMF null self-test\", \"Page\", NULL)", "callback", "false",
				Bool(AMF_RegisterPage("AMF null self-test", "Page", nullptr)));
			add("AMF_SetPageVisible(NULL, NULL, true)", "names", "false", Bool(AMF_SetPageVisible(nullptr, nullptr, true)));
			add("AMF_DrawThemeFrame(NULL, 0, 0, 10, 10)", "self (draw list)", "false", Bool(AMF_DrawThemeFrame(nullptr, 0.0f, 0.0f, 10.0f, 10.0f)));
			AMF_GetSideListRect(nullptr, nullptr, nullptr, nullptr);
			add("AMF_GetSideListRect(NULL, NULL, NULL, NULL)", "pOut", "returned", "returned");
			add("AMF_GetStick(0, NULL, NULL, NULL, NULL)", "pOut", "true", Bool(AMF_GetStick(0, nullptr, nullptr, nullptr, nullptr)));
			add("SMF_GetReservedKeyCodes(NULL, 0)", "pOut (the size probe)", "a count", SMF_GetReservedKeyCodes(nullptr, 0) > 0 ? "a count" : "0");

			add("(after) pages and windows", "-", "none added",
				registry::Snapshot().size() == pagesBefore && consumer::WindowCount() == windowsBefore ? "none added" : "SOMETHING WAS ADDED");
			return out;
		}

		void LogCases(const char* a_group, const std::vector<Case>& a_cases)
		{
			for (const Case& c : a_cases)
			{
				if (c.ok)
				{
					logger::info("null self-test ({}): {} - null {} -> {} (a no-op, as promised)", a_group, c.call, c.kind, c.got);
				}
				else
				{
					logger::error("null self-test ({}): {} - null {} -> {}, expected {}", a_group, c.call, c.kind, c.got, c.expected);
				}
			}
		}
	}

	void Frame()
	{
		std::shared_ptr<Pending> run;
		{
			std::scoped_lock l(g_lock);
			run = g_pending;
		}
		if (!run) { return; }
		if (!GImGui) { return; }   // no ImGui context yet: the run waits for a frame that has one

		BeginHiddenWindow("##amf-null-selftest");
		const ImGuiWindow* const window = GImGui->CurrentWindow;
		if (!window || window->SkipItems)
		{
			EndHiddenWindow();   // its items would be skipped this frame: run on the next one
			return;
		}
		const std::vector<Case> cimgui = RunCimguiCases();
		EndHiddenWindow();
		const std::vector<Case> api = RunApiCases();

		LogCases("cimgui", cimgui);
		LogCases("SMF / AMF API", api);
		std::size_t passed = 0, total = 0;
		for (const Case& c : cimgui) { ++total; if (c.ok) { ++passed; } }
		for (const Case& c : api) { ++total; if (c.ok) { ++passed; } }
		logger::info("null self-test: {} of {} calls were the no-op their guard promises", passed, total);
		const std::string json = std::string("{\"ok\":") + Bool(passed == total) + ",\"op\":\"nulltest\",\"passed\":" + std::to_string(passed) +
								 ",\"total\":" + std::to_string(total) + ",\"cimgui\":" + CasesJson(cimgui) + ",\"api\":" + CasesJson(api) + "}";
		{
			std::scoped_lock l(g_lock);
			if (g_pending == run) { g_pending.reset(); }
		}
		{
			std::scoped_lock l(run->m);
			run->json = json;
			run->done = true;
		}
		run->cv.notify_all();
	}

	std::string RunJson(unsigned a_timeoutMs)
	{
		std::shared_ptr<Pending> run;
		{
			std::scoped_lock l(g_lock);
			if (!g_pending) { g_pending = std::make_shared<Pending>(); }
			run = g_pending;   // a run already asked for is shared, never queued twice
		}
		if (!run) { return "{\"ok\":false,\"op\":\"nulltest\",\"error\":\"could not queue the run\"}"; }
		logger::info("null self-test: asked for (DevBench amf.menu op=nulltest); runs on the next frame");
		std::unique_lock l(run->m);
		if (!run->cv.wait_for(l, std::chrono::milliseconds(a_timeoutMs), [&] { return run->done; }))
		{
			return "{\"ok\":false,\"op\":\"nulltest\",\"error\":\"no frame ran the self-test within the timeout (is the game drawing?) - it runs at the next frame and logs its result\"}";
		}
		return run->json;
	}
}
