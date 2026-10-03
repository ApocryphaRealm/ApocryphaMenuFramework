// SPDX-License-Identifier: MIT
// AMF Example - the smallest complete mod with pages in the Apocrypha Menu Framework (Skyrim, SKSE, CommonLibSSE-NG).
// Copy it as the start of your own. Everything it knows about the framework comes from AMF.h.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <imgui.h>   // Dear ImGui 1.90.8 docking - the framework's version (see AMF.h, DRAWING)

#include "AMF.h"
#include "PreciseSlider.h"   // one unit per D-pad nudge (sdk/include)

namespace
{
	float g_speed = 1.0f;
	bool  g_enabled = true;
	bool  g_advanced = false;
	int   g_mode = 0;
	char  g_name[64] = "Dovahkiin";

	void DrawSettings()
	{
		const bool ours = AMF::UseFrameworkImGui();
		static bool s_logged = false;
		if (!s_logged) {   // once: whether this build's Dear ImGui could share the framework's
			s_logged = true;
			SKSE::log::info("AMF Example: first draw - {} (context {})", ours ? "drawing with the framework's Dear ImGui" : "Dear ImGui not shared, drawing nothing", AMF::ImGuiContext());
		}
		if (!ours) {
			return;   // not the framework's ImGui (or not up yet): draw nothing
		}
		ImGui::TextWrapped("A page from another mod, drawn with the ordinary C++ Dear ImGui API through AMF.h.");
		ImGui::Separator();
		ImGui::Checkbox("Enabled", &g_enabled);
		precise::SliderFloat("Speed", &g_speed, 0.5f, 3.0f, "%.2f");   // a D-pad nudge moves 0.01, not 1% of the range
		const char* modes[] = { "Gentle", "Normal", "Brutal" };
		ImGui::Combo("Mode", &g_mode, modes, IM_ARRAYSIZE(modes));
		ImGui::InputText("Name", g_name, sizeof(g_name));
		if (ImGui::Checkbox("Show the Advanced page", &g_advanced)) {
			AMF::SetPageVisible("AMF Example", "Advanced", g_advanced);
		}
		ImGui::Spacing();
		ImGui::TextDisabled("Framework %s, API %u, language %s, input: %s", AMF::Version(), AMF::APIVersion(), AMF::Language(),
			AMF::GetInputMode() == AMF::InputMode::kController ? "controller" : "keyboard/mouse");
	}

	void DrawAdvanced()
	{
		if (!AMF::UseFrameworkImGui()) {
			return;
		}
		ImGui::TextWrapped("Hidden until the switch on the Settings page turns it on (AMF::SetPageVisible).");
		float lx = 0.0f, ly = 0.0f;
		bool  live = false;
		AMF::GetStick(0, &lx, &ly, nullptr, &live);
		ImGui::Text("Left stick: %.2f, %.2f%s", lx, ly, live ? "  (moving)" : "");
	}

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		// kPostLoad: every SKSE plugin is loaded, whatever order their file names put them in.
		if (!a_msg || a_msg->type != SKSE::MessagingInterface::kPostLoad) {
			return;
		}
		if (!AMF::IsInstalled()) {
			SKSE::log::info("AMF Example: the Apocrypha Menu Framework is not installed - no menu page");
			return;
		}
		AMF::RegisterPage("AMF Example", "Settings", &DrawSettings);
		AMF::RegisterPage("AMF Example", "Advanced", &DrawAdvanced);
		AMF::SetPageVisible("AMF Example", "Advanced", g_advanced);
		SKSE::log::info("AMF Example: pages registered with the Apocrypha Menu Framework {} (API {})", AMF::Version(), AMF::APIVersion());
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	if (auto dir = SKSE::log::log_directory()) {   // Documents\My Games\Skyrim Special Edition\SKSE\AMFExample.log
		auto log = std::make_shared<spdlog::logger>("AMFExample",
			std::make_shared<spdlog::sinks::basic_file_sink_mt>((*dir / "AMFExample.log").string(), true));
		log->flush_on(spdlog::level::info);
		spdlog::set_default_logger(std::move(log));
	}
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
