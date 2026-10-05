#include "McmScripts.h"

#include "Input.h"
#include "Keyboard.h"
#include "McmShared.h"
#include "PreciseSlider.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

#undef GetObject

// See McmScripts.h. Part of the MCM loader. SkyUI's side of every call below was read from SKI_ConfigBase.psc
// and SKI_ConfigManager.psc (SkyUI SE 5.2, as carried in MCM Helper's MIT repo under scripts/private).

namespace mcmloader::scripts
{
	namespace
	{
		using namespace mcmloader::detail;
		using json = nlohmann::json;
		using ObjectPtr = RE::BSTSmartPointer<RE::BSScript::Object>;
		using Clock = std::chrono::steady_clock;
		using strings::TR;

		// SKI_ConfigBase's OPTION_TYPE_* and OPTION_FLAG_*
		enum : int { kEmpty = 0, kHeader = 1, kText = 2, kToggle = 3, kSlider = 4, kMenu = 5, kColor = 6, kKeymap = 7, kInput = 8 };
		constexpr int kFlagDisabled = 0x01;
		constexpr int kFlagHidden = 0x02;
		constexpr int kFlagWithUnmap = 0x04;
		constexpr int kSlots = 128;  // the option buffers' size; slot 2r is row r's left column, 2r+1 its right

		const char* TypeName(int a_type)
		{
			static const char* names[] = { "empty", "header", "text", "toggle", "slider", "menu", "color", "keymap", "input" };
			return a_type >= 0 && a_type <= kInput ? names[a_type] : "unknown";
		}

		struct Option
		{
			int type = kEmpty;
			int flags = 0;
			std::string text;
			std::string str;   // text value / menu value / slider format string
			float num = 0.0f;  // toggle 0/1, slider value, colour, key code
		};

		struct SliderParams
		{
			float start = 0.0f, def = 0.0f, min = 0.0f, max = 1.0f, interval = 1.0f;
		};

		// What the open config shows. Written on VM threads (call results) and the main thread, under g_mutex; the
		// page draws from a copy.
		struct Session
		{
			int mod = -1;
			int page = -2;          // the Pages index the options belong to (-1: the "" page), -2: none built yet
			std::string pageName;
			std::vector<Option> options = std::vector<Option>(kSlots);
			unsigned generation = 0;   // +1 per page build
			std::map<int, SliderParams> sliders;  // slot -> range, this page
			std::map<int, std::string> info;      // slot -> HighlightOption's text, this page
			int menuSlot = -1;
			bool menuReady = false;
			int menuStart = -1;
			std::vector<std::string> menuOptions;
		};

		struct SMod
		{
			RE::FormID questId = 0;
			std::string plugin;       // the quest's file stem: the name its translation file carries
			std::string modName;      // the ModName property, raw (may be a $key)
			std::string entryName;    // the AMF entry
			Table translations;
			std::string translationsLanguage;  // the TextLanguage() they were read in
			std::vector<std::string> pages;                  // Pages, raw, as last read
			std::map<std::string, std::string> tabs;         // raw page -> registered tab name
			ObjectPtr script;          // this game's config object (main thread writes; read under g_mutex)
			bool present = false;      // found in this game
			bool hidden = false;       // out of SkyUI's list (UnregisterMod) this game
			bool registered = false;   // _configManager set at the last pass (false: AMF drives it without SkyUI's manager)
		};

		std::mutex g_mutex;
		std::vector<std::unique_ptr<SMod>> g_mods;  // never shrinks: page closures hold indices
		Session g_session;
		std::atomic<int> g_open{ -1 };              // the config OpenConfig was sent to
		std::atomic<bool> g_openedByTool{ false };  // the DevBench tool opened it: no auto-close when no page draws it
		int g_lastDrawFrame = -10;
		Table g_skyuiTable;  // SkyUI's own strings ($Accept, $Cancel ...), main thread at first discovery

		// ---------------------------------------------------------------------------------------- text

		const Table* g_drawTable = nullptr;  // render thread: the table of the mod being drawn

		const std::string* Find(const Table* a_table, const std::string& a_key)
		{
			if (a_table)
			{
				if (const auto it = a_table->find(a_key); it != a_table->end()) { return &it->second; }
			}
			if (const auto it = g_skyuiTable.find(a_key); it != g_skyuiTable.end()) { return &it->second; }
			return nullptr;
		}

		// SkyUI's translation: "$KEY" whole first; then "$KEY{arg}{arg}" with each arg filling the translation's "{}" in
		// order (Translator.translateNested); an unknown key loses its "$" (as phases 1-2 show it).
		std::string Tr(const Table* a_table, const std::string& a_raw, int a_depth = 0)
		{
			if (a_raw.empty() || a_raw[0] != '$' || a_depth > 4) { return StripTags(a_raw); }
			if (const auto* text = Find(a_table, a_raw)) { return StripTags(*text); }
			const auto brace = a_raw.find('{');
			if (brace != std::string::npos)
			{
				// The translation files key a nested string with its braces empty ("$PEM_MCM_LimitContainers{}\tTotal
				// available {}, ..."), so "$KEY{}{}" is tried first, then the bare "$KEY".
				std::string key = a_raw.substr(0, brace);
				std::string emptied = key;
				for (std::size_t i = brace; i < a_raw.size(); ++i)
				{
					if (a_raw[i] == '{') { emptied += "{}"; }
				}
				const std::string* text = Find(a_table, emptied);
				if (!text) { text = Find(a_table, key); }
				if (text)
				{
					std::string out = *text;
					std::size_t pos = brace;
					while (pos < a_raw.size() && a_raw[pos] == '{')
					{
						const auto close = a_raw.find('}', pos);
						if (close == std::string::npos) { break; }
						const auto at = out.find("{}");
						if (at == std::string::npos) { break; }
						out.replace(at, 2, Tr(a_table, a_raw.substr(pos + 1, close - pos - 1), a_depth + 1));
						pos = close + 1;
					}
					return StripTags(out);
				}
			}
			return StripTags(a_raw.substr(1));
		}

		std::string Tr(const std::string& a_raw) { return Tr(g_drawTable, a_raw); }

		// A slider's format string: "{N}" is the value with N decimals ("{0}", "{1} sec", "$Every {0} seconds" - the
		// whole string is a translation key first, as SkyUI translates it before filling the number in).
		// Returns a printf format for ImGui with the number as "%.Nf" (so precise::SliderFloat steps one shown digit).
		std::string SliderFormat(const std::string& a_skyui)
		{
			std::string fmt = a_skyui.empty() ? std::string("{0}") : a_skyui;
			if (fmt[0] == '$') { fmt = Tr(fmt); }
			std::string out;
			bool placed = false;
			for (std::size_t i = 0; i < fmt.size(); ++i)
			{
				const char c = fmt[i];
				if (c == '%') { out += "%%"; continue; }
				if (c == '{')
				{
					const auto close = fmt.find('}', i);
					if (close != std::string::npos)
					{
						const std::string n = fmt.substr(i + 1, close - i - 1);
						const bool digits = !n.empty() && std::all_of(n.begin(), n.end(), [](unsigned char d) { return std::isdigit(d); });
						if (digits || n.empty())
						{
							out += "%." + (digits ? n : std::string("0")) + "f";
							placed = true;
							i = close;
							continue;
						}
					}
				}
				out += c;
			}
			if (!placed) { out = "%.0f " + out; }  // a format without a number: SkyUI shows the text; keep the value visible
			return out;
		}

		// ---------------------------------------------------------------------------------------- script variables

		// A script variable by name through the type chain, parents' variables first - MCM Helper's
		// ScriptObject::GetVariable (MIT, github.com/Exit-9B/MCM-Helper src/Script/ScriptObject.cpp).
		RE::BSScript::Variable* Var(const ObjectPtr& a_object, std::string_view a_name)
		{
			if (!a_object) { return nullptr; }
			constexpr auto kInvalid = static_cast<std::uint32_t>(-1);
			auto index = kInvalid;
			std::uint32_t offset = 0;
			for (auto cls = a_object->type.get(); cls; cls = cls->GetParent())
			{
				if (index == kInvalid)
				{
					const auto vars = cls->GetVariableIter();
					for (std::uint32_t i = 0; vars && i < cls->GetNumVariables(); ++i)
					{
						if (vars[i].name == a_name)
						{
							index = i;
							break;
						}
					}
				}
				else
				{
					offset += cls->GetNumVariables();
				}
			}
			if (index == kInvalid) { return nullptr; }
			return std::addressof(a_object->variables[offset + index]);
		}

		template <class F>
		std::uint32_t ForArray(const ObjectPtr& a_object, std::string_view a_name, F&& a_fn)
		{
			const auto var = Var(a_object, a_name);
			if (!var || !var->IsArray()) { return 0; }
			const auto array = var->GetArray();
			if (!array) { return 0; }
			const auto n = array->size();
			for (std::uint32_t i = 0; i < n; ++i) { a_fn(i, (*array)[i]); }
			return n;
		}

		bool IsType(const ObjectPtr& a_object, const char* a_name)
		{
			for (auto cls = a_object ? a_object->type.get() : nullptr; cls; cls = cls->GetParent())
			{
				if (_stricmp(cls->GetName(), a_name) == 0) { return true; }
			}
			return false;
		}

		// The page as the script built it (VM thread, right after SetPage returned).
		std::vector<Option> ReadOptions(const ObjectPtr& a_config)
		{
			std::vector<Option> out(kSlots);
			const auto flags = ForArray(a_config, "_optionFlagsBuf", [&](std::uint32_t i, RE::BSScript::Variable& v) {
				if (i < kSlots && v.IsInt())
				{
					const int f = v.GetSInt();  // AddOption: type + flags * 0x100
					out[i].type = f & 0xFF;
					out[i].flags = (f >> 8) & 0xFF;
				}
			});
			ForArray(a_config, "_textBuf", [&](std::uint32_t i, RE::BSScript::Variable& v) { if (i < kSlots && v.IsString()) { out[i].text = std::string(v.GetString()); } });
			ForArray(a_config, "_strValueBuf", [&](std::uint32_t i, RE::BSScript::Variable& v) { if (i < kSlots && v.IsString()) { out[i].str = std::string(v.GetString()); } });
			ForArray(a_config, "_numValueBuf", [&](std::uint32_t i, RE::BSScript::Variable& v) { if (i < kSlots && v.IsFloat()) { out[i].num = v.GetFloat(); } });
			if (flags < kSlots) { logger::debug("MCM scripts: option buffers hold {} slot(s), not {}", flags, kSlots); }
			return out;
		}

		std::vector<std::string> ReadStringArray(const RE::BSScript::Variable* a_var)
		{
			std::vector<std::string> out;
			if (!a_var || !a_var->IsArray()) { return out; }
			const auto array = a_var->GetArray();
			if (!array) { return out; }
			for (std::uint32_t i = 0; i < array->size(); ++i)
			{
				const auto& v = (*array)[i];
				out.emplace_back(v.IsString() ? std::string(v.GetString()) : std::string());
			}
			return out;
		}

		// ------------------------------------------------- UI.InvokeStringA recorder (menu lists, message boxes)
		//
		// SetMenuDialogOptions and ShowMessage hand their strings only to the Journal's Flash panel
		// (UI.InvokeStringA("Journal Menu", "_root.ConfigPanelFader.configPanel.setMenuDialogOptions" / ".showMessageDialog",
		// ...)). With the Journal closed SKSE drops those calls, so AMF puts a forwarder in UI.InvokeStringA's slot of the
		// VM's UI type: it records those two calls while the Journal is closed and ALWAYS runs the original afterwards.

		std::mutex g_captureMutex;
		std::vector<std::string> g_capturedMenu;
		bool g_menuCaptured = false;
		struct Message
		{
			bool pending = false;
			std::string text, accept, cancel;
			int mod = -1;
		} g_message;

		bool EndsWith(std::string_view a_s, std::string_view a_tail)
		{
			return a_s.size() >= a_tail.size() && a_s.substr(a_s.size() - a_tail.size()) == a_tail;
		}

		void Observe(const RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack)
		{
			const auto stack = a_stack.get();
			const auto frame = stack ? stack->top : nullptr;
			if (!frame) { return; }
			const auto page = frame->GetPageForFrame();
			const auto& menu = frame->GetStackFrameVariable(0, page);
			const auto& target = frame->GetStackFrameVariable(1, page);
			if (!menu.IsString() || !target.IsString() || menu.GetString() != "Journal Menu") { return; }
			const std::string_view t = target.GetString();
			const bool isMenu = EndsWith(t, ".setMenuDialogOptions");
			const bool isMessage = EndsWith(t, ".showMessageDialog");
			if (!isMenu && !isMessage) { return; }
			if (const auto ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen("Journal Menu")) { return; }  // SkyUI's own menu is showing it
			const auto strings = ReadStringArray(&frame->GetStackFrameVariable(2, page));
			std::scoped_lock lock(g_captureMutex);
			if (isMenu)
			{
				g_capturedMenu = strings;
				g_menuCaptured = true;
				logger::debug("MCM scripts: recorded a menu list of {} option(s)", strings.size());
			}
			else
			{
				g_message.pending = true;
				g_message.text = strings.size() > 0 ? strings[0] : std::string();
				g_message.accept = strings.size() > 1 ? strings[1] : std::string("$Accept");
				g_message.cancel = strings.size() > 2 ? strings[2] : std::string();
				g_message.mod = g_open.load();
				logger::info("MCM scripts: the mod asked a question (ShowMessage) - shown as AMF's popup");
			}
		}

		class InvokeStringAForwarder : public RE::BSScript::IFunction
		{
		public:
			explicit InvokeStringAForwarder(RE::BSTSmartPointer<RE::BSScript::IFunction> a_original) : _original(std::move(a_original)) {}

			const RE::BSFixedString& GetName() const override { return _original->GetName(); }
			const RE::BSFixedString& GetObjectTypeName() const override { return _original->GetObjectTypeName(); }
			const RE::BSFixedString& GetStateName() const override { return _original->GetStateName(); }
			RE::BSScript::TypeInfo GetReturnType() const override { return _original->GetReturnType(); }
			std::uint32_t GetParamCount() const override { return _original->GetParamCount(); }
			void GetParam(std::uint32_t a_idx, RE::BSFixedString& a_name, RE::BSScript::TypeInfo& a_type) const override { _original->GetParam(a_idx, a_name, a_type); }
			std::uint32_t GetStackFrameSize() const override { return _original->GetStackFrameSize(); }
			bool GetIsNative() const override { return _original->GetIsNative(); }
			bool GetIsStatic() const override { return _original->GetIsStatic(); }
			bool GetIsEmpty() const override { return _original->GetIsEmpty(); }
			FunctionType GetFunctionType() const override { return _original->GetFunctionType(); }
			std::uint32_t GetUserFlags() const override { return _original->GetUserFlags(); }
			const RE::BSFixedString& GetDocString() const override { return _original->GetDocString(); }
			void InsertLocals(RE::BSScript::StackFrame* a_frame) override { _original->InsertLocals(a_frame); }
			CallResult Call(const RE::BSTSmartPointer<RE::BSScript::Stack>& a_stack, RE::BSScript::ErrorLogger* a_logger,
				RE::BSScript::Internal::VirtualMachine* a_vm, bool a_arg4) override
			{
				try { Observe(a_stack); }
				catch (...) {}  // recording must never stand in the original's way
				return _original->Call(a_stack, a_logger, a_vm, a_arg4);
			}
			const RE::BSFixedString& GetSourceFilename() const override { return _original->GetSourceFilename(); }
			bool TranslateIPToLineNumber(std::uint32_t a_ip, std::uint32_t& a_line) const override { return _original->TranslateIPToLineNumber(a_ip, a_line); }
			bool GetVarNameForStackIndex(std::uint32_t a_idx, RE::BSFixedString& a_name) const override { return _original->GetVarNameForStackIndex(a_idx, a_name); }
			bool CanBeCalledFromTasklets() const override { return _original->CanBeCalledFromTasklets(); }
			void SetCallableFromTasklets(bool a_callable) override { _original->SetCallableFromTasklets(a_callable); }

			RE::BSTSmartPointer<RE::BSScript::IFunction> _original;
		};

		RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> g_uiType;  // main thread
		RE::BSTSmartPointer<RE::BSScript::IFunction> g_original;
		RE::BSTSmartPointer<RE::BSScript::IFunction> g_forwarder;
		std::string g_captureState = "not installed";

		RE::BSScript::ObjectTypeInfo::GlobalFuncInfo* FindSlot()
		{
			if (!g_uiType) { return nullptr; }
			const auto funcs = g_uiType->GetGlobalFuncIter();
			for (std::uint32_t i = 0; funcs && i < g_uiType->GetNumGlobalFuncs(); ++i)
			{
				if (funcs[i].func && funcs[i].func->GetName() == "InvokeStringA") { return &funcs[i]; }
			}
			return nullptr;
		}

		// Main thread. Re-checked on every discovery pass: if anything put a different function in the slot since, ours goes
		// back over IT (and forwards to it).
		void InstallRecorder()
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) { return; }
			if (!g_uiType && (!vm->GetScriptObjectType("UI", g_uiType) || !g_uiType))
			{
				g_captureState = "UI is not a loaded script type yet";
				logger::debug("MCM scripts: {} - the recorder waits for the next pass", g_captureState);
				return;
			}
			const auto slot = FindSlot();
			if (!slot)
			{
				g_captureState = "UI.InvokeStringA not found";
				logger::warn("MCM scripts: {} - menu lists and message boxes cannot be shown", g_captureState);
				return;
			}
			if (g_forwarder && slot->func.get() == g_forwarder.get()) { return; }
			g_original = slot->func;
			g_forwarder.reset(new InvokeStringAForwarder(g_original));
			slot->func = g_forwarder;
			g_captureState = "installed";
			logger::info("MCM scripts: UI.InvokeStringA forwarded through the recorder (original {:X}, forwarder {:X})",
				reinterpret_cast<std::uintptr_t>(g_original.get()), reinterpret_cast<std::uintptr_t>(g_forwarder.get()));
		}

		void RemoveRecorder()
		{
			const auto slot = FindSlot();
			if (slot && g_forwarder && slot->func.get() == g_forwarder.get() && g_original)
			{
				slot->func = g_original;
				logger::info("MCM scripts: UI.InvokeStringA given back its original");
			}
			g_captureState = "not installed";
		}

		// ---------------------------------------------------------------------------------------- the call queue
		//
		// One call at a time, the next started from the previous one's completion - SkyUI's manager is single-threaded
		// too, and _activeOption / _state are shared by every dialog. An op runs on the main thread and calls done()
		// exactly once (from a VM thread when it waited for a call).

		struct Op
		{
			std::string key;  // non-empty: a newer op with the same key replaces this one while it is still waiting
			std::string name;
			std::function<void(std::function<void()>)> run;
		};

		std::mutex g_queueMutex;
		std::deque<Op> g_queue;
		bool g_busy = false;
		Clock::time_point g_busySince{};
		std::string g_busyWith;
		unsigned g_busyTicket = 0;

		void Pump();

		void Enqueue(Op a_op)
		{
			{
				std::scoped_lock lock(g_queueMutex);
				bool replaced = false;
				if (!a_op.key.empty())
				{
					for (auto& queued : g_queue)
					{
						if (queued.key == a_op.key)
						{
							queued = a_op;
							replaced = true;
							break;
						}
					}
				}
				if (!replaced) { g_queue.push_back(std::move(a_op)); }
			}
			Pump();
		}

		void Pump()
		{
			Op op;
			unsigned ticket = 0;
			{
				std::scoped_lock lock(g_queueMutex);
				if (g_busy || g_queue.empty()) { return; }
				g_busy = true;
				g_busySince = Clock::now();
				op = std::move(g_queue.front());
				g_queue.pop_front();
				g_busyWith = op.name;
				ticket = ++g_busyTicket;
			}
			auto finished = std::make_shared<std::atomic<bool>>(false);
			auto done = [finished, ticket]() {
				if (finished->exchange(true)) { return; }
				{
					std::scoped_lock lock(g_queueMutex);
					if (g_busyTicket != ticket) { return; }  // the watchdog already moved on
					g_busy = false;
					g_busyWith.clear();
				}
				Pump();
			};
			const auto tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				done();
				return;
			}
			tasks->AddTask([op = std::move(op), done]() {
				logger::debug("MCM scripts: {}", op.name);
				op.run(done);
			});
		}

		// A call that never finishes (a script waiting on something that will not come while the game is paused) would
		// hold the queue for good. After 15 s with no question showing, the queue moves on and says so.
		void Watchdog()
		{
			bool stalled = false;
			std::string with;
			{
				std::scoped_lock lock(g_queueMutex);
				stalled = g_busy && Clock::now() - g_busySince > std::chrono::seconds(15);
				with = g_busyWith;
			}
			{
				std::scoped_lock lock(g_captureMutex);
				if (g_message.pending) { stalled = false; }
			}
			if (!stalled) { return; }
			logger::warn("MCM scripts: \"{}\" has not finished after 15 s - the queue moves on without it", with);
			{
				std::scoped_lock lock(g_queueMutex);
				g_busy = false;
				++g_busyTicket;
				g_busyWith.clear();
			}
			Pump();
		}

		void ClearQueue()
		{
			std::scoped_lock lock(g_queueMutex);
			g_queue.clear();
		}

		// Main thread: one call on the config script. a_then runs on a VM thread once it has finished.
		bool Call(const ObjectPtr& a_object, const char* a_function, VarArgs&& a_args, std::function<void(const RE::BSScript::Variable&)> a_then)
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm || !a_object) { return false; }
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> callback{ new ResultFn(std::move(a_then)) };
			ObjectPtr object = a_object;
			const bool sent = vm->DispatchMethodCall(object, a_function, &a_args, callback);
			if (!sent) { logger::warn("MCM scripts: {} could not be called on the config script", a_function); }
			return sent;
		}

		VarArgs Args() { return VarArgs{}; }
		VarArgs Args(std::int32_t a_i)
		{
			VarArgs v;
			v.args.resize(1);
			v.args[0].SetSInt(a_i);
			return v;
		}
		VarArgs ArgsF(float a_f)
		{
			VarArgs v;
			v.args.resize(1);
			v.args[0].SetFloat(a_f);
			return v;
		}
		VarArgs ArgsS(const std::string& a_s)
		{
			VarArgs v;
			v.args.resize(1);
			v.args[0].SetString(a_s);
			return v;
		}

		ObjectPtr ScriptOf(int a_mod)
		{
			std::scoped_lock lock(g_mutex);
			if (a_mod < 0 || a_mod >= static_cast<int>(g_mods.size())) { return {}; }
			return g_mods[a_mod]->script;
		}

		// ------------------------------------------------------------------------------- the ops SkyUI's manager makes

		void PrefetchSliders(int a_mod);

		// SetPage(name, index) - OnPageReset fills the buffers - then read them.
		void Build(int a_mod, const ObjectPtr& a_config, int a_page, const std::string& a_name, std::function<void()> a_done)
		{
			VarArgs args;
			args.args.resize(2);
			args.args[0].SetString(a_name);
			args.args[1].SetSInt(a_page);
			const bool sent = Call(a_config, "SetPage", std::move(args), [a_mod, a_config, a_page, a_name, a_done](const RE::BSScript::Variable&) {
				auto options = ReadOptions(a_config);
				bool newPage = false;
				{
					std::scoped_lock lock(g_mutex);
					if (g_open.load() == a_mod)
					{
						newPage = g_session.page != a_page;
						g_session.mod = a_mod;
						g_session.page = a_page;
						g_session.pageName = a_name;
						g_session.options = std::move(options);
						++g_session.generation;
						if (newPage) { g_session.sliders.clear(); }
						g_session.info.clear();
					}
				}
				PrefetchSliders(a_mod);
				a_done();
			});
			if (!sent) { a_done(); }
		}

		// The page currently open, rebuilt (after every change: the script's own Set*OptionValue calls reach only the Flash
		// panel, and SetOptionFlags never writes back to the arrays - a fresh OnPageReset is the state the script holds).
		void Rebuild(int a_mod, const ObjectPtr& a_config, std::function<void()> a_done)
		{
			int page = -1;
			std::string name;
			{
				std::scoped_lock lock(g_mutex);
				page = g_session.page;
				name = g_session.pageName;
			}
			if (page == -2)
			{
				a_done();
				return;
			}
			Build(a_mod, a_config, page, name, std::move(a_done));
		}

		void QueueOpen(int a_mod)
		{
			Enqueue({ "", "OpenConfig", [a_mod](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || !Call(config, "OpenConfig", Args(), [a_done](const RE::BSScript::Variable&) { a_done(); })) { a_done(); }
			} });
		}

		void QueuePage(int a_mod, int a_page, const std::string& a_name)
		{
			Enqueue({ "page", "SetPage " + a_name, [a_mod, a_page, a_name](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || g_open.load() != a_mod) { a_done(); return; }
				Build(a_mod, config, a_page, a_name, a_done);
			} });
		}

		void QueueClose(int a_mod)
		{
			ClearQueue();  // whatever was waiting belonged to the config being closed
			Enqueue({ "", "CloseConfig", [a_mod](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || !Call(config, "CloseConfig", Args(), [a_done](const RE::BSScript::Variable&) { a_done(); })) { a_done(); }
			} });
		}

		void PrefetchSliders(int a_mod)
		{
			std::vector<int> missing;
			{
				std::scoped_lock lock(g_mutex);
				if (g_open.load() != a_mod) { return; }
				for (int i = 0; i < kSlots; ++i)
				{
					if (g_session.options[i].type == kSlider && !g_session.sliders.contains(i)) { missing.push_back(i); }
				}
			}
			for (const int slot : missing)
			{
				Enqueue({ "range:" + std::to_string(slot), "RequestSliderDialogData " + std::to_string(slot), [a_mod, slot](std::function<void()> a_done) {
					const auto config = ScriptOf(a_mod);
					if (!config || g_open.load() != a_mod) { a_done(); return; }
					if (!Call(config, "RequestSliderDialogData", Args(slot), [a_mod, slot, config, a_done](const RE::BSScript::Variable&) {
							SliderParams p;
							float* fields[] = { &p.start, &p.def, &p.min, &p.max, &p.interval };
							ForArray(config, "_sliderParams", [&](std::uint32_t i, RE::BSScript::Variable& v) { if (i < 5 && v.IsFloat()) { *fields[i] = v.GetFloat(); } });
							if (p.interval <= 0.0f) { p.interval = 1.0f; }
							if (p.max < p.min) { std::swap(p.min, p.max); }
							{
								std::scoped_lock lock(g_mutex);
								if (g_open.load() == a_mod) { g_session.sliders[slot] = p; }
							}
							a_done();
						}))
					{
						a_done();
					}
				} });
			}
		}

		// An interaction: the Request (when the dialog has one, so _activeOption is this option even if another call ran
		// in between), the accept, then the page rebuilt.
		void QueueChange(int a_mod, int a_slot, const char* a_request, const char* a_accept, std::function<VarArgs()> a_acceptArgs, const std::string& a_key = {})
		{
			std::string name = std::string(a_accept) + " " + std::to_string(a_slot);
			Enqueue({ a_key, name, [a_mod, a_slot, a_request, a_accept, a_acceptArgs](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || g_open.load() != a_mod) { a_done(); return; }
				auto accept = [a_mod, config, a_accept, a_acceptArgs, a_done]() {
					if (!Call(config, a_accept, a_acceptArgs(), [a_mod, config, a_done](const RE::BSScript::Variable&) { Rebuild(a_mod, config, a_done); })) { a_done(); }
				};
				if (!a_request) { accept(); return; }
				// the accept runs on a VM thread here; dispatching from it is what the manager's own events do
				if (!Call(config, a_request, Args(a_slot), [accept](const RE::BSScript::Variable&) { accept(); })) { a_done(); }
			} });
		}

		void QueueSelect(int a_mod, int a_slot) { QueueChange(a_mod, a_slot, nullptr, "SelectOption", [a_slot]() { return Args(a_slot); }); }
		void QueueDefault(int a_mod, int a_slot) { QueueChange(a_mod, a_slot, nullptr, "ResetOption", [a_slot]() { return Args(a_slot); }); }
		void QueueSlider(int a_mod, int a_slot, float a_v)
		{
			QueueChange(a_mod, a_slot, "RequestSliderDialogData", "SetSliderValue", [a_v]() { return ArgsF(a_v); }, "slider:" + std::to_string(a_slot));
		}
		void QueueMenu(int a_mod, int a_slot, int a_index)
		{
			QueueChange(a_mod, a_slot, "RequestMenuDialogData", "SetMenuIndex", [a_index]() { return Args(a_index); });
		}
		void QueueColor(int a_mod, int a_slot, int a_color)
		{
			QueueChange(a_mod, a_slot, "RequestColorDialogData", "SetColorValue", [a_color]() { return Args(a_color); }, "color:" + std::to_string(a_slot));
		}
		void QueueInput(int a_mod, int a_slot, const std::string& a_text)
		{
			QueueChange(a_mod, a_slot, "RequestInputDialogData", "SetInputText", [a_text]() { return ArgsS(a_text); });
		}
		// RemapKey(index, keyCode, conflictControl, conflictName). The conflict is passed empty for now (PLAN: owed) - SkyUI's
		// manager looks it up in the game's control map and every registered config's GetCustomControl first.
		void QueueKey(int a_mod, int a_slot, int a_code)
		{
			QueueChange(a_mod, a_slot, nullptr, "RemapKey", [a_slot, a_code]() {
				VarArgs v;
				v.args.resize(4);
				v.args[0].SetSInt(a_slot);
				v.args[1].SetSInt(a_code);
				v.args[2].SetString("");
				v.args[3].SetString("");
				return v;
			});
		}

		// The menu dialog's list: RequestMenuDialogData runs OnOptionMenuOpen, whose SetMenuDialogOptions reaches the recorder.
		void QueueMenuList(int a_mod, int a_slot)
		{
			{
				std::scoped_lock lock(g_mutex);
				g_session.menuSlot = a_slot;
				g_session.menuReady = false;
			}
			Enqueue({ "menulist", "RequestMenuDialogData " + std::to_string(a_slot), [a_mod, a_slot](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || g_open.load() != a_mod) { a_done(); return; }
				{
					std::scoped_lock lock(g_captureMutex);
					g_capturedMenu.clear();
					g_menuCaptured = false;
				}
				if (!Call(config, "RequestMenuDialogData", Args(a_slot), [a_mod, a_slot, config, a_done](const RE::BSScript::Variable&) {
						int start = -1;
						ForArray(config, "_menuParams", [&](std::uint32_t i, RE::BSScript::Variable& v) { if (i == 0 && v.IsInt()) { start = v.GetSInt(); } });
						std::vector<std::string> options;
						bool captured = false;
						{
							std::scoped_lock lock(g_captureMutex);
							options = g_capturedMenu;
							captured = g_menuCaptured;
						}
						if (!captured) { logger::warn("MCM scripts: menu {} - no list reached the recorder ({})", a_slot, g_captureState); }
						{
							std::scoped_lock lock(g_mutex);
							if (g_open.load() == a_mod && g_session.menuSlot == a_slot)
							{
								g_session.menuOptions = std::move(options);
								g_session.menuStart = start;
								g_session.menuReady = true;
							}
						}
						a_done();
					}))
				{
					a_done();
				}
			} });
		}

		// HighlightOption(i) leaves the mod's help text in _infoText.
		void QueueInfo(int a_mod, int a_slot)
		{
			Enqueue({ "info", "HighlightOption " + std::to_string(a_slot), [a_mod, a_slot](std::function<void()> a_done) {
				const auto config = ScriptOf(a_mod);
				if (!config || g_open.load() != a_mod) { a_done(); return; }
				if (!Call(config, "HighlightOption", Args(a_slot), [a_mod, a_slot, config, a_done](const RE::BSScript::Variable&) {
						const auto var = Var(config, "_infoText");
						std::string text = var && var->IsString() ? std::string(var->GetString()) : std::string();
						{
							std::scoped_lock lock(g_mutex);
							if (g_open.load() == a_mod) { g_session.info[a_slot] = std::move(text); }
						}
						a_done();
					}))
				{
					a_done();
				}
			} });
		}

		// The answer to a ShowMessage, sent as SkyUI's own mod event (SKI_ConfigBase.OnMessageDialogClose reads numArg).
		void Answer(bool a_yes)
		{
			{
				std::scoped_lock lock(g_captureMutex);
				if (!g_message.pending) { return; }
				g_message.pending = false;
			}
			logger::info("MCM scripts: question answered {}", a_yes ? "yes" : "no");
			if (const auto tasks = SKSE::GetTaskInterface())
			{
				tasks->AddTask([a_yes]() {
					SKSE::ModCallbackEvent event{ "SKICP_messageDialogClosed", "", a_yes ? 1.0f : 0.0f, nullptr };
					if (const auto source = SKSE::GetModCallbackEventSource()) { source->SendEvent(&event); }
				});
			}
		}

		void Open(int a_mod, bool a_byTool)
		{
			const int open = g_open.load();
			if (open == a_mod) { return; }
			if (open >= 0)
			{
				Answer(false);
				QueueClose(open);
			}
			{
				std::scoped_lock lock(g_mutex);
				g_session = Session{};
				g_session.mod = a_mod;
			}
			g_openedByTool.store(a_byTool);
			g_open.store(a_mod);
			QueueOpen(a_mod);
		}

		void Close()
		{
			const int open = g_open.exchange(-1);
			if (open < 0) { return; }
			Answer(false);  // a question still showing would hold the mod's script in its wait loop
			QueueClose(open);
			std::scoped_lock lock(g_mutex);
			g_session = Session{};
		}

		// ---------------------------------------------------------------------------------------------- drawing

		int g_requestedPage = -100;   // render thread: the page last queued, so a frame does not queue it again
		int g_requestedFor = -1;
		int g_menuAsked = -1;         // render thread: the open combo whose list was asked for
		std::pair<unsigned, int> g_infoAsked{ ~0u, -1 };
		std::optional<int> g_capturing;  // render thread: the keymap slot waiting for a key

		void HelpTooltip(int a_mod, const Session& a_s, int a_slot)
		{
			if (!ImGui::IsItemHovered()) { return; }
			if (const auto it = a_s.info.find(a_slot); it != a_s.info.end())
			{
				if (!it->second.empty()) { ImGui::SetTooltip("%s", Tr(it->second).c_str()); }
				return;
			}
			if (g_infoAsked != std::make_pair(a_s.generation, a_slot))
			{
				g_infoAsked = { a_s.generation, a_slot };
				QueueInfo(a_mod, a_slot);
			}
		}

		void DefaultMenu(int a_mod, int a_slot)
		{
			if (ImGui::BeginPopupContextItem("##default"))
			{
				if (ImGui::MenuItem(TR("AMF_McmScriptDefault", "Reset to default"))) { QueueDefault(a_mod, a_slot); }
				ImGui::EndPopup();
			}
		}

		void DrawOption(int a_mod, const Session& a_s, int a_slot)
		{
			const Option& o = a_s.options[a_slot];
			if (o.type == kEmpty || (o.flags & kFlagHidden)) { return; }
			const bool disabled = (o.flags & kFlagDisabled) != 0;
			const std::string label = Tr(o.text);
			ImGui::PushID(a_slot);
			if (disabled) { ImGui::BeginDisabled(); }
			switch (o.type)
			{
			case kHeader:
				ImGui::SeparatorText(label.empty() ? " " : label.c_str());
				break;
			case kText:
			{
				const std::string value = Tr(o.str);
				std::string row = label.empty() ? value : label;
				if (row.empty()) { row = " "; }
				if (ImGui::Selectable((row + "##text").c_str())) { QueueSelect(a_mod, a_slot); }
				if (!label.empty() && !value.empty())
				{
					const ImVec2 max = ImGui::GetItemRectMax();
					const ImVec2 min = ImGui::GetItemRectMin();
					const float w = ImGui::CalcTextSize(value.c_str()).x;
					ImGui::GetWindowDrawList()->AddText(ImVec2(max.x - w, min.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), value.c_str());
				}
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				break;
			}
			case kToggle:
			{
				bool v = o.num != 0.0f;
				if (widgets::Toggle(label.empty() ? "##toggle" : label.c_str(), &v)) { QueueSelect(a_mod, a_slot); }
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				break;
			}
			case kSlider:
			{
				const auto it = a_s.sliders.find(a_slot);
				const std::string format = SliderFormat(o.str);
				if (it == a_s.sliders.end())
				{
					char shown[128]{};
					snprintf(shown, sizeof(shown), format.c_str(), o.num);
					ImGui::TextUnformatted(label.c_str());
					ImGui::SameLine();
					ImGui::TextDisabled("%s", shown);
					break;
				}
				const SliderParams& p = it->second;
				float v = o.num;
				const float before = v;
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				if (precise::SliderFloat(("##slider" + std::to_string(a_slot)).c_str(), &v, p.min, p.max, format.c_str()))
				{
					// the slider's interval can be coarser than the shown digit: a one-digit nudge moves a whole interval
					if (v != before && std::fabs(v - before) < p.interval * 0.5f) { v = before + (v > before ? p.interval : -p.interval); }
					v = p.min + std::round((v - p.min) / p.interval) * p.interval;
					v = std::clamp(v, p.min, p.max);
					if (v != before) { QueueSlider(a_mod, a_slot, v); }
				}
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				ImGui::SameLine();
				ImGui::TextUnformatted(label.c_str());
				break;
			}
			case kMenu:
			{
				const std::string current = Tr(o.str);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				const bool open = theme::BeginComboTight("##menu", current.c_str());
				HelpTooltip(a_mod, a_s, a_slot);
				if (open)
				{
					if (g_menuAsked != a_slot)
					{
						g_menuAsked = a_slot;
						QueueMenuList(a_mod, a_slot);
					}
					if (a_s.menuSlot == a_slot && a_s.menuReady)
					{
						for (int i = 0; i < static_cast<int>(a_s.menuOptions.size()); ++i)
						{
							const std::string text = Tr(a_s.menuOptions[i]) + "##" + std::to_string(i);
							if (ImGui::Selectable(text.c_str(), i == a_s.menuStart)) { QueueMenu(a_mod, a_slot, i); }
						}
						if (a_s.menuOptions.empty()) { ImGui::TextDisabled("-"); }
					}
					else
					{
						ImGui::TextDisabled("%s", TR("AMF_McmScriptLoading", "Loading..."));
					}
					ImGui::EndCombo();
				}
				else if (g_menuAsked == a_slot)
				{
					g_menuAsked = -1;  // closed: the next opening asks again (a list can change with the mod's state)
				}
				DefaultMenu(a_mod, a_slot);
				ImGui::SameLine();
				ImGui::TextUnformatted(label.c_str());
				break;
			}
			case kColor:
			{
				const auto rgb = static_cast<std::uint32_t>(static_cast<std::int64_t>(o.num)) & 0xFFFFFF;
				float col[3]{ ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
				if (ImGui::ColorEdit3(label.empty() ? "##color" : label.c_str(), col, ImGuiColorEditFlags_NoInputs))
				{
					const auto c = [](float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
					QueueColor(a_mod, a_slot, static_cast<int>((c(col[0]) << 16) | (c(col[1]) << 8) | c(col[2])));
				}
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				break;
			}
			case kKeymap:
			{
				const int code = static_cast<int>(o.num);
				const bool waiting = g_capturing && *g_capturing == a_slot;
				const std::string button = (waiting ? std::string("...") : KeyName(code)) + "##key";
				if (ImGui::Button(button.c_str()) && !waiting)
				{
					g_capturing = a_slot;
					input::ArmKeyCapture();
				}
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				if (o.flags & kFlagWithUnmap)
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("x##unmap")) { QueueKey(a_mod, a_slot, -1); }  // SkyUI's unmap sends -1
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(label.c_str());
				if (waiting && !input::IsKeyCaptureArmed())
				{
					// (device << 32) | code - 0 keyboard (DirectInput scan code), 1 mouse
					const std::int64_t packed = input::LastCapturedKey();
					g_capturing.reset();
					if (packed >= 0)
					{
						const auto device = static_cast<std::int32_t>(packed >> 32);
						const auto key = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
						if (device == 0 && key != 0x01) { QueueKey(a_mod, a_slot, key); }
						else if (device == 1) { QueueKey(a_mod, a_slot, 256 + key); }
						else { logger::info("MCM scripts: keymap {} - Escape or a controller button; keymaps take keyboard and mouse for now", a_slot); }
					}
				}
				break;
			}
			case kInput:
			{
				char buffer[512]{};
				strncpy_s(buffer, o.str.c_str(), _TRUNCATE);
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
				ImGui::InputText("##input", buffer, sizeof(buffer));
				keyboard::NoteTextField(ImGui::GetItemID());
				if (ImGui::IsItemDeactivatedAfterEdit() && o.str != buffer) { QueueInput(a_mod, a_slot, buffer); }
				HelpTooltip(a_mod, a_s, a_slot);
				DefaultMenu(a_mod, a_slot);
				ImGui::SameLine();
				ImGui::TextUnformatted(label.c_str());
				break;
			}
			default:
				ImGui::TextDisabled(TR("AMF_McmUnknownOption", "%s (SkyUI option type %d)"), label.c_str(), o.type);
				break;
			}
			if (disabled) { ImGui::EndDisabled(); }
			ImGui::PopID();
		}

		void DrawQuestion()
		{
			Message m;
			{
				std::scoped_lock lock(g_captureMutex);
				m = g_message;
			}
			if (!m.pending) { return; }
			if (!ImGui::IsPopupOpen("##skyuiquestion")) { ImGui::OpenPopup("##skyuiquestion"); }
			if (ImGui::BeginPopupModal("##skyuiquestion", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
			{
				ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
				ImGui::TextUnformatted(Tr(m.text).c_str());
				ImGui::PopTextWrapPos();
				ImGui::Spacing();
				if (ImGui::Button(Tr(m.accept.empty() ? std::string("$Accept") : m.accept).c_str()))
				{
					Answer(true);
					ImGui::CloseCurrentPopup();
				}
				if (!m.cancel.empty())
				{
					ImGui::SameLine();
					if (ImGui::Button(Tr(m.cancel).c_str()))
					{
						Answer(false);
						ImGui::CloseCurrentPopup();
					}
				}
				ImGui::EndPopup();
			}
		}

		void DrawTab(int a_mod, const std::string& a_rawPage)
		{
			if (g_open.load() != a_mod)
			{
				Open(a_mod, false);
				g_requestedPage = -100;
				g_menuAsked = -1;
				g_capturing.reset();
			}
			g_lastDrawFrame = ImGui::GetFrameCount();

			int page = -3;  // -3: no longer offered
			std::string plugin;
			{
				std::scoped_lock lock(g_mutex);
				SMod& mod = *g_mods[a_mod];
				// Follows a language change (2.1.1), read again on the drawing thread under the lock; see McmLoader.
				if (const std::string lang = TextLanguage(); !mod.plugin.empty() && mod.translationsLanguage != lang)
				{
					mod.translations = LoadTranslations(mod.plugin);
					mod.translationsLanguage = lang;
				}
				g_drawTable = &mod.translations;
				plugin = mod.plugin;
				if (mod.pages.empty() && a_rawPage.empty()) { page = -1; }
				for (std::size_t i = 0; i < mod.pages.size(); ++i)
				{
					if (mod.pages[i] == a_rawPage) { page = static_cast<int>(i); }
				}
			}
			ImGui::TextDisabled(TR("AMF_McmScriptNote", "Drawn from %s's own SkyUI script. Changes go to that script, as in SkyUI's menu."),
				plugin.c_str());
			if (page == -3)
			{
				ImGui::TextDisabled("%s", TR("AMF_McmScriptEmpty", "Nothing on this page."));
				return;
			}

			Session s;
			{
				std::scoped_lock lock(g_mutex);
				s = g_session;
			}
			if (s.page != page && (g_requestedPage != page || g_requestedFor != a_mod))
			{
				g_requestedPage = page;
				g_requestedFor = a_mod;
				QueuePage(a_mod, page, a_rawPage);
			}
			if (s.page != page)
			{
				ImGui::TextDisabled("%s", TR("AMF_McmScriptLoading", "Loading..."));
				DrawQuestion();
				return;
			}

			{
				bool busy = false;
				{
					std::scoped_lock lock(g_queueMutex);
					busy = g_busy && Clock::now() - g_busySince > std::chrono::milliseconds(1500);
				}
				if (busy) { ImGui::TextDisabled("%s", TR("AMF_McmScriptWaiting", "Waiting for the mod's script...")); }
			}

			int last = -1;
			for (int i = 0; i < kSlots; ++i)
			{
				if (s.options[i].type != kEmpty) { last = i; }
			}
			if (last < 0)
			{
				ImGui::TextDisabled("%s", TR("AMF_McmScriptEmpty", "Nothing on this page."));
			}
			else if (ImGui::BeginTable("##skyuipage", 2, ImGuiTableFlags_SizingStretchSame))
			{
				for (int row = 0; row <= last / 2; ++row)
				{
					ImGui::TableNextRow();
					for (int col = 0; col < 2; ++col)
					{
						ImGui::TableNextColumn();
						DrawOption(a_mod, s, row * 2 + col);
					}
				}
				ImGui::EndTable();
			}
			DrawQuestion();
		}

		// ------------------------------------------------------------------------------------------- discovery

		std::atomic<unsigned> g_loadGeneration{ 0 };

		void SetTabsVisible(SMod& a_mod, bool a_on)
		{
			for (const auto& [raw, tab] : a_mod.tabs) { registry::SetPageVisible(a_mod.entryName.c_str(), tab.c_str(), a_on); }
		}

		// Main thread, g_mutex held: register a tab for every page the script offers now, show them, hide the rest.
		void SyncTabs(std::size_t a_index)
		{
			SMod& mod = *g_mods[a_index];
			std::vector<std::string> wanted = mod.pages;
			if (wanted.empty()) { wanted.push_back(""); }  // no Pages: everything is on the "" page
			std::set<std::string> used;
			for (const auto& [raw, tab] : mod.tabs) { used.insert(tab); }
			for (const auto& raw : wanted)
			{
				if (mod.tabs.contains(raw)) { continue; }
				std::string name = raw.empty() ? std::string(TR("AMF_McmSettingsTab", "Settings")) : Tr(&mod.translations, raw);
				if (name.empty()) { name = TR("AMF_McmSettingsTab", "Settings"); }
				const std::string base = name;
				for (int n = 2; used.contains(name); ++n) { name = base + " (" + std::to_string(n) + ")"; }
				used.insert(name);
				const int index = static_cast<int>(a_index);
				if (registry::RegisterFn(mod.entryName.c_str(), name.c_str(), [index, raw]() { DrawTab(index, raw); }))
				{
					mod.tabs.emplace(raw, name);
				}
			}
			for (const auto& [raw, tab] : mod.tabs)
			{
				const bool show = settings::Get().loadSkyUIScriptMenus && detail::IsImported("script|" + mod.plugin + "|" + mod.modName) &&
				                  std::find(wanted.begin(), wanted.end(), raw) != wanted.end();
				registry::SetPageVisible(mod.entryName.c_str(), tab.c_str(), show);
			}
		}

		void DiscoverNow()
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			const auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!vm || !dataHandler || !policy)
			{
				logger::debug("MCM scripts: no VM / data handler yet - discovery waits for the next pass");
				return;
			}
			if (g_skyuiTable.empty()) { g_skyuiTable = LoadTranslations("SkyUI_SE"); }
			if (settings::Get().loadSkyUIScriptMenus) { InstallRecorder(); }

			int found = 0;
			int added = 0;
			int unregistered = 0;
			for (const auto quest : dataHandler->GetFormArray<RE::TESQuest>())
			{
				if (!quest) { continue; }
				const auto handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
				if (!handle) { continue; }
				ObjectPtr config;
				if (!vm->FindBoundObject(handle, "SKI_ConfigBase", config) || !config) { continue; }
				if (IsType(config, "MCM_ConfigBase")) { continue; }  // MCM Helper's - phases 1-2 draw those from config.json
				// SkyUI registered it (_configManager set) - OR its script has run its own set-up (_initialized: OnGameReload ran
				// OnConfigInit, so ModName and Pages are filled) and SkyUI simply has not, or will not, take it in: a big list
				// whose configs register minutes after a new game, past SkyUI's 128 limit (RegisterMod returns -1 and leaves
				// _configManager None), or a manager replaced by MCM Unlocked / a Barzing layout. SKI_ConfigBase uses
				// _configManager only to remember that it registered (SkyUI 5.2's source: set in OnConfigManagerReady, cleared by
				// SKICP_configManagerReset, never called), so AMF drives such a config the same way (Soulsthat, 2026-10-04: CBBE 3BA,
				// moreHUD, T.N.G., Wyrmstooth ... missing from AMF in his list).
				const auto manager = Var(config, "_configManager");
				// the VALUE, not the type: "_configManager = none" leaves an object-typed None, which IsNoneObject (a None
				// TYPE) does not report - tested 2026-10-04 with TestBench papyrus var after SkyUI's own reset event
				const bool registered = manager && manager->IsObject() && manager->GetObject();
				const auto initVar = Var(config, "_initialized");
				const bool initialized = initVar && initVar->IsBool() && initVar->GetBool();
				if (!registered && !initialized) { continue; }  // not set up yet - a later pass looks again
				const auto nameVar = config->GetProperty("ModName");
				const std::string modName = nameVar && nameVar->IsString() ? std::string(nameVar->GetString()) : std::string();
				if (modName.empty()) { continue; }
				const auto pages = ReadStringArray(config->GetProperty("Pages"));
				++found;
				if (!registered) { ++unregistered; }

				std::scoped_lock lock(g_mutex);
				std::size_t index = g_mods.size();
				for (std::size_t i = 0; i < g_mods.size(); ++i)
				{
					if (g_mods[i]->questId == quest->GetFormID()) { index = i; }
				}
				if (index == g_mods.size())
				{
					auto mod = std::make_unique<SMod>();
					mod->questId = quest->GetFormID();
					const auto file = quest->GetFile(0);
					mod->plugin = file ? std::filesystem::path(std::string(file->GetFilename())).stem().string() : std::string();
					mod->modName = modName;
					mod->translations = LoadTranslations(mod->plugin);
					mod->translationsLanguage = TextLanguage();
					mod->entryName = Tr(&mod->translations, modName);
					if (mod->entryName.empty()) { mod->entryName = mod->plugin; }
					mod->entryName += " (MCM)";
					g_mods.push_back(std::move(mod));
					++added;
					logger::info("MCM scripts: {} - quest {:08X} ({}), script {}, {} page(s){}", g_mods.back()->entryName, quest->GetFormID(),
						g_mods.back()->plugin, config->GetTypeInfo() ? config->GetTypeInfo()->GetName() : "?", pages.size(),
						registered ? "" : " - not registered with SkyUI's manager; AMF drives it directly");
				}
				SMod& mod = *g_mods[index];
				mod.script = config;
				mod.present = true;
				mod.registered = registered;
				mod.pages = pages;
				SyncTabs(index);
			}
			logger::info("MCM scripts: discovery - {} script-only SkyUI menu(s) in this game, {} new, {} not registered with SkyUI's manager",
				found, added, unregistered);
		}

		// After a load the configs register with SkyUI over the first half-minute: look a few times.
		void ScheduleDiscovery()
		{
			const unsigned generation = ++g_loadGeneration;
			std::thread([generation]() {
				// passes at 2, 5, 10, 20 and 40 s, then 1, 2, 4 and 8 minutes: on a big list a new game's configs set themselves up
				// for minutes (Soulsthat's, 2026-10-04); the AMF menu opening looks again too (RequestDiscovery)
				for (const int gap : { 2, 3, 5, 10, 20, 20, 60, 120, 240 })
				{
					std::this_thread::sleep_for(std::chrono::seconds(gap));
					if (g_loadGeneration.load() != generation || !settings::Get().loadSkyUIScriptMenus) { return; }
					if (const auto tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { Discover(); }); }
				}
			}).detach();
		}

		std::atomic<bool> g_watchdogRunning{ false };

		void StartWatchdog()
		{
			if (g_watchdogRunning.exchange(true)) { return; }
			std::thread([]() {
				for (;;)
				{
					std::this_thread::sleep_for(std::chrono::seconds(3));
					Watchdog();
				}
			}).detach();
		}

		int FindMod(const std::string& a_name)
		{
			std::scoped_lock lock(g_mutex);
			const std::string want = Lower(a_name);
			for (std::size_t i = 0; i < g_mods.size(); ++i)
			{
				const SMod& m = *g_mods[i];
				if (Lower(m.entryName) == want || Lower(m.plugin) == want || Lower(m.modName) == want) { return static_cast<int>(i); }
			}
			return -1;
		}
	}

	void OnGameLoaded()
	{
		g_open.store(-1);
		ClearQueue();
		{
			std::scoped_lock lock(g_captureMutex);
			g_message = Message{};
		}
		{
			std::scoped_lock lock(g_mutex);
			g_session = Session{};
			for (auto& mod : g_mods)
			{
				mod->script.reset();
				mod->present = false;
				mod->hidden = false;
				SetTabsVisible(*mod, false);  // shown again as discovery finds each one in this game
			}
		}
		if (settings::Get().loadSkyUIScriptMenus)
		{
			StartWatchdog();
			ScheduleDiscovery();
		}
	}

	void RequestDiscovery()
	{
		// any thread (the renderer, as the menu opens): at most one queued pass every 2 s
		static std::atomic<long long> last{ 0 };
		const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
		if (now - last.load() < 2000 || !settings::Get().loadSkyUIScriptMenus) { return; }
		last = now;
		logger::info("MCM scripts: the menu opened - one more discovery pass queued");
		if (const auto tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { Discover(); }); }
	}

	void Discover()
	{
		// Runs with the switch off too: the configs it finds are what the SkyUI-list give-back returns. Off, no tab shows
		// and the recorder stays out.
		try { DiscoverNow(); }
		catch (const std::exception& e) { logger::error("MCM scripts: discovery failed - {}", e.what()); }
	}

	void Frame()
	{
		const int open = g_open.load();
		if (open >= 0 && !g_openedByTool.load() && g_lastDrawFrame < ImGui::GetFrameCount() - 1)
		{
			Close();  // the entry stopped being drawn (another entry chosen, or the menu closed): SkyUI's OnMenuClose
			g_requestedPage = -100;
			g_menuAsked = -1;
			g_capturing.reset();
		}
	}

	void SetEnabled(bool a_on)
	{
		logger::info("MCM scripts: switched {} on the settings page", a_on ? "on" : "off");
		if (a_on)
		{
			StartWatchdog();
			if (const auto tasks = SKSE::GetTaskInterface()) { tasks->AddTask([]() { Discover(); }); }
			return;
		}
		Close();
		if (const auto tasks = SKSE::GetTaskInterface())
		{
			tasks->AddTask([]() {
				RemoveRecorder();
				std::scoped_lock lock(g_mutex);
				for (auto& mod : g_mods) { SetTabsVisible(*mod, false); }
			});
		}
	}

	std::vector<MenuInfo> Menus()
	{
		std::vector<MenuInfo> out;
		std::scoped_lock lock(g_mutex);
		for (const auto& m : g_mods) { out.push_back({ "script|" + m->plugin + "|" + m->modName, m->entryName, m->present }); }
		return out;
	}

	void RefreshVisibility()
	{
		std::scoped_lock lock(g_mutex);
		for (std::size_t i = 0; i < g_mods.size(); ++i)
		{
			if (g_mods[i]->present) { SyncTabs(i); }
			else { SetTabsVisible(*g_mods[i], false); }
		}
	}

	std::vector<HideTarget> HideTargets()
	{
		std::vector<HideTarget> out;  // with the switch off too: a menu AMF hid must still be given back
		std::scoped_lock lock(g_mutex);
		for (std::size_t i = 0; i < g_mods.size(); ++i)
		{
			if (g_mods[i]->present && g_mods[i]->script)
			{
				out.push_back({ g_mods[i]->script, g_mods[i]->modName, i, "script|" + g_mods[i]->plugin + "|" + g_mods[i]->modName });
			}
		}
		return out;
	}

	void SetHidden(std::size_t a_index, bool a_hidden)
	{
		std::scoped_lock lock(g_mutex);
		if (a_index < g_mods.size()) { g_mods[a_index]->hidden = a_hidden; }
	}

	int Hidden()
	{
		std::scoped_lock lock(g_mutex);
		return static_cast<int>(std::count_if(g_mods.begin(), g_mods.end(), [](const auto& m) { return m->hidden; }));
	}

	int Count()
	{
		if (!settings::Get().loadSkyUIScriptMenus) { return 0; }
		std::scoped_lock lock(g_mutex);
		return static_cast<int>(std::count_if(g_mods.begin(), g_mods.end(), [](const auto& m) { return m->present; }));
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		json args;
		try { args = json::parse(a_argsJson.empty() ? "{}" : a_argsJson); }
		catch (...) { return R"({"ok":false,"error":"arguments are not JSON"})"; }
		auto str = [&](const char* k) { const auto it = args.find(k); return it != args.end() && it->is_string() ? it->get<std::string>() : std::string(); };
		auto num = [&](const char* k, double d) { const auto it = args.find(k); return it != args.end() && it->is_number() ? it->get<double>() : d; };
		const std::string action = str("action").empty() ? std::string("list") : str("action");

		if (action == "list")
		{
			json out{ { "ok", true }, { "enabled", settings::Get().loadSkyUIScriptMenus }, { "open", g_open.load() }, { "recorder", g_captureState } };
			{
				std::scoped_lock lock(g_queueMutex);
				out["busy"] = g_busy ? g_busyWith : std::string();
				out["queued"] = g_queue.size();
			}
			{
				std::scoped_lock lock(g_captureMutex);
				out["question"] = g_message.pending ? g_message.text : std::string();
			}
			std::scoped_lock lock(g_mutex);
			out["mods"] = json::array();
			for (std::size_t i = 0; i < g_mods.size(); ++i)
			{
				const SMod& m = *g_mods[i];
				out["mods"].push_back({ { "index", i }, { "entry", m.entryName }, { "plugin", m.plugin }, { "modName", m.modName },
					{ "quest", std::format("{:08X}", m.questId) }, { "pages", m.pages }, { "present", m.present }, { "registered", m.registered }, { "hiddenInSkyUI", m.hidden },
					{ "script", m.script && m.script->GetTypeInfo() ? m.script->GetTypeInfo()->GetName() : "" } });
			}
			return out.dump();
		}
		if (action == "answer")
		{
			const auto yes = args.find("yes");
			Answer(yes != args.end() && yes->is_boolean() && yes->get<bool>());
			return R"({"ok":true})";
		}
		if (action == "close")
		{
			Close();
			g_openedByTool.store(false);
			return R"({"ok":true,"queued":"CloseConfig"})";
		}
		if (action == "options")
		{
			Session s;
			{
				std::scoped_lock lock(g_mutex);
				s = g_session;
			}
			const Table* table = nullptr;
			std::unique_lock lock(g_mutex);
			if (s.mod >= 0 && s.mod < static_cast<int>(g_mods.size())) { table = &g_mods[s.mod]->translations; }
			json list = json::array();
			for (int i = 0; i < kSlots; ++i)
			{
				const Option& o = s.options[i];
				if (o.type == kEmpty) { continue; }
				json row{ { "slot", i }, { "type", TypeName(o.type) }, { "flags", o.flags }, { "text", o.text }, { "shown", Tr(table, o.text) },
					{ "str", o.str }, { "num", o.num } };
				if (const auto it = s.sliders.find(i); it != s.sliders.end())
				{
					row["range"] = { it->second.min, it->second.max, it->second.interval, it->second.def };
				}
				if (const auto it = s.info.find(i); it != s.info.end()) { row["info"] = it->second; }
				list.push_back(std::move(row));
			}
			json out{ { "ok", true }, { "mod", s.mod }, { "page", s.page }, { "pageName", s.pageName }, { "generation", s.generation }, { "options", list } };
			if (s.menuSlot >= 0) { out["menu"] = { { "slot", s.menuSlot }, { "ready", s.menuReady }, { "start", s.menuStart }, { "options", s.menuOptions } }; }
			return out.dump();
		}

		if (action == "open")
		{
			const int mod = FindMod(str("mod"));
			if (mod < 0) { return json{ { "ok", false }, { "error", "no SkyUI script menu '" + str("mod") + "' (action list names them)" } }.dump(); }
			Open(mod, true);
			g_openedByTool.store(true);
			// the page: by name, or by index; the first one by default
			std::string name = str("page");
			int page = -1;
			{
				std::scoped_lock lock(g_mutex);
				const auto& pages = g_mods[mod]->pages;
				if (!pages.empty())
				{
					page = 0;
					if (!name.empty())
					{
						const auto it = std::find(pages.begin(), pages.end(), name);
						if (it == pages.end()) { return json{ { "ok", false }, { "error", "no page '" + name + "'" }, { "pages", pages } }.dump(); }
						page = static_cast<int>(it - pages.begin());
					}
					else if (const int n = static_cast<int>(num("pageIndex", 0)); n >= 0 && n < static_cast<int>(pages.size()))
					{
						page = n;
					}
					name = pages[page];
				}
				else
				{
					name.clear();
				}
			}
			QueuePage(mod, page, name);
			return json{ { "ok", true }, { "queued", "OpenConfig, SetPage" }, { "page", page }, { "pageName", name } }.dump();
		}

		const int open = g_open.load();
		if (open < 0) { return R"({"ok":false,"error":"no SkyUI script menu is open - action open first"})"; }
		const int slot = static_cast<int>(num("slot", -1));
		if (action != "page" && (slot < 0 || slot >= kSlots)) { return R"({"ok":false,"error":"slot (0-127) needed"})"; }
		if (action == "page")
		{
			const std::string name = str("page");
			int page = -1;
			{
				std::scoped_lock lock(g_mutex);
				const auto& pages = g_mods[open]->pages;
				const auto it = std::find(pages.begin(), pages.end(), name);
				if (it == pages.end() && !pages.empty()) { return json{ { "ok", false }, { "error", "no page '" + name + "'" }, { "pages", pages } }.dump(); }
				page = it == pages.end() ? -1 : static_cast<int>(it - pages.begin());
			}
			QueuePage(open, page, name);
			return json{ { "ok", true }, { "queued", "SetPage " + name } }.dump();
		}
		if (action == "select") { QueueSelect(open, slot); }
		else if (action == "default") { QueueDefault(open, slot); }
		else if (action == "slider") { QueueSlider(open, slot, static_cast<float>(num("value", 0.0))); }
		else if (action == "menu") { QueueMenu(open, slot, static_cast<int>(num("index", 0))); }
		else if (action == "menuoptions") { QueueMenuList(open, slot); }
		else if (action == "color") { QueueColor(open, slot, static_cast<int>(num("value", 0))); }
		else if (action == "key") { QueueKey(open, slot, static_cast<int>(num("code", -1))); }
		else if (action == "input") { QueueInput(open, slot, str("text")); }
		else if (action == "info") { QueueInfo(open, slot); }
		else
		{
			return json{ { "ok", false }, { "error", "unknown action '" + action +
				"' (list, open, page, options, select, slider, menu, menuoptions, color, key, input, default, info, answer, close)" } }.dump();
		}
		return json{ { "ok", true }, { "queued", action }, { "slot", slot } }.dump();
	}
}

namespace mcmloader::detail
{
	RE::BSScript::Variable* ScriptVar(const RE::BSTSmartPointer<RE::BSScript::Object>& a_object, std::string_view a_name)
	{
		return ::mcmloader::scripts::Var(a_object, a_name);
	}
}
