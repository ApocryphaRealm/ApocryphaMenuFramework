#include "PCH.h"
#include <algorithm>

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
		std::string      g_listPath;      // its category list, for the DevBench report
		int              g_rowIndex = -1; // where our row landed when it was added - NOT where it is now

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
				// WHICH ROW WAS PRESSED IS READ OFF THE ENTRY, NOT OFF A REMEMBERED INDEX.
				//
				// 1.8.4, from borokoshow's report against Dragonborn UI: the row drew - first between
				// HELP and QUIT, and after Quit on the next open - and did nothing either way. The
				// index is not stable. SystemPage.SetShowMod does
				//     entryList.splice(MOD_MANAGER_BUTTON_INDEX, 0, {text:"$MOD MANAGER"})
				// whenever the game decides to show the Mod Manager row, so every entry after index 2
				// moves down by one AFTER we have pushed ours and recorded where it landed. The list's
				// own itemPress event carries the entry object (BSScrollingList.onItemPress dispatches
				// {type, index, entry, keyboardOrMouse}), so the entry's own text is the identity that
				// cannot drift. The index stays as a fallback for a list that hands us no entry.
				int         index = -1;
				std::string text;
				if (a_params.argCount > 0 && a_params.args[0].IsObject())
				{
					RE::GFxValue idx;
					if (a_params.args[0].GetMember("index", &idx) && idx.IsNumber())
					{
						index = static_cast<int>(idx.GetNumber());
					}
					RE::GFxValue entry;
					RE::GFxValue label;
					if (a_params.args[0].GetMember("entry", &entry) && entry.IsObject() &&
						entry.GetMember("text", &label) && label.IsString())
					{
						text = label.GetString();
					}
				}

				const bool ours = !text.empty() ? text == kRowLabel
												: (index >= 0 && index == g_rowIndex);
				if (ours)
				{
					logger::info("System row: selected (index {}, text \"{}\"); opening the mod menus",
						index, text.empty() ? "<none given>" : text.c_str());
					renderer::SetSelectedNode("system/mods");
					renderer::SetMenuVisible(true, /*a_nested=*/true);
					return;
				}

				// Anything else is not ours and needs nothing from us: the menu's own listener is
				// still attached and handles it exactly as it always did.
			}
		};

		// The category list's rows, in order, as `0:$SAVE, 1:$LOAD, ...`. Logged after every injection
		// and reported by the DevBench tool, because the order differs per art replacer and per open.
		std::string RowTexts(RE::GFxMovieView* a_movie, const std::string& a_listPath)
		{
			RE::GFxValue list;
			std::string  out;
			if (!a_movie || !a_movie->GetVariable(&list, (a_listPath + ".entryList").c_str()) || !list.IsArray())
			{
				return out;
			}
			const std::uint32_t count = list.GetArraySize();
			for (std::uint32_t i = 0; i < count; ++i)
			{
				RE::GFxValue entry;
				RE::GFxValue text;
				std::string  label = "?";
				if (list.GetElement(i, &entry) && entry.IsObject() &&
					entry.GetMember("text", &text) && text.IsString())
				{
					label = text.GetString();
				}
				if (i != 0)
				{
					out += ", ";
				}
				out += std::to_string(i) + ":" + label;
			}
			return out;
		}

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

			g_listPath = listPath;

			// Never add twice into one movie: a re-open normally rebuilds the list, but a movie that
			// outlives the close - some replacers and menu caches keep theirs - still holds our row,
			// and a second push would leave two identical ones.
			//
			// 1.8.4: finding it already there no longer RETURNS from this function. Until this version
			// it did, and that skipped the listener block below - which the close handler had just
			// dropped, because the listener belongs to the movie that was closed. The result is exactly
			// what borokoshow reported against Dragonborn UI: the row draws and pressing it does
			// nothing at all. Whether the row is found or pushed, the listener is attached afterwards.
			const std::uint32_t before = entryList.GetArraySize();
			bool               alreadyThere = false;
			for (std::uint32_t i = 0; i < before; ++i)
			{
				RE::GFxValue existing;
				RE::GFxValue text;
				if (entryList.GetElement(i, &existing) && existing.IsObject() &&
					existing.GetMember("text", &text) && text.IsString() &&
					std::string(text.GetString()) == kRowLabel)
				{
					g_rowIndex = static_cast<int>(i);
					alreadyThere = true;
					logger::debug("System row: already present at index {}; re-attaching the press listener",
						g_rowIndex);
					break;
				}
			}

			if (!alreadyThere)
			{
				RE::GFxValue entry;
				movie->CreateObject(&entry);
				RE::GFxValue label;
				label.SetString(kRowLabel);
				entry.SetMember("text", label);
				entryList.PushBack(entry);
				g_rowIndex = static_cast<int>(before);

				RE::GFxValue result;
				movie->Invoke((listPath + ".InvalidateData").c_str(), &result, nullptr, 0);
			}

			// What the menu actually holds, in order. Where the row ENDS UP is the game's business -
			// SetShowMod splices a Mod Manager row in at index 2 whenever the game wants one, which
			// moves everything below it - and a report that says "the row sat between HELP and QUIT"
			// can only be answered if the list of the day was written down.
			logger::info("System row: category list is now [{}]", RowTexts(movie, listPath));

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

	std::string ListJson()
	{
		std::string rows;
		RE::GPtr<RE::IMenu> menu = JournalMenu();
		if (menu && menu->uiMovie && !g_listPath.empty())
		{
			rows = RowTexts(menu->uiMovie.get(), g_listPath);
		}
		std::string esc;
		for (char c : rows)
		{
			if (c == '"' || c == '\\')
			{
				esc += '\\';
			}
			esc += c;
		}
		return std::string("{\"journalOpen\":") + ((menu && menu->uiMovie) ? "true" : "false") +
			   ",\"injected\":" + (WasInjected() ? "true" : "false") +
			   ",\"listener\":" + (g_listenerAdded ? "true" : "false") +
			   ",\"addedAtIndex\":" + std::to_string(g_rowIndex) +
			   ",\"page\":\"" + g_foundPath + "\",\"rows\":\"" + esc + "\"}";
	}

	namespace
	{
		float g_paneLeft = -1.0f;   // the journal art's pane divider, as a screen fraction; -1 = none seen

		// One clip's bounds as fractions of the stage, in _root space. Rejects a degenerate clip and
		// one bigger than the stage (a union of hidden children, not a panel).
		bool MeasureClip(RE::GFxMovieView* a_movie, const RE::GFxValue& a_root, const RE::GRectF& a_visible,
			const std::string& a_path, float& a_x, float& a_y, float& a_w, float& a_h)
		{
			if (a_path.empty() || a_path == ".")
			{
				return false;
			}
			const float visW = a_visible.right - a_visible.left;
			const float visH = a_visible.bottom - a_visible.top;
			RE::GFxValue bounds;
			RE::GFxValue args[1]{ a_root };
			const std::string fn = a_path + ".getBounds";
			if (!a_movie->Invoke(fn.c_str(), &bounds, args, 1) || !bounds.IsObject())
			{
				return false;
			}
			RE::GFxValue xMin, xMax, yMin, yMax;
			if (!bounds.GetMember("xMin", &xMin) || !bounds.GetMember("xMax", &xMax) ||
				!bounds.GetMember("yMin", &yMin) || !bounds.GetMember("yMax", &yMax) ||
				!xMin.IsNumber() || !xMax.IsNumber() || !yMin.IsNumber() || !yMax.IsNumber())
			{
				return false;
			}
			const float left = static_cast<float>(xMin.GetNumber());
			const float top = static_cast<float>(yMin.GetNumber());
			const float width = static_cast<float>(xMax.GetNumber()) - left;
			const float height = static_cast<float>(yMax.GetNumber()) - top;
			if (width < 1.0f || height < 1.0f)
			{
				return false;   // faded out, not built, or AS2's 6710886.4 for an empty clip
			}
			if (width > visW * 1.02f || height > visH * 1.02f)
			{
				logger::debug("System row: {} measures {:.0f}x{:.0f} against a {:.0f}x{:.0f} stage - bigger than the screen, so it is a union of hidden children",
					a_path, width, height, visW, visH);
				return false;
			}
			a_x = (left - a_visible.left) / visW;
			a_y = (top - a_visible.top) / visH;
			a_w = width / visW;
			a_h = height / visH;
			return true;
		}
	}

	bool MeasurePath(const std::string& a_path, float& a_x, float& a_y, float& a_w, float& a_h)
	{
		RE::GPtr<RE::IMenu> menu = JournalMenu();
		if (!menu || !menu->uiMovie)
		{
			return false;
		}
		RE::GFxMovieView* movie = menu->uiMovie.get();
		RE::GFxValue root;
		if (!movie->GetVariable(&root, "_root") || !root.IsObject())
		{
			return false;
		}
		const RE::GRectF visible = movie->GetVisibleFrameRect();
		if (visible.right - visible.left <= 1.0f || visible.bottom - visible.top <= 1.0f)
		{
			return false;
		}
		return MeasureClip(movie, root, visible, a_path, a_x, a_y, a_w, a_h);
	}

	float PaneLeft()
	{
		return g_paneLeft;
	}

	const char* ArtKey()
	{
		return g_paneLeft > 0.0f ? "qjo-redesign" : "panel";
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
		// handed _root: that is the one space the visible frame rect is also expressed in.
		RE::GFxValue root;
		if (!movie->GetVariable(&root, "_root") || !root.IsObject())
		{
			return false;
		}
		const RE::GRectF visible = movie->GetVisibleFrameRect();
		if (visible.right - visible.left <= 1.0f || visible.bottom - visible.top <= 1.0f)
		{
			return false;   // a degenerate stage would divide the fractions into nonsense
		}

		// WHICH CLIP TO MEASURE. Menu_mc measured larger than the screen (getBounds is the union of a
		// clip and ALL its children, and the journal keeps its hidden panels parented), so the page's
		// own PanelRect - the invisible rectangle the page lays its content out against - is the
		// answer; SystemPageRect and the page itself are the fallbacks.
		const std::string page = g_foundPath.empty() ? std::string() : g_foundPath;
		const std::string candidates[] = {
			page + ".PanelRect",
			page + ".SystemPageRect",
			page,
		};
		bool found = false;
		std::string foundAt;
		for (const std::string& path : candidates)
		{
			if (MeasureClip(movie, root, visible, path, a_x, a_y, a_w, a_h))
			{
				found = true;
				foundAt = path;
				break;
			}
		}
		if (!found)
		{
			logger::warn("System row: journal is open but no panel clip could be measured; the window keeps its own size");
			return false;
		}

		// Quest Journal Overhaul - Entire Journal Redesigned (the owner, 2026-09-13, two screenshots):
		// it keeps vanilla's PanelRect where it always was but draws the System page as a button
		// column LEFT of a vertical divider and a content pane RIGHT of it, headed by the selected
		// row's title and a rule. Fitting PanelRect put the window across the buttons. So when that
		// art's divider is on the stage, the pane is the rectangle: left of the divider's right
		// edge, below the header rule, out to the page's own right edge. Vanilla and the other
		// replacers have none of these clips and keep PanelRect untouched.
		g_paneLeft = -1.0f;
		{
			float dx = 0.0f, dy = 0.0f, dw = 0.0f, dh = 0.0f;
			const char* const dividers[] = {
				"_root.QuestJournalFader.Menu_mc.QJUI_PanelShadows_mc.RightDivider_mc",
				"_root.Menu_mc.QJUI_PanelShadows_mc.RightDivider_mc",
			};
			for (const char* d : dividers)
			{
				if (MeasureClip(movie, root, visible, d, dx, dy, dw, dh))
				{
					g_paneLeft = dx + dw + 0.008f;
					break;
				}
			}
		}
		if (g_paneLeft > 0.0f)
		{
			const float right0 = a_x + a_w;
			float right = right0;
			float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
			// RightFill_mc is the fill the redesign lays over exactly its content pane (frame 1,
			// QJUI_LayoutPanelShadows): its right and bottom edges are the pane's.
			float bottomFill = -1.0f;
			if (MeasureClip(movie, root, visible, "_root.QuestJournalFader.Menu_mc.QJUI_PanelShadows_mc.RightFill_mc", rx, ry, rw, rh))
			{
				right = rx + rw - 0.012f;
				bottomFill = ry + rh - 0.012f;
			}
			else if (MeasureClip(movie, root, visible, page + ".SystemPageRect", rx, ry, rw, rh) && rx + rw > right)
			{
				right = rx + rw - 0.012f;
			}
			float top = a_y;
			float hx = 0.0f, hy = 0.0f, hw = 0.0f, hh = 0.0f;
			const std::string rules[] = {
				page + ".QJS_Overlay.CategoryHeaderRule",   // where the redesign's script puts it
				page + ".CategoryHeader_mc.CategoryHeaderRule",
				page + ".CategoryHeaderRule",
				"_root.QuestJournalFader.Menu_mc.CategoryHeader_mc.CategoryHeaderRule",
			};
			for (const std::string& r : rules)
			{
				if (MeasureClip(movie, root, visible, r, hx, hy, hw, hh))
				{
					top = (std::max)(top, hy + hh + 0.01f);
					break;
				}
			}
			const float bottom = bottomFill > 0.0f ? bottomFill : a_y + a_h;
			if (g_paneLeft > a_x && right > g_paneLeft + 0.1f && bottom > top + 0.1f)
			{
				a_x = g_paneLeft;
				a_w = right - g_paneLeft;
				a_y = top;
				a_h = bottom - top;
				foundAt += " + Journal Redesigned pane (RightDivider_mc / CategoryHeaderRule / SystemPageRect)";
			}
			else
			{
				g_paneLeft = -1.0f;   // the divider was there but the pane it implies is nonsense; PanelRect stands
			}
		}

		// Clamp into the stage. A panel can sit a hair off the edge, and a window placed there is
		// one the player cannot reach the far side of.
		if (a_x < 0.0f) { a_w += a_x; a_x = 0.0f; }
		if (a_y < 0.0f) { a_h += a_y; a_y = 0.0f; }
		if (a_x + a_w > 1.0f) { a_w = 1.0f - a_x; }
		if (a_y + a_h > 1.0f) { a_h = 1.0f - a_y; }
		if (a_w <= 0.0f || a_h <= 0.0f)
		{
			return false;
		}

		static std::string reported;
		if (reported != foundAt)
		{
			reported = foundAt;
			logger::info("System row: journal panel measured at {} -> x={:.3f} y={:.3f} w={:.3f} h={:.3f} of the screen",
				foundAt, a_x, a_y, a_w, a_h);
		}
		return true;
	}
}
