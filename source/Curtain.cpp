#include "PCH.h"

#include "Curtain.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include <imgui.h>

namespace curtain
{
	namespace
	{
		using clock = std::chrono::steady_clock;

		// How long the fade out takes once the main menu is up. Short: this is a reveal, not an effect.
		constexpr float kFadeSeconds = 0.40f;

		// The escape hatch, in seconds, read from the INI so a heavy load order can be given more
		// room without a rebuild. 30s was the first value and it was too short: on the owner's list
		// (2026-09-15) the main menu had still not appeared, so the curtain timed out and uncovered
		// the tail of startup - exactly what it exists to hide.
		float HardTimeoutSeconds()
		{
			return static_cast<float>(settings::Get().curtainTimeoutSeconds);
		}

		// The player being in a loaded world is proof we are past startup, whatever the menu did.
		// parentCell stays null until a save or a new game actually loads, so this cannot fire
		// early. It is what stops a longer timeout from ever stranding anyone: if the curtain is
		// somehow still up when play begins, it goes at once.
		bool PlayerIsInWorld()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->parentCell;
		}

		std::atomic<bool> g_lifted{ false };
		bool              g_started = false;
		bool              g_sawMainMenu = false;
		clock::time_point g_firstFrame{};
		clock::time_point g_fadeStart{};

		bool MainMenuIsUp()
		{
			// Null the moment the process starts - this runs long before the UI singleton exists.
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		}
	}

	bool IsCovering()
	{
		return !g_lifted.load(std::memory_order_acquire);
	}

	void Lift(const char* a_reason)
	{
		if (g_lifted.exchange(true, std::memory_order_acq_rel))
		{
			return;  // already down; never log twice
		}
		logger::info("startup curtain: lifted ({})", a_reason ? a_reason : "no reason given");
	}

	void Draw()
	{
		if (g_lifted.load(std::memory_order_acquire))
		{
			return;
		}

		// Turned off in the settings page or the INI: never cover anything, and do not keep
		// checking for the rest of the session.
		if (!settings::Get().startupCurtain)
		{
			Lift("the setting is off");
			return;
		}

		const clock::time_point now = clock::now();
		if (!g_started)
		{
			g_started = true;
			g_firstFrame = now;
			logger::info("startup curtain: covering the screen until the main menu is ready "
						 "(lifts by itself after {:.0f}s, or the moment play begins)", HardTimeoutSeconds());
		}

		if (!g_sawMainMenu && PlayerIsInWorld())
		{
			Lift("the player is already in the world");
			return;
		}

		if (!g_sawMainMenu && MainMenuIsUp())
		{
			g_sawMainMenu = true;
			g_fadeStart = now;
			logger::info("startup curtain: main menu is up; fading out over {:.2f}s", kFadeSeconds);
		}

		float alpha = 1.0f;
		if (g_sawMainMenu)
		{
			const float elapsed = std::chrono::duration<float>(now - g_fadeStart).count();
			if (elapsed >= kFadeSeconds)
			{
				Lift("the main menu is up and the fade finished");
				return;
			}
			alpha = 1.0f - (elapsed / kFadeSeconds);
		}
		else if (std::chrono::duration<float>(now - g_firstFrame).count() >= HardTimeoutSeconds())
		{
			// Something is wrong - a main menu replacer we cannot see, or a start that never gets
			// there. Either way the player gets their screen back rather than a black rectangle.
			Lift("the main menu was never seen within the timeout");
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();
		if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
		{
			return;  // no viewport yet; nothing sensible to cover
		}

		// The FOREGROUND draw list, so the curtain is over the framework's own window and over
		// every consumer HUD element, not interleaved with them.
		const auto a = static_cast<ImU32>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
		ImGui::GetForegroundDrawList()->AddRectFilled(
			ImVec2(0.0f, 0.0f),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y),
			IM_COL32(0, 0, 0, a));
	}
}
