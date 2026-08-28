#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "MainMenuDriver.h"
#include "Renderer.h"
#include "utils/Logger.h"

#include <string>

namespace devbenchtool
{
	namespace
	{
		// Minimal extractor for a top-level JSON string field: finds "key":"value" and returns
		// value (handling backslash escapes). Enough for this tool's small, controlled argument
		// shape - no dependency on a JSON library.
		std::string JsonStr(const std::string& a_json, const char* a_key)
		{
			const std::string key = std::string("\"") + a_key + "\"";
			auto pos = a_json.find(key);
			if (pos == std::string::npos)
			{
				return "";
			}
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string::npos)
			{
				return "";
			}
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t'))
			{
				++pos;
			}
			if (pos >= a_json.size() || a_json[pos] != '"')
			{
				return "";
			}
			++pos;
			std::string out;
			while (pos < a_json.size() && a_json[pos] != '"')
			{
				if (a_json[pos] == '\\' && pos + 1 < a_json.size())
				{
					++pos;
				}
				out += a_json[pos];
				++pos;
			}
			return out;
		}

		// Runs on devbench's LISTENER thread. Every renderer:: call used here is thread-safe
		// (atomic visibility, mutex-guarded selection, RunConsoleCommand marshals to the main
		// thread, registry::Snapshot is thread-safe), so no extra marshalling is needed.
		void MenuTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string args = a_argsJson ? a_argsJson : "{}";
			const std::string op = JsonStr(args, "op");

			std::string result;
			if (op == "open")
			{
				renderer::SetMenuVisible(true);
				result = "{\"ok\":true,\"op\":\"open\"}";
			}
			else if (op == "close")
			{
				renderer::SetMenuVisible(false);
				result = "{\"ok\":true,\"op\":\"close\"}";
			}
			else if (op == "select")
			{
				const std::string node = JsonStr(args, "node");
				renderer::SetSelectedNode(node);
				result = "{\"ok\":true,\"op\":\"select\",\"node\":\"" + node + "\"}";
			}
			else if (op == "activate")
			{
				renderer::ActivateSelectedNode();
				result = std::string("{\"ok\":true,\"op\":\"activate\",\"selected\":\"") +
						 renderer::GetSelectedNode() + "\"}";
			}
			else if (op == "state" || op.empty())
			{
				result = renderer::GetMenuStateJson();
			}
			else
			{
				result = "{\"ok\":false,\"error\":\"unknown op '" + op + "'\"}";
			}

			a_write(a_sink, result.c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered)
		{
			return;
		}

		auto* dev = DevBenchAPI::GetDevBenchInterface001();
		if (!dev)
		{
			if (a_lastAttempt)
			{
				logger::info("DevBench not detected; the amf.menu driving tool is unavailable this "
							 "session (the menu still works normally)");
			}
			else
			{
				logger::debug("DevBench not detected yet; will retry at the next message");
			}
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Drive and inspect the Apocrypha Menu Framework window for testing. "
			"op: open|close|select|activate|state. For select, node is a path: game-settings, stats, "
			"quest, general, system/save, system/load, system/savequit, system/quit, or mod:<index>. "
			"activate runs the selected node's action (Save/Quit). state returns visibility, the "
			"selected node and every registered mod + its pages.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{"
			"\"op\":{\"type\":\"string\"},\"node\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (dev->RegisterTool("amf.menu", descriptor, &MenuTool, nullptr))
		{
			logger::info("Registered \"amf.menu\" driving tool with DevBench (build {})", dev->GetBuildNumber());
		}
		else
		{
			logger::warn("DevBench reported \"amf.menu\" replaced an existing tool of the same name");
		}

		// Rule 64's start-menu extension: the vanilla Main Menu driver (amf.mainmenu).
		mainmenudriver::Register(dev);

		registered = true;
	}
}
