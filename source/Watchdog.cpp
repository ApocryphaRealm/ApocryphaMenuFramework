#include "Watchdog.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace watchdog
{
	namespace
	{
		using clock = std::chrono::steady_clock;

		std::atomic<std::uint64_t> g_frames{ 0 };
		std::atomic<std::int64_t>  g_lastFrameMs{ 0 };   // steady-clock ms at the last Tick()
		std::atomic<bool>          g_started{ false };

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
		}

		std::int64_t SinceLastFrameMs()
		{
			const std::int64_t last = g_lastFrameMs.load(std::memory_order_relaxed);
			return last == 0 ? 0 : (NowMs() - last);
		}

		void Monitor()
		{
			// Deliberately coarse: a poll every 2s is plenty for a window measured in minutes, and
			// keeps this thread invisible in a profile.
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::seconds(2));

				if (!settings::Get().watchdogEnabled)
				{
					continue;
				}

				const std::int64_t windowMs = static_cast<std::int64_t>(settings::Get().watchdogSeconds) * 1000;
				if (windowMs <= 0)
				{
					continue;
				}

				// Only meaningful once the renderer has produced at least one frame - before that
				// the game is still starting up and a "stall" is expected.
				if (g_lastFrameMs.load(std::memory_order_relaxed) == 0)
				{
					continue;
				}

				const std::int64_t stalled = SinceLastFrameMs();
				if (stalled >= windowMs)
				{
					KillNow(std::string("watchdog: no frame for ") + std::to_string(stalled / 1000) +
							"s (window " + std::to_string(windowMs / 1000) + "s) - treating the game as hung");
				}
			}
		}
	}

	void Init()
	{
		bool expected = false;
		if (!g_started.compare_exchange_strong(expected, true))
		{
			return;
		}

		std::thread(Monitor).detach();
		logger::info("watchdog: monitor started (enabled={}, window={}s)",
					 settings::Get().watchdogEnabled, settings::Get().watchdogSeconds);
	}

	void Tick()
	{
		g_frames.fetch_add(1, std::memory_order_relaxed);
		g_lastFrameMs.store(NowMs(), std::memory_order_relaxed);
	}

	void KillNow(const std::string& a_reason)
	{
		// Log and FLUSH before terminating - TerminateProcess runs no cleanup, so an unflushed
		// sink would lose exactly the line explaining why the game vanished.
		logger::critical("watchdog: terminating the process - {}", a_reason);
		logger::flush();

		// The one call that reliably ends a game whose main thread is wedged: issued from a thread
		// that is still running, inside the process itself. External kills cannot help there.
		::TerminateProcess(::GetCurrentProcess(), 0xA3F0DEAD);

		// TerminateProcess does not return for the calling process, but the compiler wants an
		// escape for [[noreturn]].
		for (;;) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
	}

	std::string StatusJson()
	{
		const std::int64_t stalledMs = SinceLastFrameMs();
		const std::int64_t windowMs = static_cast<std::int64_t>(settings::Get().watchdogSeconds) * 1000;
		const bool hung = settings::Get().watchdogEnabled && windowMs > 0 &&
						  g_lastFrameMs.load(std::memory_order_relaxed) != 0 && stalledMs >= windowMs;

		return std::string("{\"frames\":") + std::to_string(g_frames.load(std::memory_order_relaxed)) +
			   ",\"secondsSinceFrame\":" + std::to_string(stalledMs / 1000) +
			   ",\"watchdogEnabled\":" + (settings::Get().watchdogEnabled ? "true" : "false") +
			   ",\"windowSeconds\":" + std::to_string(settings::Get().watchdogSeconds) +
			   ",\"consideredHung\":" + (hung ? "true" : "false") + "}";
	}
}
