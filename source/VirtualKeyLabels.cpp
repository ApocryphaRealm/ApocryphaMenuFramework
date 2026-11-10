#include "VirtualKeyLabels.h"

#include "utils/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

// See VirtualKeyLabels.h. 2.4.2.

namespace vkeylabels
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::mutex g_lock;
		const api::InterfaceV2* g_api = nullptr;       // Beyond VirtualKey's V2 table, once found
		Clock::time_point g_nextFind{};                 // when to look for VirtualKey.dll again while it is absent
		Clock::time_point g_nextRevision{};             // when to ask the catalog revision again
		std::uint64_t g_revision = ~0ull;               // the revision g_labels was copied at (~0 = never)
		std::unordered_map<std::int32_t, std::string> g_labels;  // virtual code -> "VK001"
		bool g_loggedFound = false;

		// VirtualKey.dll loaded and exporting a V2 table AMF can read. Absent, or an API older than V2 (the 0.1
		// prototype): nullptr, asked again at most every 10 s (it never appears late in practice; this costs nothing).
		const api::InterfaceV2* FindApi(Clock::time_point a_now)
		{
			if (g_api) { return g_api; }
			if (a_now < g_nextFind) { return nullptr; }
			g_nextFind = a_now + std::chrono::seconds(10);
			const HMODULE module = ::GetModuleHandleW(L"VirtualKey.dll");
			if (!module) { return nullptr; }
			const auto get = reinterpret_cast<api::GetInterfaceFn>(::GetProcAddress(module, "VirtualKey_GetInterface"));
			if (!get) { return nullptr; }
			const auto* table = static_cast<const api::InterfaceV2*>(get(api::kInterfaceVersionV2));
			if (!table || table->interfaceVersion < api::kInterfaceVersionV2 || table->structSize < sizeof(api::InterfaceV2) ||
				!table->GetBindingRevision || !table->CopyBindings) {
				if (!g_loggedFound) {
					g_loggedFound = true;
					logger::info("Virtual keys: VirtualKey.dll is loaded but offers no V2 interface - virtual codes read \"Virtual key N\"");
				}
				return nullptr;
			}
			g_api = table;
			logger::info("Virtual keys: Beyond VirtualKey's V2 interface found - virtual codes show its labels");
			return g_api;
		}

		// Copies the catalog again when its revision moved. Called at most once a second.
		void Refresh(const api::InterfaceV2& a_api)
		{
			if (!a_api.GetBindingRevision || !a_api.CopyBindings) { return; }   // FindApi checked them too; kept here for the gate
			if (a_api.IsReady && !a_api.IsReady()) { return; }
			const std::uint64_t revision = a_api.GetBindingRevision();
			if (revision == g_revision) { return; }
			const std::size_t count = a_api.CopyBindings(nullptr, 0);
			std::vector<api::BindingV2> rows(count + 8);    // room for a binding added between the two calls
			const std::size_t total = a_api.CopyBindings(rows.data(), rows.size());
			rows.resize((std::min)(total, rows.size()));
			g_labels.clear();
			for (const auto& row : rows) {
				const std::string label(row.keyLabelUtf8, ::strnlen(row.keyLabelUtf8, sizeof(row.keyLabelUtf8)));
				// "VK" alone is what Beyond VirtualKey writes when the code has no display index: no better than ours.
				if (IsVirtualKey(row.key) && label.size() > 2) { g_labels[row.key] = label; }
			}
			g_revision = revision;
			logger::info("Virtual keys: {} label(s) read from Beyond VirtualKey (catalog revision {})", g_labels.size(), revision);
		}
	}

	std::optional<std::string> Label(std::int32_t a_code)
	{
		if (!IsVirtualKey(a_code)) { return std::nullopt; }
		const std::scoped_lock lock(g_lock);
		const auto now = Clock::now();
		if (const auto* table = FindApi(now); table && now >= g_nextRevision) {
			g_nextRevision = now + std::chrono::seconds(1);
			Refresh(*table);
		}
		if (const auto it = g_labels.find(a_code); it != g_labels.end()) { return it->second; }
		return std::nullopt;
	}

	std::string Name(std::int32_t a_code)
	{
		if (auto label = Label(a_code)) { return std::move(*label); }
		return "Virtual key " + std::to_string(a_code);
	}
}
