#include "PCH.h"

#include "LoadGuard.h"

#include "LoadGuardCore.h"
#include "utils/Logger.h"

#include <atomic>
#include <mutex>
#include <string>

namespace loadguard
{
	namespace
	{
		std::mutex g_lock;
		core::State g_state;   // guarded by g_lock: events arrive on the main thread, the render and input paths read it
		std::atomic<bool> g_installed{ false };

		void LogEdge(bool a_wasBlocked, const char* a_why)
		{
			const bool now = g_state.Blocked();
			if (now == a_wasBlocked) { return; }
			logger::info("loading guard: {} ({})", now ? "on - AMF ignores input and stays closed" : "off - AMF answers input again", a_why);
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) { return RE::BSEventNotifyControl::kContinue; }
				if (a_event->menuName == RE::LoadingMenu::MENU_NAME)
				{
					std::scoped_lock l(g_lock);
					const bool was = g_state.Blocked();
					g_state.OnLoadingMenu(a_event->opening);
					LogEdge(was, a_event->opening ? "the Loading Menu opened" : "the Loading Menu closed");
				}
				else if (a_event->opening && a_event->menuName == RE::MainMenu::MENU_NAME)
				{
					std::scoped_lock l(g_lock);
					const bool was = g_state.Blocked();
					g_state.OnMainMenuOpened();
					LogEdge(was, "the Main Menu opened");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		if (g_installed.load(std::memory_order_acquire)) { return; }
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			// not permanent: kPostLoadGame / kNewGame still lift the start-up hold; loads are then not seen
			logger::warn("loading guard: no UI singleton at kDataLoaded - the Main Menu and Loading Menu cannot be watched; "
						 "AMF stays closed until a game is loaded or started");
			return;
		}
		static MenuSink sink;
		ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
		g_installed.store(true, std::memory_order_release);
		logger::info("loading guard: watching the Main Menu and the Loading Menu - AMF ignores input until the Main Menu opens, "
					 "and during every load");
	}

	void OnGameReady()
	{
		std::scoped_lock l(g_lock);
		const bool was = g_state.Blocked();
		g_state.OnGameReady();
		LogEdge(was, "SKSE reported a game loaded or started");
	}

	bool Blocked()
	{
		std::scoped_lock l(g_lock);
		return g_state.Blocked();
	}

	void NoteIgnored(const char* a_what)
	{
		bool first = false;
		bool started = false;
		{
			std::scoped_lock l(g_lock);
			first = g_state.TakeFirstIgnored();
			started = g_state.started;
		}
		if (first)
		{
			logger::debug("loading guard: {} ignored - the game is {} (logged once per load)", a_what ? a_what : "input",
						  started ? "loading" : "not at its main menu yet");
		}
	}
}
