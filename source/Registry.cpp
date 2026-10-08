#include "Registry.h"

#include "utils/Logger.h"

#include <algorithm>
#include <mutex>

namespace registry
{
	namespace
	{
		std::mutex g_lock;
		std::vector<Entry> g_entries;
	}

	bool Register(const char* a_modName, const char* a_pageName, AMF_RenderCallback a_render)
	{
		if (!a_render)
		{
			logger::warn("AMF_RegisterPage refused: mod=\"{}\", page=\"{}\" - the render callback is null",
						 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>");
			return false;
		}
		return RegisterFn(a_modName, a_pageName, std::function<void()>(a_render));
	}

	bool RegisterFn(const char* a_modName, const char* a_pageName, std::function<void()> a_render, bool a_converted)
	{
		if (!a_modName || !*a_modName || !a_pageName || !*a_pageName || !a_render)
		{
			logger::warn("AMF_RegisterPage refused: mod=\"{}\", page=\"{}\", render set={} - every argument must be non-null/non-empty",
						 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>", static_cast<bool>(a_render));
			return false;
		}

		std::scoped_lock lock(g_lock);

		for (auto& entry : g_entries)
		{
			if (entry.modName == a_modName)
			{
				for (const auto& page : entry.pages)
				{
					if (page.pageName == a_pageName)
					{
						logger::warn("AMF_RegisterPage refused: \"{}\" already has a page \"{}\" (duplicate registration)",
									 a_modName, a_pageName);
						return false;
					}
				}

				entry.pages.push_back({ a_pageName, std::move(a_render), false, a_converted });
				logger::info("page registered: \"{}\" -> \"{}\" (mod now has {} page(s), rendered as tabs - one menu per mod)",
							 a_modName, a_pageName, entry.pages.size());
				return true;
			}
		}

		g_entries.push_back({ a_modName, { { a_pageName, std::move(a_render), false, a_converted } } });
		logger::info("first page registered for \"{}\": \"{}\" ({} mod(s) in the registry)",
					 a_modName, a_pageName, g_entries.size());
		return true;
	}

	bool SetPageVisible(const char* a_modName, const char* a_pageName, bool a_visible)
	{
		if (!a_modName || !*a_modName || !a_pageName || !*a_pageName)
		{
			logger::warn("AMF_SetPageVisible refused: mod=\"{}\", page=\"{}\" - both names must be non-empty",
						 a_modName ? a_modName : "<null>", a_pageName ? a_pageName : "<null>");
			return false;
		}

		std::scoped_lock lock(g_lock);
		for (auto& entry : g_entries)
		{
			if (entry.modName != a_modName) { continue; }
			for (auto& page : entry.pages)
			{
				if (page.pageName != a_pageName) { continue; }
				if (page.hidden == !a_visible) { return true; }  // already so; the consumer may call every frame
				page.hidden = !a_visible;
				logger::info("page {}: \"{}\" -> \"{}\"", a_visible ? "shown" : "hidden", a_modName, a_pageName);
				return true;
			}
		}
		logger::warn("AMF_SetPageVisible: \"{}\" has no registered page \"{}\"", a_modName, a_pageName);
		return false;
	}

	bool Rename(const std::string& a_modName, const std::string& a_pageName, const std::string& a_newName)
	{
		if (a_modName.empty() || a_newName.empty())
		{
			logger::warn("RenameSection (SMF-compat) refused: mod=\"{}\", page=\"{}\", new name=\"{}\"", a_modName, a_pageName, a_newName);
			return false;
		}
		std::scoped_lock lock(g_lock);
		const auto mod = std::ranges::find(g_entries, a_modName, &Entry::modName);
		if (mod == g_entries.end())
		{
			logger::warn("RenameSection (SMF-compat): no menu \"{}\"", a_modName);
			return false;
		}
		if (a_pageName.empty())
		{
			if (std::ranges::find(g_entries, a_newName, &Entry::modName) != g_entries.end())
			{
				logger::warn("RenameSection (SMF-compat): a menu \"{}\" already exists - \"{}\" not renamed", a_newName, a_modName);
				return false;
			}
			mod->modName = a_newName;
			logger::info("RenameSection (SMF-compat): menu \"{}\" -> \"{}\"", a_modName, a_newName);
			return true;
		}
		// the page itself and every page under it keep their place; only the renamed segment changes
		const auto slash = a_pageName.rfind('/');
		const std::string renamed = (slash == std::string::npos ? std::string() : a_pageName.substr(0, slash + 1)) + a_newName;
		bool done = false;
		for (auto& page : mod->pages)
		{
			if (page.pageName == a_pageName)
			{
				page.pageName = renamed;
				done = true;
			}
			else if (page.pageName.starts_with(a_pageName + "/"))
			{
				page.pageName = renamed + page.pageName.substr(a_pageName.size());
				done = true;
			}
		}
		if (done)
		{
			logger::info("RenameSection (SMF-compat): \"{}\" page \"{}\" -> \"{}\"", a_modName, a_pageName, renamed);
		}
		else
		{
			logger::warn("RenameSection (SMF-compat): \"{}\" has no page \"{}\"", a_modName, a_pageName);
		}
		return done;
	}

	bool Remove(const std::string& a_modName, const std::string& a_pageName)
	{
		std::scoped_lock lock(g_lock);
		const auto mod = std::ranges::find(g_entries, a_modName, &Entry::modName);
		if (a_modName.empty() || mod == g_entries.end())
		{
			logger::warn("DeleteSection (SMF-compat): no menu \"{}\"", a_modName);
			return false;
		}
		if (a_pageName.empty())
		{
			g_entries.erase(mod);
			logger::info("DeleteSection (SMF-compat): menu \"{}\" removed with all its pages ({} mod(s) left)", a_modName, g_entries.size());
			return true;
		}
		// the render thread draws from a snapshot, which holds its own copy of each callback - removing here is safe mid-frame
		const auto before = mod->pages.size();
		std::erase_if(mod->pages, [&](const Page& p) { return p.pageName == a_pageName || p.pageName.starts_with(a_pageName + "/"); });
		const auto removed = before - mod->pages.size();
		if (removed == 0)
		{
			logger::warn("DeleteSection (SMF-compat): \"{}\" has no page \"{}\"", a_modName, a_pageName);
			return false;
		}
		logger::info("DeleteSection (SMF-compat): \"{}\" lost {} page(s) at \"{}\"", a_modName, removed, a_pageName);
		if (mod->pages.empty())
		{
			g_entries.erase(mod);   // a menu with no page left is not listed (SMF drops the empty node too)
		}
		return true;
	}

	std::vector<Entry> Snapshot()
	{
		std::scoped_lock lock(g_lock);
		return g_entries;
	}

	std::size_t Count()
	{
		std::scoped_lock lock(g_lock);
		return g_entries.size();
	}
}
