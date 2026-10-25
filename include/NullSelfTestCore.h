#pragma once

// ============================================================================================================
// The null self-test's cimgui half (2.3.0; plan 4. plans\amf-null-checks\PLAN.md section 6, rule 64). AMF's Nexus page
// says a null passed by mistake to one of its exports is a no-op, not a crash; this is the run that shows it. Each case
// calls one of the exported cimgui entry points - the very functions a mod built for SKSE Menu Framework resolves by name
// - with a null where a mod could pass one (a label, a value pointer, a method's self, a callback, an out-struct, text),
// and records what came back against what the guard promises. Every case is a no-op by design: a substituted label draws
// an empty widget in the caller's (hidden) window, everything else returns before Dear ImGui is touched.
//
// Call it inside an ImGui frame with a window current (devbench amf.menu op=nulltest runs it on the render thread in a
// hidden window - NullSelfTest.cpp; tools\null_selftest_check.bat runs this same code off-game against AMF's generated
// cimgui.cpp). The SKSE Menu Framework and AMF C-API cases need the running framework, so they live in NullSelfTest.cpp.
// ============================================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include <string>
#include <vector>

// The exported names, exactly as source/cimgui/cimgui.cpp defines them (extern "C": the linkage is the name)
extern "C"
{
	bool igButton(const char* label, const ImVec2 size);
	bool igCheckbox(const char* label, bool* v);
	bool igSliderFloat(const char* label, float* v, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
	bool igSliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format,
		ImGuiSliderFlags flags);
	bool igDragFloat(const char* label, float* v, float v_speed, float v_min, float v_max, const char* format, ImGuiSliderFlags flags);
	bool igCombo_FnStrPtr(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count,
		int popup_max_height_in_items);
	bool igInputText(const char* label, char* buf, size_t buf_size, ImGuiInputTextFlags flags, ImGuiInputTextCallback callback, void* user_data);
	void igTextUnformatted(const char* text, const char* text_end);
	void igPushID_Str(const char* str_id);
	void igPopID();
	void igGetCursorScreenPos(ImVec2* pOut);
	void ImDrawList_AddLine(ImDrawList* self, const ImVec2 p1, const ImVec2 p2, ImU32 col, float thickness);
	ImFont* ImFontAtlas_AddFontDefault(ImFontAtlas* self, const ImFontConfig* font_cfg);
	void ImGuiIO_AddKeyEvent(ImGuiIO* self, ImGuiKey key, bool down);
}

namespace nullselftest
{
	struct Case
	{
		std::string call;       // the call as a mod would write it
		std::string kind;       // what was null: label, p_data, self, callback, pOut, text, name
		std::string expected;   // what the guard promises
		std::string got;        // what came back
		bool        ok = false;
	};

	inline std::string Bool(bool a_b) { return a_b ? "true" : "false"; }

	// The window the cases draw in: on screen (so its widgets are really submitted, not clipped away before they read
	// anything) but fully transparent, taking no mouse, no navigation and no focus, never saved. Pair with EndHiddenWindow.
	inline void BeginHiddenWindow(const char* a_name)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.001f);   // not 0: ImGui hides a window begun at alpha 0; 0.001 rounds every colour to 0
		ImGui::Begin(a_name ? a_name : "##amf-null-selftest", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
				ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
	}

	inline void EndHiddenWindow()
	{
		ImGui::End();
		ImGui::PopStyleVar();
	}

	// The cimgui cases. Inside a frame, with a window current.
	inline std::vector<Case> RunCimguiCases()
	{
		std::vector<Case> out;
		auto add = [&](const char* a_call, const char* a_kind, const std::string& a_expected, const std::string& a_got) {
			out.push_back({ a_call, a_kind, a_expected, a_got, a_expected == a_got });
		};
		ImGuiContext* const g = GImGui;
		ImGuiWindow* const window = g ? g->CurrentWindow : nullptr;
		if (!window)
		{
			add("(setup)", "-", "a frame with a window", "no ImGui frame or window - nothing called");
			return out;
		}

		add("igButton(NULL, {0,0})", "label", "false", Bool(igButton(nullptr, ImVec2(0.0f, 0.0f))));
		add("igCheckbox(\"##nt\", NULL)", "p_data", "false", Bool(igCheckbox("##nt-check", nullptr)));
		add("igSliderFloat(\"##nt\", NULL, 0, 1, \"%.2f\", 0)", "p_data", "false",
			Bool(igSliderFloat("##nt-slider", nullptr, 0.0f, 1.0f, "%.2f", 0)));
		{
			const float lo = 0.0f, hi = 1.0f;
			add("igSliderScalar(\"##nt\", Float, NULL, &lo, &hi, NULL, 0)", "p_data", "false",
				Bool(igSliderScalar("##nt-scalar", ImGuiDataType_Float, nullptr, &lo, &hi, nullptr, 0)));
			float v = 0.5f;
			add("igSliderScalar(\"##nt\", Float, &v, NULL, NULL, NULL, 0)", "p_data", "false and v unchanged",
				Bool(igSliderScalar("##nt-scalar2", ImGuiDataType_Float, &v, nullptr, nullptr, nullptr, 0)) + (v == 0.5f ? " and v unchanged" : " and v CHANGED"));
		}
		add("igDragFloat(NULL, NULL, 1, 0, 1, NULL, 0)", "label + p_data", "false",
			Bool(igDragFloat(nullptr, nullptr, 1.0f, 0.0f, 1.0f, nullptr, 0)));
		{
			int current = 1;
			add("igCombo_FnStrPtr(\"##nt\", &i, NULL getter, NULL, 3, -1)", "callback", "false and i unchanged",
				Bool(igCombo_FnStrPtr("##nt-combo", &current, nullptr, nullptr, 3, -1)) + (current == 1 ? " and i unchanged" : " and i CHANGED"));
		}
		add("igInputText(\"##nt\", NULL, 16, 0, NULL, NULL)", "p_data", "false",
			Bool(igInputText("##nt-text", nullptr, 16, 0, nullptr, nullptr)));
		{
			const ImVec2 before = window->DC.CursorPos;
			igTextUnformatted(nullptr, nullptr);
			add("igTextUnformatted(NULL, NULL)", "text", "nothing drawn", window->DC.CursorPos.y == before.y ? "nothing drawn" : "a line was drawn");
		}
		{
			const int depth = window->IDStack.Size;
			igPushID_Str(nullptr);
			const bool pushed = window->IDStack.Size == depth + 1;
			igPopID();
			add("igPushID_Str(NULL) + igPopID()", "label", "pushed \"\", stack balanced",
				std::string(pushed ? "pushed \"\"" : "nothing pushed") + (window->IDStack.Size == depth ? ", stack balanced" : ", stack UNBALANCED"));
		}
		igGetCursorScreenPos(nullptr);
		add("igGetCursorScreenPos(NULL)", "pOut", "returned", "returned");
		{
			const int cmds = window->DrawList ? window->DrawList->VtxBuffer.Size : -1;
			ImDrawList_AddLine(nullptr, ImVec2(0.0f, 0.0f), ImVec2(10.0f, 10.0f), 0xFFFFFFFF, 1.0f);
			const int after = window->DrawList ? window->DrawList->VtxBuffer.Size : -1;
			add("ImDrawList_AddLine(NULL, ...)", "self", "returned, nothing drawn", cmds == after ? "returned, nothing drawn" : "drew");
		}
		add("ImFontAtlas_AddFontDefault(NULL, NULL)", "self", "NULL", ImFontAtlas_AddFontDefault(nullptr, nullptr) ? "a font" : "NULL");
		ImGuiIO_AddKeyEvent(nullptr, ImGuiKey_A, true);
		add("ImGuiIO_AddKeyEvent(NULL, A, true)", "self", "returned", "returned");
		return out;
	}

	// JSON for one case list (the strings are ASCII and carry no quotes but the escaped ones below)
	inline std::string Json(const std::string& a_s)
	{
		std::string o = "\"";
		for (const char c : a_s)
		{
			if (c == '"' || c == '\\') { o += '\\'; o += c; }
			else if (static_cast<unsigned char>(c) < 0x20) { o += ' '; }
			else { o += c; }
		}
		return o + "\"";
	}

	inline std::string CasesJson(const std::vector<Case>& a_cases)
	{
		std::string o = "[";
		for (std::size_t i = 0; i < a_cases.size(); ++i)
		{
			const Case& c = a_cases[i];
			o += (i ? "," : "") + std::string("{\"call\":") + Json(c.call) + ",\"null\":" + Json(c.kind) + ",\"expected\":" + Json(c.expected) +
				 ",\"got\":" + Json(c.got) + ",\"ok\":" + Bool(c.ok) + "}";
		}
		return o + "]";
	}
}
