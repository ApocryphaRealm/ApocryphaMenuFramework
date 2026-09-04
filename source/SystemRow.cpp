#include "PCH.h"

#include "SystemRow.h"

#include "Renderer.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <atomic>
#include <string>

namespace systemrow
{
	namespace
	{
		constexpr const char* kJournal = "Journal Menu";
		constexpr const char* kRowLabel = "SKSE MENUS";

		std::atomic_bool g_installed{ false };
		std::atomic_bool g_injected{ false };
		std::string      g_foundPath;     // the System page, once located
		int              g_rowIndex = -1; // our row's index in entryList

		// The listener object handed to addEventListener. Held for the life of the process: the
		// movie keeps a reference to it, and letting our side drop the value invites the pair to
		// disagree about lifetime.
		RE::GFxValue     g_listenerScope;
		bool             g_listenerAdded = false;
		constexpr const char* kListenerMethod = "AMF_onCategoryButtonPress";

		// Where the System page lives. The first is vanilla's, verified by reading the menu's own
		// instance names; the rest are cheap guesses for replacers that wrap the hierarchy. Probed
		// in order, and whichever resolves is logged - guessing silently is how a scan ends up
		// excluding the place the answer lives.
		const char* const kPageCandidates[] = {
			"_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
			"_root.Menu_mc.SystemFader.Page_mc",
			"_root.QuestJournalFader.Menu_mc.SystemPage_mc",
			"_level0.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
		};

		RE::GPtr<RE::IMenu> JournalMenu()
		{
			auto* ui = RE::UI::GetSingleton();
			return ui ? ui->GetMenu(kJournal) : nullptr;
		}

		// Finds the System page in whatever movie is actually loaded.
		bool LocatePage(RE::GFxMovieView* a_movie, std::string& a_out)
		{
			for (const char* candidate : kPageCandidates)
			{
				RE::GFxValue page;
				if (a_movie->GetVariable(&page, candidate) && !page.IsUndefined())
				{
					const std::string listPath = std::string(candidate) + ".CategoryList_mc.List_mc";
					RE::GFxValue list;
					if (a_movie->GetVariable(&list, listPath.c_str()) && !list.IsUndefined())
					{
						a_out = candidate;
						return true;
					}
				}
			}
			return false;
		}

		// ------------------------------------------------------------------------------------
		// The wrapper. Every press goes through here; only our own row is kept, and everything
		// else is handed straight back to the menu's own handler so save, load, settings, controls
		// and quit behave exactly as they always did.
		// ------------------------------------------------------------------------------------
		class ItemPressHandler : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				int index = -1;
				if (a_params.argCount > 0 && a_params.args[0].IsObject())
				{
					RE::GFxValue idx;
					if (a_params.args[0].GetMember("index", &idx) && idx.IsNumber())
					{
						index = static_cast<int>(idx.GetNumber());
					}
				}

				if (index >= 0 && index == g_rowIndex)
				{
					logger::info("System row: selected (index {}); opening the mod menus", index);
					renderer::SetSelectedNode("system/mods");
					renderer::SetMenuVisible(true, /*a_nested=*/true);
					return;
				}

				// Anything else is not ours and needs nothing from us: the menu's own listener is
				// still attached and handles it exactly as it always did.
			}
		};

		void InjectRow()
		{
			if (!settings::Get().systemMenuRow)
			{
				return;   // switched off: the game's menu is left exactly as it was
			}

			RE::GPtr<RE::IMenu> menu = JournalMenu();
			if (!menu || !menu->uiMovie)
			{
				logger::warn("System row: the journal has no movie yet; nothing added this open");
				return;
			}
			RE::GFxMovieView* movie = menu->uiMovie.get();

			std::string page;
			if (!LocatePage(movie, page))
			{
				logger::warn("System row: could not find the System page in this journal - no row "
							 "added. The artwork installed here may restructure the menu; the row "
							 "is the only thing lost, everything else is untouched.");
				return;
			}
			g_foundPath = page;
			logger::info("System row: System page found at \"{}\"", page);

			// ---- the row -----------------------------------------------------------------
			const std::string listPath = page + ".CategoryList_mc.List_mc";
			RE::GFxValue entryList;
			if (!movie->GetVariable(&entryList, (listPath + ".entryList").c_str()) || !entryList.IsArray())
			{
				logger::warn("System row: \"{}.entryList\" is not an array; no row added", listPath);
				return;
			}

			// Never add twice into one movie: a re-open rebuilds the list, but a second call in the
			// same open would leave two identical rows.
			const std::uint32_t before = entryList.GetArraySize();
			for (std::uint32_t i = 0; i < before; ++i)
			{
				RE::GFxValue existing;
				RE::GFxValue text;
				if (entryList.GetElement(i, &existing) && existing.IsObject() &&
					existing.GetMember("text", &text) && text.IsString() &&
					std::string(text.GetString()) == kRowLabel)
				{
					g_rowIndex = static_cast<int>(i);
					logger::debug("System row: already present at index {}", g_rowIndex);
					return;
				}
			}

			RE::GFxValue entry;
			movie->CreateObject(&entry);
			RE::GFxValue label;
			label.SetString(kRowLabel);
			entry.SetMember("text", label);
			entryList.PushBack(entry);
			g_rowIndex = static_cast<int>(before);

			RE::GFxValue result;
			movie->Invoke((listPath + ".InvalidateData").c_str(), &result, nullptr, 0);

			// ---- the press listener --------------------------------------------------------
			// NOT a wrapper. The menu binds its own handler with
			//     this.CategoryList.addEventListener("itemPress", this, "onCategoryButtonPress")
			// so the clean move is to add a SECOND listener beside it rather than replace the
			// first. Two earlier attempts tried to read the original out and delegate to it, and
			// both failed for the same reason: GetVariable does not return ActionScript functions,
			// neither off the page instance nor off the class prototype. Adding a listener needs
			// no such read.
			//
			// It is also the safer shape by some distance. The game's own handler is never touched,
			// so save, load, settings, controls and quit cannot be broken by anything here - in the
			// worst case our listener is simply never called. In the pause menu that matters more
			// than elegance.
			//
			// The one cosmetic cost: the vanilla handler also runs for our row, does not recognise
			// the index, and plays its cancel sound. The row still works.
			if (!g_listenerAdded)
			{
				movie->CreateObject(&g_listenerScope);
				RE::GFxValue fn;
				movie->CreateFunction(&fn, new ItemPressHandler());
				g_listenerScope.SetMember(kListenerMethod, fn);

				RE::GFxValue args[3];
				args[0].SetString("itemPress");
				args[1] = g_listenerScope;
				args[2].SetString(kListenerMethod);

				RE::GFxValue added;
				if (!movie->Invoke((listPath + ".addEventListener").c_str(), &added, args, 3))
				{
					logger::warn("System row: addEventListener failed on \"{}\"; the row will draw "
								 "but do nothing. The menu's own handler is untouched.", listPath);
					return;
				}
				g_listenerAdded = true;
				logger::info("System row: press listener added beside the menu's own");
			}

			g_injected.store(true, std::memory_order_release);
			logger::info("System row: \"{}\" added at index {} and the press handler wrapped",
						 kRowLabel, g_rowIndex);
		}

		class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (a_event && !a_event->opening && a_event->menuName == kJournal)
				{
					// The movie is torn down with the menu; the listener on it goes with it.
					g_listenerAdded = false;
					g_listenerScope = RE::GFxValue{};
				}
				if (a_event && a_event->opening && a_event->menuName == kJournal)
				{
					// The movie is rebuilt per open, so the row and the wrap go on every time.
					InjectRow();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	bool Install()
	{
		if (g_installed.load(std::memory_order_acquire))
		{
			return true;
		}
		auto* ui = RE::UI::GetSingleton();
		if (!ui)
		{
			logger::warn("System row: no UI singleton yet; will retry at the next message");
			return false;   // not permanent - a first failed lookup never is
		}
		static MenuSink sink;
		ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);
		g_installed.store(true, std::memory_order_release);
		logger::info("System row: menu sink installed; the row is added when the journal opens");
		return true;
	}

	bool WasInjected()
	{
		return g_injected.load(std::memory_order_acquire);
	}

	const char* FoundPath()
	{
		return g_foundPath.c_str();
	}

	bool GetPanelRect(float& a_x, float& a_y, float& a_w, float& a_h)
	{
		RE::GPtr<RE::IMenu> menu = JournalMenu();
		if (!menu || !menu->uiMovie)
		{
			return false;   // journal not open - nothing to measure, and nothing to report
		}
		RE::GFxMovieView* movie = menu->uiMovie.get();

		// getBounds() reports in the coordinate space of whatever clip it is handed, so it is
		// handed _root: that is the one space the visible frame rect below is also expressed in,
		// and mixing the two spaces is the easy way to get a rectangle that is right in shape and
		// wrong in place.
		RE::GFxValue root;
		if (!movie->GetVariable(&root, "_root") || !root.IsObject())
		{
			return false;
		}

		// The whole journal PANEL first, then narrower fallbacks. Fitting the outer panel is what
		// makes the surface read as a page of the journal; the System page alone is the next best
		// thing when a replacer has rebuilt the hierarchy above it.
		const char* const kPanelCandidates[] = {
			"_root.QuestJournalFader.Menu_mc",
			"_root.Menu_mc",
			"_level0.QuestJournalFader.Menu_mc",
		};

		const RE::GRectF visible = movie->GetVisibleFrameRect();
		const float visW = visible.right - visible.left;
		const float visH = visible.bottom - visible.top;
		if (visW <= 1.0f || visH <= 1.0f)
		{
			return false;   // a degenerate stage would divide the fractions below into nonsense
		}

		for (const char* path : kPanelCandidates)
		{
			RE::GFxValue bounds;
			RE::GFxValue args[1]{ root };
			const std::string fn = std::string(path) + ".getBounds";
			if (!movie->Invoke(fn.c_str(), &bounds, args, 1) || !bounds.IsObject())
			{
				continue;
			}

			RE::GFxValue xMin, xMax, yMin, yMax;
			if (!bounds.GetMember("xMin", &xMin) || !bounds.GetMember("xMax", &xMax) ||
				!bounds.GetMember("yMin", &yMin) || !bounds.GetMember("yMax", &yMax) ||
				!xMin.IsNumber() || !xMax.IsNumber() || !yMin.IsNumber() || !yMax.IsNumber())
			{
				continue;
			}

			const float left = static_cast<float>(xMin.GetNumber());
			const float top = static_cast<float>(yMin.GetNumber());
			const float width = static_cast<float>(xMax.GetNumber()) - left;
			const float height = static_cast<float>(yMax.GetNumber()) - top;

			// A clip that is faded out or not yet built measures as a point or as the 6710886.4
			// AS2 reports for an empty clip. Neither is a panel, and accepting either would size
			// the window to a sliver or to nothing.
			if (width < 1.0f || height < 1.0f || width > visW * 4.0f || height > visH * 4.0f)
			{
				continue;
			}

			a_x = (left - visible.left) / visW;
			a_y = (top - visible.top) / visH;
			a_w = width / visW;
			a_h = height / visH;

			static std::string reported;
			if (reported != path)
			{
				reported = path;
				logger::info("System row: journal panel measured at {} -> x={:.3f} y={:.3f} w={:.3f} h={:.3f} of the screen",
					path, a_x, a_y, a_w, a_h);
			}
			return true;
		}

		logger::warn("System row: journal is open but no panel clip could be measured; the window keeps its own size");
		return false;
	}
}
