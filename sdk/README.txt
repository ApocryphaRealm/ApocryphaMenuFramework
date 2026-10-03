Apocrypha Menu Framework (Skyrim) - SDK
=======================================
Version 2.0.4 (matches the framework release it ships beside)

For mod authors who want a settings page in the Apocrypha Menu Framework. MIT licensed, so vendor it freely.

  include\AMF.h        the whole public API: one header, nothing to link, safe when the framework is not installed.
                       Read its top comment - a page in five lines, how to draw (C++ Dear ImGui 1.90.8 docking through
                       the framework's context, or the cimgui ig* exports), and the version rules. The same header
                       works for the Oblivion Remastered framework.
  include\PreciseSlider.h  precise::SliderFloat / precise::SliderInt - a slider a D-pad nudge moves by exactly one unit
                       of the digit it shows (ImGui's own steps 1% of the range). Header-only.
  example\main.cpp     a complete SKSE plugin (CommonLibSSE-NG) with two pages built against AMF.h - copy it as the
                       start of your own.
  example\CMakeLists.txt, CMakePresets.json, vcpkg.json, cmake\   its build: run build.bat with VCPKG_ROOT set.
                       cmake\ports\imgui pins Dear ImGui to 1.90.8 docking, the framework's own version. Keep it: the
                       stock vcpkg port gives a different version, and AMF::UseFrameworkImGui() then refuses to share
                       the framework's ImGui (it checks, so a mismatch draws nothing instead of corrupting memory).
  example\AMFExample.dll  that example, built for Skyrim SE / AE (1.5.97 - 1.6.x): drop it into Data\SKSE\Plugins
                       beside the framework to see "AMF Example" appear in the menu.

Skyrim notes
  - Sharing the framework's Dear ImGui from C++ (AMF::UseFrameworkImGui) works with the Skyrim framework 1.7.2 and
    later. A call added after the first release names the version that added it in its comment; on an older
    framework it returns its documented fallback rather than failing.
  - A mod written for SKSE Menu Framework keeps working unchanged: the framework answers that framework's header too.
    AMF.h is for a mod that wants the framework's own calls - hiding pages, the inner-tab bumpers, the thumbsticks,
    the language, the on-screen keyboard and the theme frame.

Source and the framework itself: https://github.com/ApocryphaRealm/ApocryphaMenuFramework
