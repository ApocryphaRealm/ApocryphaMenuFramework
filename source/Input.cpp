#include "Input.h"
#include "Keyboard.h"

#include <chrono>
#include <cmath>

#include "Bindings.h"
#include "Compat.h"
#include "Offsets.h"
#include "Renderer.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>   // GImGui: the active item is asked for directly, so a text field can keep the D-pad

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace input
{
	namespace
	{
		// -----------------------------------------------------------------------------------
		// The record the input thread hands to the render thread. Everything ImGui needs,
		// nothing that dereferences game memory later - the InputEvent list is dead the moment
		// the hook returns, so records copy by value.
		// -----------------------------------------------------------------------------------
		struct Record
		{
			enum class Kind : std::uint8_t
			{
				kMouseMove,
				kMouseButton,
				kMouseWheel,
				kKeyboard,
				kGamepad,
				kThumbstick,  // left stick, for menu nav in controller mode (x,y in [-1,1])
				kCharacter,
				kCursorSet,   // absolute placement from a driver (DevBench)
			};

			Kind kind{};
			std::uint32_t code = 0;   // idCode: mouse button index / DIK scancode / XInput mask / unicode
			bool down = false;        // press (true) or release (false) transitions only
			float x = 0.0f;           // mouse deltas / wheel direction
			float y = 0.0f;
		};

		std::mutex g_queueLock;
		std::vector<Record> g_queue;

		// Armed by BeginRebindToggleKey(); the next keyboard press in the hook becomes the toggle
		// key (Escape cancels). Atomic - set on the render thread, consumed on the input thread.
		std::atomic<bool> g_awaitingRebind{ false };

		// Observe-only keybind capture (amf.keybind, L26): armed over DevBench, records the next
		// keyboard/gamepad press without consuming it, then disarms. -1 = nothing captured yet.
		// AUTO INPUT MODE (the author, 2026-09-01). What the player last really used, and when.
		// Only DELIBERATE input counts: a key or gamepad button going down, a mouse button, real
		// mouse movement, or a stick pushed past the navigation deadzone. Idle noise - a resting
		// stick, a nudged mouse - must never flip the mode, which is the whole reason the framework
		// refused to auto-detect before this was asked for.
		std::atomic<Device> g_lastDevice{ Device::kUnknown };
		std::atomic<std::chrono::steady_clock::time_point> g_lastDeviceAt{ std::chrono::steady_clock::time_point{} };
		constexpr float kMouseMoveThreshold = 2.0f;   // pixels in one event
		constexpr float kStickThreshold = 0.35f;      // same as the nav deadzone

		void NoteDevice(Device a_device)
		{
			const Device was = g_lastDevice.exchange(a_device, std::memory_order_relaxed);
			g_lastDeviceAt.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
			if (was == a_device) { return; }

			const bool wantsController = (a_device == Device::kGamepad);
			logger::info("input: {} used -> {} navigation",
						 wantsController ? "controller" : "keyboard/mouse",
						 wantsController ? "controller" : "keyboard");
		}

		// The sticks as they really are, kept so a consumer can read them apart (1.9.5). The
		// translation below collapses them onto one set of nav axes; these are the raw values.
		std::atomic<float> g_stickX[2]{};
		std::atomic<float> g_stickY[2]{};
		std::atomic<bool>  g_stickClicked[2]{};
		std::atomic<bool>  g_sticksCaptured{ false };

		// B pressed while a text field held the keyboard. The key itself is kept from ImGui (it
		// would revert the text), so the renderer cannot ask ImGui whether it happened - it asks
		// here instead, and the flag is consumed on read.
		std::atomic<bool>  g_textFieldCancel{ false };

		// Set by the renderer each frame: an item is being edited, so the right stick drives it.
		std::atomic<bool> g_itemActive{ false };

		std::atomic<bool> g_captureArmed{ false };
		std::atomic<std::int64_t> g_lastCaptured{ -1 };

		// XInput Start button mask (RE::BSWin32GamepadDevice::kStart) - closes the menu in
		// controller mode, since there is otherwise no gamepad way out (design decision, 2026-08-28).
		constexpr std::uint32_t kGamepadStart = 0x0010;
		constexpr std::uint32_t kDIKEscape = 0x01;

		// Buttons the GAME currently believes are held - maintained on the input thread only.
		// While the menu is open, a release passes through ONLY if its press reached the game
		// before the menu opened. Passing every release (the 1.1.2 behavior) let release-
		// triggered actions fire: Skyrim's shout activates on button RELEASE, so a shout
		// button pressed INSIDE the menu was consumed on the down-edge but completed as a
		// shout on the up-edge (the author's report). Keyed device<<32|idCode.
		std::unordered_set<std::uint64_t> g_gameHeldButtons;

		// ---- the engine's own text entry --------------------------------------------------
		// Skyrim's keyboard device only turns the WM_CHAR queue into RE::CharEvent while
		// ControlMap's text-entry count is above zero (ControlMap::AllowTextInput raises and lowers
		// it). This framework has no WndProc hook, so a CharEvent is the ONLY way a typed letter
		// ever reaches ImGui - which is why clicking the mod search bar and typing did nothing at
		// all, while clicking, navigation and the on-screen keyboard (which calls
		// io.AddInputCharacter directly) all worked (phbd01, 2026-09-19: "when I click on the
		// search bar and try to type, nothing happens").
		//
		// Held exactly as long as an ImGui text field wants the keyboard, and raised/lowered here
		// on the GAME thread, where PollInputDevices runs. The count is a counter, so every raise
		// is matched by exactly one lower; closing the menu drops WantTextInput and releases it.
		bool g_textInputHeld = false;
		int  g_textInputAdded = 0;   // how many raises we made, so exactly that many are undone
		bool g_textInputDropLogged = false;   // one warning per hold, not one per frame

		void SyncEngineTextInput()
		{
			const bool want = renderer::IsMainWindowVisible() && renderer::WantsTextInput();
			auto* controls = RE::ControlMap::GetSingleton();
			if (!controls) { return; }
			// HELD, AND SOMETHING TOOK IT BACK (phbd01, 2026-09-21: "it is fixed at first now, but it
			// returns after some time - I can click but not type, I have to press escape again").
			// Raising the count once, when the field takes focus, is not enough: while the field is
			// still focused another mod (or the engine closing some other menu) can lower the count
			// back to zero, and from then on no CharEvent is made - typing dies with the box still
			// focused, and only Escape (focus lost, then taken again, which re-runs the raise below)
			// brings it back. So while we hold it, the count is checked every frame and topped up
			// again, and the first drop in each hold is logged with the number.
			if (want && g_textInputHeld)
			{
				auto& rdHeld = controls->GetRuntimeData();
				if (rdHeld.textEntryCount <= 0)
				{
					const int before = rdHeld.textEntryCount;
					int added = 0;
					while (rdHeld.textEntryCount <= 0 && added < 8 && g_textInputAdded < 64)
					{
						controls->AllowTextInput(true);
						++added;
						++g_textInputAdded;
					}
					if (!g_textInputDropLogged)
					{
						g_textInputDropLogged = true;
						logger::warn("input: ControlMap's text-entry count fell to {} while a text field had focus - "
									 "another mod released text input it did not take. Raised it {} time(s) to {} "
									 "so typing keeps working.",
									 before, added, static_cast<int>(rdHeld.textEntryCount));
					}
				}
				return;
			}
			if (want == g_textInputHeld) { return; }
			// THE COUNT, NOT THE CALL (2026-09-19). AllowTextInput moves ControlMap's textEntryCount,
			// and the engine only makes CharEvents while that count is ABOVE ZERO. A single +1 is
			// therefore not enough if another mod has driven the count NEGATIVE - it has called
			// AllowTextInput(false) more often than true, which nothing stops it doing - and from a
			// player's side that is indistinguishable from the fault this code was written to fix:
			// clicking the box does nothing, and some unrelated action that happens to reset the
			// count (opening a menu, pressing Escape) makes typing start working. phbd01 reported
			// exactly that shape again on 2026-09-19, after 1.9.5 shipped the single call.
			//
			// So the count is READ, and raised until it is actually positive - bounded, and we
			// remember how many we added so exactly that many come back off. If it cannot be made
			// positive the log says so with the number, which names the fault instead of leaving it
			// looking like ours.
			auto& rd = controls->GetRuntimeData();
			if (want)
			{
				const int before = rd.textEntryCount;
				int added = 0;
				while (rd.textEntryCount <= 0 && added < 8)
				{
					controls->AllowTextInput(true);
					++added;
				}
				if (added == 0) { controls->AllowTextInput(true); added = 1; }   // already positive: one, balanced
				g_textInputAdded = added;
				if (before < 0)
				{
					logger::warn("input: ControlMap's text-entry count was {} - another mod has released text "
								 "input more often than it took it. Raised {} time(s) to reach {}. If this is "
								 "still not positive, typing cannot work until that mod is found.",
								 before, added, static_cast<int>(rd.textEntryCount));
				}
				else
				{
					logger::debug("input: engine text entry raised ({} -> {}, {} call(s))",
								  before, static_cast<int>(rd.textEntryCount), added);
				}
			}
			else
			{
				// Undo our own raises - but never below zero: if something RESET the count while we
				// held it (rather than lowering it by one), lowering by everything we added would push
				// it negative and break typing for the next mod that asks for it.
				int released = 0;
				while (released < g_textInputAdded && rd.textEntryCount > 0)
				{
					controls->AllowTextInput(false);
					++released;
				}
				logger::debug("input: engine text entry released ({} of {} call(s), count now {})",
							  released, g_textInputAdded, static_cast<int>(rd.textEntryCount));
				g_textInputAdded = 0;
				g_textInputDropLogged = false;
			}
			g_textInputHeld = want;
		}

		std::uint64_t ButtonKey(const RE::ButtonEvent* a_button)
		{
			return (static_cast<std::uint64_t>(a_button->GetDevice()) << 32) | a_button->GetIDCode();
		}

		// Software cursor, owned by the render thread. The game recentres/hides the OS cursor
		// at will, so the only trustworthy position is one we integrate ourselves from the
		// MouseMoveEvent deltas (the Wheeler-lineage approach from the survey).
		float g_cursorX = 0.0f;
		float g_cursorY = 0.0f;
		// Records that must land on a LATER frame (see QueueMouseClick). Drained by the render
		// thread at the top of ProcessQueuedEvents; guarded by the same lock as the main queue.
		struct Deferred { int framesLeft; Record record; };
		std::vector<Deferred> g_deferred;

		std::atomic<float> g_cursorMirrorX{ 0.0f };   // read by DevBench off-thread
		std::atomic<float> g_cursorMirrorY{ 0.0f };

		void Enqueue(const Record& a_record)
		{
			std::scoped_lock lock(g_queueLock);

			// A runaway queue means the render thread stopped draining (e.g. device lost);
			// dropping input is strictly better than growing unbounded on the input thread.
			if (g_queue.size() < 512)
			{
				g_queue.push_back(a_record);
			}
		}

		// -----------------------------------------------------------------------------------
		// DIK scancode -> ImGuiKey. The navigation-and-editing set; full text input is M4.
		// -----------------------------------------------------------------------------------
		// An action, as the ImGui key that performs it. This is the join between the Controls page's
		// bindings and what ImGui's navigation actually reads: whatever the player bound "Move up"
		// to arrives at ImGui as UpArrow/DpadUp, so nav needs no notion of bindings at all.
		// True when an ImGui text field currently holds the keyboard AND this key would move nav.
		// Those keys are dropped rather than fed, so typing survives them; everything else still
		// reaches ImGui, so A, B and the shoulder buttons behave exactly as before.
		bool TextFieldHasTheKeyboard(ImGuiKey a_key)
		{
			switch (a_key)
			{
			case ImGuiKey_GamepadDpadUp:
			case ImGuiKey_GamepadDpadDown:
			case ImGuiKey_GamepadDpadLeft:
			case ImGuiKey_GamepadDpadRight:
			// B is filtered too, and for a different reason. ImGui treats the gamepad cancel as an
			// InputText CANCEL, which RESTORES THE TEXT THE FIELD HAD WHEN EDITING BEGAN - so
			// pressing circle to leave the box threw away what had just been typed and put the
			// previous search back (the owner, 2026-09-19: "it reverted the word that I'd made to the
			// previous word that I'd searched ... It shouldn't revert the word"). Keeping it away
			// from ImGui and ending the edit ourselves (Renderer's B handler calls ClearActiveID)
			// leaves the buffer exactly as typed, which is what "leave the box" should mean.
			case ImGuiKey_GamepadFaceRight:
				break;
			default:
				return false;
			}
			return GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId);
		}

		ImGuiKey ActionToImGuiKey(bindings::Action a_action, bool a_gamepad)
		{
			switch (a_action)
			{
			case bindings::Action::kUp:       return a_gamepad ? ImGuiKey_GamepadDpadUp : ImGuiKey_UpArrow;
			case bindings::Action::kDown:     return a_gamepad ? ImGuiKey_GamepadDpadDown : ImGuiKey_DownArrow;
			case bindings::Action::kLeft:     return a_gamepad ? ImGuiKey_GamepadDpadLeft : ImGuiKey_LeftArrow;
			case bindings::Action::kRight:    return a_gamepad ? ImGuiKey_GamepadDpadRight : ImGuiKey_RightArrow;
			case bindings::Action::kActivate: return a_gamepad ? ImGuiKey_GamepadFaceDown : ImGuiKey_Enter;
			case bindings::Action::kBack:     return a_gamepad ? ImGuiKey_GamepadFaceRight : ImGuiKey_Backspace;
			case bindings::Action::kClose:    return a_gamepad ? ImGuiKey_None : ImGuiKey_Escape;
			default:                          return ImGuiKey_None;
			}
		}

		ImGuiKey ScancodeToImGuiKey(std::uint32_t a_scancode)
		{
			// A REBOUND key wins (1.9.6). The table below stays as the fallback, so every key that
			// was not given a new job keeps the one it always had - a player who rebinds nothing
			// notices no difference.
			if (const auto act = bindings::FromKeyboard(a_scancode); act != bindings::Action::kCount)
			{
				if (const ImGuiKey mapped = ActionToImGuiKey(act, false); mapped != ImGuiKey_None) { return mapped; }
			}
			switch (a_scancode)
			{
			case 0x01: return ImGuiKey_Escape;
			case 0x0F: return ImGuiKey_Tab;
			case 0x1C: return ImGuiKey_Enter;
			case 0x39: return ImGuiKey_Space;
			case 0x0E: return ImGuiKey_Backspace;
			case 0xC8: return ImGuiKey_UpArrow;
			case 0xD0: return ImGuiKey_DownArrow;
			case 0xCB: return ImGuiKey_LeftArrow;
			case 0xCD: return ImGuiKey_RightArrow;
			case 0xC7: return ImGuiKey_Home;
			case 0xCF: return ImGuiKey_End;
			case 0xC9: return ImGuiKey_PageUp;
			case 0xD1: return ImGuiKey_PageDown;
			case 0x2A: return ImGuiKey_LeftShift;
			case 0x36: return ImGuiKey_RightShift;
			case 0x1D: return ImGuiKey_LeftCtrl;
			case 0x9D: return ImGuiKey_RightCtrl;
			case 0x38: return ImGuiKey_LeftAlt;
			case 0xB8: return ImGuiKey_RightAlt;
			case 0xD3: return ImGuiKey_Delete;
			// Letters and digits. A typed character still arrives as a CharEvent - these are the
			// KEY events, which is what ImGui's text field needs for the editing shortcuts
			// (Ctrl+A select all, Ctrl+C/X/V, Ctrl+Z) and what a mod reading ImGui::IsKeyPressed
			// needs to see. Without them Ctrl+A in the search bar did nothing.
			case 0x1E: return ImGuiKey_A;  case 0x30: return ImGuiKey_B;  case 0x2E: return ImGuiKey_C;
			case 0x20: return ImGuiKey_D;  case 0x12: return ImGuiKey_E;  case 0x21: return ImGuiKey_F;
			case 0x22: return ImGuiKey_G;  case 0x23: return ImGuiKey_H;  case 0x17: return ImGuiKey_I;
			case 0x24: return ImGuiKey_J;  case 0x25: return ImGuiKey_K;  case 0x26: return ImGuiKey_L;
			case 0x32: return ImGuiKey_M;  case 0x31: return ImGuiKey_N;  case 0x18: return ImGuiKey_O;
			case 0x19: return ImGuiKey_P;  case 0x10: return ImGuiKey_Q;  case 0x13: return ImGuiKey_R;
			case 0x1F: return ImGuiKey_S;  case 0x14: return ImGuiKey_T;  case 0x16: return ImGuiKey_U;
			case 0x2F: return ImGuiKey_V;  case 0x11: return ImGuiKey_W;  case 0x2D: return ImGuiKey_X;
			case 0x15: return ImGuiKey_Y;  case 0x2C: return ImGuiKey_Z;
			case 0x02: return ImGuiKey_1;  case 0x03: return ImGuiKey_2;  case 0x04: return ImGuiKey_3;
			case 0x05: return ImGuiKey_4;  case 0x06: return ImGuiKey_5;  case 0x07: return ImGuiKey_6;
			case 0x08: return ImGuiKey_7;  case 0x09: return ImGuiKey_8;  case 0x0A: return ImGuiKey_9;
			case 0x0B: return ImGuiKey_0;
			default:   return ImGuiKey_None;
			}
		}

		// -----------------------------------------------------------------------------------
		// XInput button mask (RE::BSWin32GamepadDevice::Key) -> ImGuiKey gamepad navigation.
		// Fed only in controller mode (explicit toggle - never auto-detected; nav-drift rule).
		// -----------------------------------------------------------------------------------
		ImGuiKey GamepadMaskToImGuiKey(std::uint32_t a_mask)
		{
			if (const auto act = bindings::FromGamepad(a_mask); act != bindings::Action::kCount)
			{
				if (const ImGuiKey mapped = ActionToImGuiKey(act, true); mapped != ImGuiKey_None) { return mapped; }
			}
			switch (a_mask)
			{
			case 0x0001: return ImGuiKey_GamepadDpadUp;
			case 0x0002: return ImGuiKey_GamepadDpadDown;
			case 0x0004: return ImGuiKey_GamepadDpadLeft;
			case 0x0008: return ImGuiKey_GamepadDpadRight;
			case 0x1000: return ImGuiKey_GamepadFaceDown;   // A = activate
			case 0x2000: return ImGuiKey_GamepadFaceRight;  // B = cancel
			case 0x4000: return ImGuiKey_GamepadFaceLeft;
			case 0x8000: return ImGuiKey_GamepadFaceUp;
			case 0x0100: return ImGuiKey_GamepadL1;
			case 0x0200: return ImGuiKey_GamepadR1;
			// The stick clicks. They were not mapped at all, so a page could not be given an
			// action on one - which is what "press R3 on the list item" needs (1.9.5).
			case 0x0040: return ImGuiKey_GamepadL3;
			case 0x0080: return ImGuiKey_GamepadR3;
			default:     return ImGuiKey_None;
			}
		}

		// -----------------------------------------------------------------------------------
		// The hook. Decides three things per event, in order:
		//   1. toggle key pressed -> flip the menu, consume the event
		//   2. menu open -> copy the event for ImGui, then pass RELEASES through to the game
		//      (stuck-key prevention) and consume everything else (camera/movement halt)
		//   3. menu closed -> pass everything through untouched
		// -----------------------------------------------------------------------------------
	// ---- driver-side event injection (DevBench) -------------------------------------------
	// Splices REAL engine event nodes (RE::ButtonEvent / RE::CharEvent) at the head of the list
	// inside this plugin's own dispatch hook, BEFORE the hook processes them - so an injected
	// press takes the same path as a hardware one from here on: the consumer input callbacks'
	// first look, the consume rule, the held set, ImGui, and every downstream handler. This is
	// what the `type`/`key` record ops could not exercise (they enter at the record queue, after
	// the first look), and the reported search-box freeze lives in exactly that gap.
	namespace inject
	{
		struct Press { RE::INPUT_DEVICE device; std::uint32_t code; int framesLeft; bool downSent; };
		std::mutex g_lock;
		std::vector<Press> g_presses;
		std::vector<std::uint32_t> g_chars;

		void SpliceButton(RE::InputEvent** a_events, RE::INPUT_DEVICE a_device, std::uint32_t a_code, float a_value, float a_held)
		{
			auto* controlMap = RE::ControlMap::GetSingleton();
			const std::string_view name = controlMap ? controlMap->GetUserEventName(a_code, a_device) : std::string_view{};
			RE::BSFixedString userEvent(name.empty() ? "" : std::string(name).c_str());
			auto* ev = RE::ButtonEvent::Create(a_device, userEvent, a_code, a_value, a_held);
			if (!ev) { return; }
			ev->next = *a_events; *a_events = ev;
		}
		void SpliceChar(RE::InputEvent** a_events, std::uint32_t a_code)
		{
			auto* ev = RE::malloc<RE::CharEvent>(sizeof(RE::CharEvent));
			if (!ev) { return; }
			std::memset(reinterpret_cast<void*>(ev), 0, sizeof(RE::CharEvent));
			RE::stl::emplace_vtable<RE::CharEvent>(ev);
			ev->device = RE::INPUT_DEVICE::kKeyboard;
			ev->eventType = RE::INPUT_EVENT_TYPE::kChar;
			ev->keyCode = a_code;
			ev->next = *a_events; *a_events = ev;
		}
		void Service(RE::InputEvent** a_events)
		{
			std::scoped_lock l(g_lock);
			if (g_presses.empty() && g_chars.empty()) { return; }
			if (!g_chars.empty()) { SpliceChar(a_events, g_chars.front()); g_chars.erase(g_chars.begin()); }  // one character per dispatch
			for (auto it = g_presses.begin(); it != g_presses.end();) {
				if (!it->downSent) { SpliceButton(a_events, it->device, it->code, 1.0f, 0.0f); it->downSent = true; ++it; continue; }
				if (it->framesLeft-- > 0) { SpliceButton(a_events, it->device, it->code, 1.0f, 0.05f * static_cast<float>(it->framesLeft + 1)); ++it; continue; }
				SpliceButton(a_events, it->device, it->code, 0.0f, 0.1f);
				it = g_presses.erase(it);
			}
		}
	}


		struct PollInputDevicesHook
		{
			static inline REL::Relocation<void(RE::BSTEventSource<RE::InputEvent*>*, RE::InputEvent**)> func;

			static void CopyForImGui(const RE::InputEvent* a_event)
			{
				switch (a_event->GetEventType())
				{
				case RE::INPUT_EVENT_TYPE::kMouseMove:
					{
						const auto* move = static_cast<const RE::MouseMoveEvent*>(a_event);
						Enqueue({ Record::Kind::kMouseMove, 0, false,
								  static_cast<float>(move->mouseInputX), static_cast<float>(move->mouseInputY) });
						break;
					}
				case RE::INPUT_EVENT_TYPE::kButton:
					{
						const auto* button = static_cast<const RE::ButtonEvent*>(a_event);

						// Transitions only; ImGui tracks held state itself, and the raw
						// held-repeat frames (value > 0, heldDownSecs > 0) would double-fire.
						const bool isDown = button->IsDown();
						const bool isUp = button->IsUp();
						if (!isDown && !isUp)
						{
							break;
						}

						switch (button->GetDevice())
						{
						case RE::INPUT_DEVICE::kMouse:
							if (button->GetIDCode() == 8 || button->GetIDCode() == 9)
							{
								// The engine delivers the wheel as button 8 (up) / 9 (down).
								if (isDown)
								{
									Enqueue({ Record::Kind::kMouseWheel, 0, false, 0.0f,
											  button->GetIDCode() == 8 ? 1.0f : -1.0f });
								}
							}
							else if (button->GetIDCode() <= 4)
							{
								Enqueue({ Record::Kind::kMouseButton, button->GetIDCode(), isDown, 0.0f, 0.0f });
							}
							break;
						case RE::INPUT_DEVICE::kKeyboard:
							Enqueue({ Record::Kind::kKeyboard, button->GetIDCode(), isDown, 0.0f, 0.0f });
							break;
						case RE::INPUT_DEVICE::kGamepad:
							Enqueue({ Record::Kind::kGamepad, button->GetIDCode(), isDown, 0.0f, 0.0f });
							break;
						default:
							break;
						}
						break;
					}
				case RE::INPUT_EVENT_TYPE::kChar:
					{
						const auto* character = static_cast<const RE::CharEvent*>(a_event);
						Enqueue({ Record::Kind::kCharacter, character->keyCode, true, 0.0f, 0.0f });
						break;
					}
				case RE::INPUT_EVENT_TYPE::kThumbstick:
					{
						// LEFT stick drives ImGui menu nav in controller mode (the author could not
						// switch menus - the D-pad is mapped but he used the stick, which was not
						// captured). Right stick is left to the game (camera). x,y in [-1,1].
						// BOTH sticks are captured while the menu is open (they are consumed anyway,
						// so the camera is already still). Which one drives ImGui is decided on the
						// render thread: left = navigation, right = moving whatever the player has
						// taken hold of with A. code: 0 = left, 1 = right.
						const auto* thumb = static_cast<const RE::ThumbstickEvent*>(a_event);
						Enqueue({ Record::Kind::kThumbstick, thumb->IsLeft() ? 0u : 1u, false,
								  thumb->xValue, thumb->yValue });
						break;
					}
				default:
					break;
				}
			}

			static void thunk(RE::BSTEventSource<RE::InputEvent*>* a_dispatcher, RE::InputEvent** a_events)
			{
				if (!a_events)
				{
					func(a_dispatcher, a_events);
					return;
				}

				SyncEngineTextInput();   // the engine makes no CharEvent unless we ask it to

				// A page that took the sticks gets them taken back the moment the menu is not up,
				// so a mod that forgets to release them cannot leave navigation dead (1.9.5).
				if (!renderer::IsMainWindowVisible() && g_sticksCaptured.load(std::memory_order_acquire))
				{
					SetSticksCaptured(false);
				}

				inject::Service(a_events);   // driver-side presses/characters, ahead of everything below

				const bool menuOpen = renderer::IsMainWindowVisible();
				const auto toggleKey = static_cast<std::uint32_t>(settings::Get().toggleKey);
				const bool controllerMode = UsingController();
				const bool awaitingRebind = g_awaitingRebind.load(std::memory_order_acquire);

				RE::InputEvent* head = *a_events;
				RE::InputEvent* previous = nullptr;
				RE::InputEvent* current = head;

				while (current)
				{
					RE::InputEvent* next = current->next;
					bool passThrough = true;

					const RE::ButtonEvent* button = current->AsButtonEvent();

					// Observe-only keybind capture: record the next keyboard/gamepad PRESS and
					// disarm. Never consumes and never touches settings - purely a witness, so it
					// composes with every branch below (including the rebind, which stays first
					// in precedence for the consuming path).
					if (button && button->IsDown() &&
						(button->GetDevice() == RE::INPUT_DEVICE::kKeyboard ||
							button->GetDevice() == RE::INPUT_DEVICE::kGamepad) &&
						g_captureArmed.load(std::memory_order_acquire))
					{
						const auto packed =
							(static_cast<std::int64_t>(button->GetDevice()) << 32) |
							static_cast<std::int64_t>(button->GetIDCode());
						g_lastCaptured.store(packed, std::memory_order_release);
						g_captureArmed.store(false, std::memory_order_release);
						logger::info("keybind capture: observed device {} code {}",
							static_cast<std::uint32_t>(button->GetDevice()), button->GetIDCode());
					}

					// THE CONTROLS PAGE'S CAPTURE (1.9.6) takes precedence over everything below while it
					// is armed: the press it is waiting for must not also do whatever it is currently
					// bound to. It consumes the event outright, so the game never sees it either.
					if (button && bindings::IsCapturing())
					{
						const auto dev = button->GetDevice();
						bool taken = false;
						if (dev == RE::INPUT_DEVICE::kKeyboard)     { taken = bindings::OfferKeyboard(button->GetIDCode(), button->IsDown()); }
						else if (dev == RE::INPUT_DEVICE::kMouse)   { taken = bindings::OfferMouse(button->GetIDCode(), button->IsDown()); }
						else if (dev == RE::INPUT_DEVICE::kGamepad) { taken = bindings::OfferGamepad(button->GetIDCode(), button->IsDown()); }
						if (taken)
						{
							settings::Save();
							passThrough = false;
							goto consumed;
						}
					}

					if (awaitingRebind && button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
						button->IsDown())
					{
						// Rebind capture takes precedence: the next keyboard key becomes the new
						// menu toggle key (Escape cancels). Consumed so the game never sees it.
						const std::uint32_t code = button->GetIDCode();
						if (code != kDIKEscape)
						{
							settings::Get().toggleKey = static_cast<std::int32_t>(code);
							settings::Save();
							logger::info("menu toggle key rebound to scan code {}", code);
						}
						else
						{
							logger::info("menu toggle key rebind cancelled (Escape)");
						}
						g_awaitingRebind.store(false, std::memory_order_release);
						passThrough = false;
					}
					else if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
						bindings::FromKeyboard(button->GetIDCode()) == bindings::Action::kToggleMenu &&
						button->IsDown() && compat::IsHotkeyEnabled())
					{
						renderer::ToggleMainWindow();
						passThrough = false;  // the game never sees the framework's own key
					}
					else if (menuOpen && controllerMode && button &&
						button->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
						bindings::FromGamepad(button->GetIDCode()) == bindings::Action::kClose &&
						button->IsDown())
					{
						// Gamepad Start CLOSES the menu in controller mode - the way out with a
						// controller (the author: "no way to use the controller to leave the menu"). It
						// only closes, never opens, so the game keeps its own Start/pause button
						// when the menu is down.
						renderer::ToggleMainWindow();
						passThrough = false;
					}
					else if (menuOpen)
					{
						// SMF-compat input callbacks (e.g. DEM's "Press a key..." bind capture)
						// get first look; a callback that consumes the event keeps it from the
						// menu's own widgets as well. The game sees it in neither case.
						if (!compat::DispatchInputEvent(current))
						{
							CopyForImGui(current);
						}
						else if (button && button->IsDown())
						{
							// A consumer's input callback CLAIMED this press while the menu is up, so the
							// menu's own widgets never see it. Logged because from the player's side this is
							// indistinguishable from the menu freezing (report 2026-09-12: the search box
							// stopped taking input) - the log names the device and key so the claiming mod
							// can be found by what it consumes.
							logger::info("input: a consumer input callback claimed device {} code {} while the menu is open - the menu's widgets will not see it",
								static_cast<std::uint32_t>(button->GetDevice()), button->GetIDCode());
						}

						passThrough = false;

						if (button && button->IsUp())
						{
							// Pass the release ONLY if the game saw the press (held across the
							// open transition). A stray release for a key the game never saw
							// down would be a no-op anyway - but shout-style release-triggered
							// actions make an unconditional pass actively dangerous.
							const auto held = g_gameHeldButtons.find(ButtonKey(button));
							if (held != g_gameHeldButtons.end())
							{
								g_gameHeldButtons.erase(held);
								passThrough = true;
							}
						}

						// Everything else is consumed - THIS is what halts the camera, the scroll-zoom and
						// movement while the menu is up. (An unconditional `passThrough = IsUp()` used to sit
						// here and overwrote the held-set decision above, so EVERY release reached the game -
						// the 1.1.2 behaviour the held set was written to end: a shout key pressed inside the
						// menu completed as a shout on its release. Queue row 2026-09-12; fixed 2026-09-12.)
					}
					else
					{
						// MENU CLOSED. Consumer input callbacks still get the event.
						//
						// They used to be dispatched ONLY in the branch above, i.e. only while the
						// framework menu was on screen - so every hotkey registered through
						// RegisterInpoutEvent/AddInputEvent was dead during normal play. Two mods
						// reported it independently (Simple Power Attack and SkyPlace, 2026-09-11):
						// "hotkeys do not work under AMF but work fine on SKSE Menu Framework".
						// Real SMF dispatches these regardless of its own menu state, and a mod that
						// registers an input event and no section - Simple Power Attack resolves
						// RegisterInpoutEvent and neither SetSection nor GetMenuFrameworkVersion -
						// has nowhere else for its key to arrive.
						//
						// The consume rule here is deliberately the OPPOSITE of the open branch. With
						// the menu up everything is swallowed by default; with it down, `passThrough`
						// stays true unless a callback explicitly claims the event by returning true.
						// Anything else would eat ordinary gameplay keys, which is a far worse defect
						// than the one being fixed.
						//
						// Nothing is fed to ImGui here: the menu is not drawing, and the held-button
						// model stays consistent because a consumed press never reaches the block
						// below that records it - the game did not see that press either.
						if (compat::DispatchInputEvent(current))
						{
							passThrough = false;
						}
					}

				consumed:
					if (passThrough && button)
					{
						// The game is about to see this edge - keep its held-state model current.
						if (button->IsDown())
						{
							g_gameHeldButtons.insert(ButtonKey(button));
						}
						else if (button->IsUp())
						{
							g_gameHeldButtons.erase(ButtonKey(button));
						}
					}

					if (!passThrough)
					{
						if (previous)
						{
							previous->next = next;
						}
						else
						{
							head = next;
						}
					}
					else
					{
						previous = current;
					}

					current = next;
				}

				// 1.7.9 (Haron's crash log, 2026-09-13): the list pointer is `RE::InputEvent* const*` -
				// another hook up the chain may hand us a pointer into ITS OWN read-only memory (a
				// `static const` empty list in a DLL's .rdata; the faulting write landed inside
				// AltTabFix.dll's image, thirteen seconds into the game, with seven other input hooks
				// in the chain). So the pruned head is written back only when it actually changed AND
				// the slot is writable; otherwise the game gets the pruned list through our own array
				// and the caller's memory is left alone.
				const auto writeBack = [&](RE::InputEvent* a_head) -> RE::InputEvent** {
					static RE::InputEvent* s_own[] = { nullptr };
					auto* slot = const_cast<RE::InputEvent**>(a_events);
					if (*slot == a_head)
					{
						return slot;
					}
					MEMORY_BASIC_INFORMATION mbi{};
					const bool writable =
						VirtualQuery(slot, &mbi, sizeof(mbi)) == sizeof(mbi) && mbi.State == MEM_COMMIT &&
						(mbi.Protect & (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0 &&
						(mbi.Protect & PAGE_GUARD) == 0;
					if (writable)
					{
						*slot = a_head;
						return slot;
					}
					static bool s_said = false;
					if (!s_said)
					{
						s_said = true;
						logger::warn("input hook: the event-list slot {} is read-only memory (another hook's constant list); "
									 "the pruned list is passed on through our own array and the caller's memory is not written",
									 static_cast<const void*>(slot));
					}
					s_own[0] = a_head;
					return s_own;
				};

				if (head)
				{
					func(a_dispatcher, writeBack(head));
				}
				else
				{
					// Everything was consumed - hand the game a live-but-empty list, the
					// corroborated dummy-list idiom, never a null pointer.
					//
					// AND write the pruned (empty) head back to the caller. This branch used to leave
					// *a_events pointing at the node it had just consumed, and the engine hands that
					// same pointer straight back on every following dispatch that carries no new
					// input - so the last key event before the hands left the keyboard was
					// re-processed every frame: a released Backspace read as held (each typed
					// character deleted on arrival), a CharEvent replayed sixty times, every later
					// press swallowed. Measured 2026-09-12 with events spliced ahead of this hook;
					// it is the mechanism behind 'the search bar stopped taking input after I
					// erased' (xLenax, 1.7.4) and the earlier reorder-field report. The non-empty
					// branch above always wrote back; this one is now symmetric.
					//
					// 1.7.9: through writeBack - a caller whose list was ALREADY empty (a constant empty
					// list from another hook) is not written at all, which is the crash Haron reported.
					writeBack(nullptr);
					static RE::InputEvent* dummy[] = { nullptr };
					func(a_dispatcher, dummy);
				}
			}
		};
	}

	bool Install()
	{
		const std::uintptr_t site = offsets::kPollInputDevicesID.address() + offsets::kPollInputDevicesOffset.offset();

		if (!REL::make_pattern<"E8">().match(site))
		{
			logger::error("PollInputDevices site 0x{:X} (ID {}+0x{:X}) is not a call instruction; "
						  "input capture NOT installed - the menu will render but cannot take input",
						  site, offsets::kPollInputDevicesID.id(), offsets::kPollInputDevicesOffset.offset());
			return false;
		}

		auto& trampoline = SKSE::GetTrampoline();
		PollInputDevicesHook::func = trampoline.write_call<5>(site, PollInputDevicesHook::thunk);

		logger::info("PollInputDevices hook installed (ID {}+0x{:X}); input capture live",
					 offsets::kPollInputDevicesID.id(), offsets::kPollInputDevicesOffset.offset());

		return true;
	}

	// ---- keys ImGui believes are held --------------------------------------------------
	// A STUCK ESCAPE (the owner, 2026-09-21: clicking a text box "is not letting me delete the word anymore ...
	// it did actually require me to press escape just now, and it started typing again"). The log showed 62
	// text-field deaths with "keys this frame: Escape(d)" - ImGui thought Escape was HELD. Escape closes this
	// menu, and the release arrives after the menu is hidden, so ImGui never heard it; a held key repeats, and
	// every text field opened after that was cancelled by the repeat about 40 ms later (Escape is InputText's
	// cancel). Pressing Escape again delivered a release and "fixed" it, exactly as reported.
	// So every keyboard key sent DOWN is remembered, and each frame any key Windows reports as UP is released
	// in ImGui too - whatever path lost its real release. Keys younger than 250 ms are left alone, so a
	// driver-injected press (which Windows never sees) still lasts its intended frames.
	struct HeldKey { std::uint32_t scancode; std::chrono::steady_clock::time_point since; };
	std::unordered_map<int, HeldKey> g_heldKeys;   // ImGuiKey -> scancode it came from
	void ReleaseStuckKeys(ImGuiIO& a_io)
	{
		if (g_heldKeys.empty()) { return; }
		const auto now = std::chrono::steady_clock::now();
		for (auto it = g_heldKeys.begin(); it != g_heldKeys.end();)
		{
			if (now - it->second.since < std::chrono::milliseconds(250)) { ++it; continue; }
			const std::uint32_t sc = it->second.scancode;
			const UINT scan = (sc & 0x80) ? (0xE000u | (sc & 0x7Fu)) : sc;   // DirectInput 0xC8 = E0 48
			const UINT vk = ::MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX);
			if (vk != 0 && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) == 0)
			{
				const auto key = static_cast<ImGuiKey>(it->first);
				a_io.AddKeyEvent(key, false);
				if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift) { a_io.AddKeyEvent(ImGuiMod_Shift, false); }
				else if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl) { a_io.AddKeyEvent(ImGuiMod_Ctrl, false); }
				else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt) { a_io.AddKeyEvent(ImGuiMod_Alt, false); }
				logger::info("input: key {} (scan 0x{:02X}) was still held in the menu but is up on the keyboard - "
							 "its release was lost (usually because it closed the menu); released it",
							 ImGui::GetKeyName(key), sc);
				it = g_heldKeys.erase(it);
			}
			else { ++it; }
		}
	}

	void ProcessQueuedEvents()
	{
		std::vector<Record> drained;
		{
			std::scoped_lock lock(g_queueLock);
			// Deferred records: count down, and promote the ones that are due into this drain
			// AFTER everything already queued, so a press never overtakes the move before it.
			for (auto it = g_deferred.begin(); it != g_deferred.end();)
			{
				if (--it->framesLeft <= 0)
				{
					g_queue.push_back(it->record);
					it = g_deferred.erase(it);
				}
				else
				{
					++it;
				}
			}
			drained.swap(g_queue);
		}

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 display = io.DisplaySize;
		const bool controllerMode = UsingController();
		ReleaseStuckKeys(io);

		for (const Record& record : drained)
		{
			switch (record.kind)
			{
			case Record::Kind::kMouseMove:
				if (std::fabs(record.x) > kMouseMoveThreshold || std::fabs(record.y) > kMouseMoveThreshold)
				{
					NoteDevice(Device::kKeyboardMouse);
				}
				g_cursorX += record.x;
				g_cursorY += record.y;
				g_cursorX = g_cursorX < 0.0f ? 0.0f : (g_cursorX > display.x - 1.0f ? display.x - 1.0f : g_cursorX);
				g_cursorY = g_cursorY < 0.0f ? 0.0f : (g_cursorY > display.y - 1.0f ? display.y - 1.0f : g_cursorY);
				break;
			case Record::Kind::kCursorSet:
				// Absolute placement from a driver (DevBench). Counts as mouse use so the shell
				// switches to keyboard/mouse presentation, exactly as a real move would.
				NoteDevice(Device::kKeyboardMouse);
				g_cursorX = record.x < 0.0f ? 0.0f : (record.x > display.x - 1.0f ? display.x - 1.0f : record.x);
				g_cursorY = record.y < 0.0f ? 0.0f : (record.y > display.y - 1.0f ? display.y - 1.0f : record.y);
				// Publish the position NOW, ahead of any button record queued behind it in this
				// same drain. The per-frame AddMousePosEvent runs after the loop, so without this
				// a driver's press would reach ImGui before the move and land on the old spot.
				io.AddMousePosEvent(g_cursorX, g_cursorY);
				break;
			case Record::Kind::kMouseButton:
				if (record.down) { NoteDevice(Device::kKeyboardMouse); }
				if (record.code < ImGuiMouseButton_COUNT)
				{
					io.AddMouseButtonEvent(static_cast<int>(record.code), record.down);
				}
				break;
			case Record::Kind::kMouseWheel:
				io.AddMouseWheelEvent(record.x, record.y);
				break;
			case Record::Kind::kKeyboard:
				{
					if (record.down) { NoteDevice(Device::kKeyboardMouse); }
					// A key typed into a text field is text, not a command: F (favourite) and Page Up / Down
					// (tabs) must not fire while the search bar or a mod's text box is being typed into.
					if (record.down && !renderer::WantsTextInput()) { bindings::RaiseAllFor(record.code, false); }
					const ImGuiKey key = ScancodeToImGuiKey(record.code);
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);
						if (record.down) { g_heldKeys[static_cast<int>(key)] = HeldKey{ record.code, std::chrono::steady_clock::now() }; }
						else { g_heldKeys.erase(static_cast<int>(key)); }

						// Modifier flags tracked explicitly - "modifier keys are not left/right
						// side conscious" (survey, ModExplorerMenu's translation notes).
						if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
							io.AddKeyEvent(ImGuiMod_Shift, record.down);
						else if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
							io.AddKeyEvent(ImGuiMod_Ctrl, record.down);
						else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
							io.AddKeyEvent(ImGuiMod_Alt, record.down);
					}
					break;
				}
			case Record::Kind::kGamepad:
				// Observability (rule 31): log EVERY gamepad event that reaches this loop, so a
				// live DevBench-monitored test can tell apart "no gamepad events arrive at all"
				// (a game-level / input-device-mode problem - e.g. the Auto Input Switch mod not
				// present to route the device) from "events arrive but nav does not respond" (an
				// ImGui-side problem). If these lines are ABSENT while pressing buttons with the
				// menu open, the events are not reaching the framework.
				{
					if (record.code == 0x0040) { g_stickClicked[0].store(record.down, std::memory_order_relaxed); }
					if (record.code == 0x0080) { g_stickClicked[1].store(record.down, std::memory_order_relaxed); }
					const ImGuiKey key = GamepadMaskToImGuiKey(record.code);
					logger::debug("gamepad event: code=0x{:04X} down={} controllerMode={} -> imguiKey={}",
								  record.code, record.down, controllerMode, static_cast<int>(key));
					if (record.down) { NoteDevice(Device::kGamepad); }
					// A binding whose action has no ImGui key of its own raises a flag the renderer
					// consumes: the tab steps, the context menu and the favourite command are the
					// framework's own commands, not navigation.
					// EVERY action this input is bound to, not just the first. Two actions may share a
					// control when they can never be live together - Y is the on-screen keyboard's
					// backspace AND "open a mod's options" - but FromGamepad returns the first match
					// in enum order, which is the backspace, so the context menu was never raised
					// (the owner, 2026-09-19: "pressing Y doesn't, even though it's bound to it").
					if (record.down) { bindings::RaiseAllFor(record.code, true); }
					// 1.8.9: the on-screen keyboard takes the pad while it is open (D-pad, A, B, X, Y), and
					// takes the A that opens it on a highlighted text box; everything else falls through.
					if (controllerMode && keyboard::HandleGamepad(record.code, record.down))
					{
						break;
					}
					// A TEXT FIELD OWNS THE D-PAD WHILE IT IS ACTIVE (2026-09-19). ImGui's InputText
					// claims the keyboard arrow keys while you type, but NOT the gamepad D-pad - so a
					// D-pad press moved nav to another item, and moving nav off an active text box
					// deactivates it. From the player's side the box simply went dead, and the
					// on-screen keyboard closed with it, because the D-pad is exactly what you press
					// to walk that keyboard. Proven from the log: the frame the field died carried
					// "GamepadDpadDown(d)" and a fresh navJustMovedTo id, and nothing else.
					if (controllerMode && key != ImGuiKey_None && !TextFieldHasTheKeyboard(key))
					{
						io.AddKeyEvent(key, record.down);
					}
					else if (controllerMode && key == ImGuiKey_GamepadFaceRight && record.down &&
							 TextFieldHasTheKeyboard(key))
					{
						// Kept from ImGui above; the renderer turns it into "let go of the box".
						g_textFieldCancel.store(true, std::memory_order_release);
					}
				}
				break;
			case Record::Kind::kThumbstick:
				if (std::fabs(record.x) > kStickThreshold || std::fabs(record.y) > kStickThreshold)
				{
					NoteDevice(Device::kGamepad);
				}
				// Kept raw for GetStick() before anything is decided about navigation.
				if (record.code < 2)
				{
					g_stickX[record.code].store(record.x, std::memory_order_relaxed);
					g_stickY[record.code].store(record.y, std::memory_order_relaxed);
				}
				// Left stick -> ImGui gamepad-nav analog axes, so the stick moves the menu
				// selection like the D-pad (the author used the stick to "switch menus"; it was not
				// captured). Deadzone stops a resting stick drifting nav. y>0 = up in Skyrim's
				// thumbstick convention; if the live test shows it inverted, flip Up/Down.
				// The controller scheme (author's spec, 2026-08-31): the LEFT stick moves through
				// the list and across to the options with no button press; A takes hold of a slider;
				// the RIGHT stick then moves it. ImGui drives BOTH navigation and value tweaking from
				// the same LStick nav axes, so exactly one stick is wired to them per frame - whichever
				// the scheme says is in charge:  nothing being edited -> LEFT (navigate);  an item
				// taken hold of -> RIGHT (move the value). The idle stick is explicitly released so a
				// resting-but-off-centre stick cannot leave a nav axis stuck down.
				if (controllerMode && keyboard::HandleStick(record.code, record.x, record.y))
				{
					break;   // 1.8.9: the keyboard has the left stick while it is open; 1.9.0: and swallows the right one
				}
				if (controllerMode)
				{
					const bool editing = g_itemActive.load(std::memory_order_relaxed);
					const bool isLeftStick = record.code == 0;
					// A consumer holding the sticks takes BOTH out of navigation, so the selection
					// cannot move while the player is handling whatever the page gave them (1.9.5).
					const bool captured = g_sticksCaptured.load(std::memory_order_relaxed);
					const bool inCharge = captured ? false : (editing ? !isLeftStick : isLeftStick);
					constexpr float dz = 0.35f;
					float sx = inCharge ? record.x : 0.0f, sy = inCharge ? record.y : 0.0f;
					// Same reasoning as the D-pad above: the nav axes are what move focus, so while a
					// text field holds the keyboard the sticks are reported as centred.
					const bool typing = GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId);
					if (typing) { sx = 0.0f; sy = 0.0f; }
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  sx < -dz, sx < -dz ? -sx : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, sx >  dz, sx >  dz ?  sx : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    sy >  dz, sy >  dz ?  sy : 0.0f);
					io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  sy < -dz, sy < -dz ? -sy : 0.0f);
					logger::debug("thumbstick({}): x={:.2f} y={:.2f} {} (editing={})",
								  isLeftStick ? "L" : "R", record.x, record.y,
								  inCharge ? "-> nav" : "(idle this frame)", editing);
				}
				break;
			case Record::Kind::kCharacter:
				io.AddInputCharacter(record.code);
				break;
			}
		}

		// One authoritative cursor position per frame, movement or not. The Win32 backend's
		// fallback poll pushes the OS cursor position (which the game recentres at will) into
		// the same event queue every frame; on frames where we stayed silent that stale
		// position won, which is exactly the flicker/teleport of the 1.1.0 smoke test. Being
		// unconditionally last - paired with trickle-off (set at init) - means the software
		// cursor is the only position ImGui ever acts on.
		io.AddMousePosEvent(g_cursorX, g_cursorY);
		g_cursorMirrorX.store(g_cursorX, std::memory_order_relaxed);
		g_cursorMirrorY.store(g_cursorY, std::memory_order_relaxed);
	}

	void OnMenuOpened()
	{
		const ImVec2 display = ImGui::GetIO().DisplaySize;
		g_cursorX = display.x * 0.5f;
		g_cursorY = display.y * 0.5f;
		ImGui::GetIO().AddMousePosEvent(g_cursorX, g_cursorY);

		// Nothing is held when the menu opens: a release that arrived while it was hidden is gone for good.
		ImGui::GetIO().ClearInputKeys();
		g_heldKeys.clear();

		std::scoped_lock lock(g_queueLock);
		g_queue.clear();

		logger::debug("menu opened: cursor centred at ({:.0f}, {:.0f}), stale queue cleared", g_cursorX, g_cursorY);
	}

	void BeginRebindToggleKey()
	{
		g_awaitingRebind.store(true, std::memory_order_release);
		logger::info("awaiting menu toggle-key rebind - next keyboard key wins, Escape cancels");
	}

	bool IsAwaitingRebind()
	{
		return g_awaitingRebind.load(std::memory_order_acquire);
	}

	Device LastDevice()
	{
		return g_lastDevice.load(std::memory_order_relaxed);
	}

	bool UsingController()
	{
		// Unknown means nothing deliberate has happened yet, and that resolves to KEYBOARD: this
		// is a PC framework, the menu is opened with a key, and guessing "controller" for a player
		// who has not touched one would hand them prompts for a device they may not own.
		return g_lastDevice.load(std::memory_order_relaxed) == Device::kGamepad;
	}

	float SecondsSinceLastDevice()
	{
		const auto at = g_lastDeviceAt.load(std::memory_order_relaxed);
		if (at == std::chrono::steady_clock::time_point{}) { return -1.0f; }
		return std::chrono::duration<float>(std::chrono::steady_clock::now() - at).count();
	}

	void SetItemActive(bool a_active)
	{
		g_itemActive.store(a_active, std::memory_order_relaxed);
	}

	void ArmKeyCapture()
	{
		g_lastCaptured.store(-1, std::memory_order_release);
		g_captureArmed.store(true, std::memory_order_release);
		logger::info("keybind capture armed - next keyboard/gamepad press will be recorded (not consumed)");
	}

	void CancelKeyCapture()
	{
		g_captureArmed.store(false, std::memory_order_release);
	}

	bool IsKeyCaptureArmed()
	{
		return g_captureArmed.load(std::memory_order_acquire);
	}

	std::int64_t LastCapturedKey()
	{
		return g_lastCaptured.load(std::memory_order_acquire);
	}

	void SetCursorAbsolute(float a_x, float a_y)
	{
		Enqueue({ Record::Kind::kCursorSet, 0, false, a_x, a_y });
	}

	void QueueMouseButton(std::uint32_t a_button, bool a_down)
	{
		Enqueue({ Record::Kind::kMouseButton, a_button, a_down, 0.0f, 0.0f });
	}

	void QueueMouseClick(std::uint32_t a_button)
	{
		std::scoped_lock lock(g_queueLock);
		g_deferred.push_back({ 1, { Record::Kind::kMouseButton, a_button, true, 0.0f, 0.0f } });
		g_deferred.push_back({ 3, { Record::Kind::kMouseButton, a_button, false, 0.0f, 0.0f } });
	}

	// Driver-side key press (DirectInput scan code) and text, queued as the SAME records the game's
	// own events become - so a headless test exercises the translation and ImGui exactly as a
	// keyboard would, from the record queue onward. Down and up land on separate frames.

	void InjectPress(std::uint32_t a_device, std::uint32_t a_code, int a_holdFrames)
	{
		RE::INPUT_DEVICE dev = a_device == 2 ? RE::INPUT_DEVICE::kGamepad : (a_device == 1 ? RE::INPUT_DEVICE::kMouse : RE::INPUT_DEVICE::kKeyboard);
		std::scoped_lock l(inject::g_lock);
		inject::g_presses.push_back({ dev, a_code, a_holdFrames < 1 ? 1 : (a_holdFrames > 600 ? 600 : a_holdFrames), false });
	}
	void InjectText(const std::string& a_utf8)
	{
		std::scoped_lock l(inject::g_lock);
		for (unsigned char c : a_utf8) { inject::g_chars.push_back(static_cast<std::uint32_t>(c)); }
	}

	void QueueKey(std::uint32_t a_scancode)
	{
		std::scoped_lock lock(g_queueLock);
		g_deferred.push_back({ 1, { Record::Kind::kKeyboard, a_scancode, true, 0.0f, 0.0f } });
		g_deferred.push_back({ 3, { Record::Kind::kKeyboard, a_scancode, false, 0.0f, 0.0f } });
	}

	void QueueText(const std::string& a_utf8)
	{
		std::scoped_lock lock(g_queueLock);
		int frame = 1;
		for (unsigned char c : a_utf8) {
			g_deferred.push_back({ frame++, { Record::Kind::kCharacter, static_cast<std::uint32_t>(c), true, 0.0f, 0.0f } });
		}
	}

	void SetSticksCaptured(bool a_captured)
	{
		const bool was = g_sticksCaptured.exchange(a_captured, std::memory_order_release);
		if (was != a_captured)
		{
			// itemActive is reported with the release because it decides which stick drives
			// navigation: while an item is being edited the RIGHT stick moves it and the LEFT one is
			// held off. If it is still true after a page lets go, the left stick stays dead and the
			// D-pad appears to be the only thing that works (the owner, 2026-09-19).
			logger::info("input: the thumbsticks are {} by a mod's page (itemActive={})",
						 a_captured ? "held" : "released", g_itemActive.load(std::memory_order_relaxed));
		}
	}

	bool AreSticksCaptured()
	{
		return g_sticksCaptured.load(std::memory_order_acquire);
	}

	void GetStick(int a_which, float& a_x, float& a_y, bool& a_clicked, bool& a_live)
	{
		const int i = (a_which == 1) ? 1 : 0;
		a_x = g_stickX[i].load(std::memory_order_relaxed);
		a_y = g_stickY[i].load(std::memory_order_relaxed);
		a_clicked = g_stickClicked[i].load(std::memory_order_relaxed);
		a_live = std::fabs(a_x) > kStickThreshold || std::fabs(a_y) > kStickThreshold;
	}

	bool TakeTextFieldCancel()
	{
		return g_textFieldCancel.exchange(false, std::memory_order_acq_rel);
	}

	void GetCursor(float& a_x, float& a_y)
	{
		a_x = g_cursorMirrorX.load(std::memory_order_relaxed);
		a_y = g_cursorMirrorY.load(std::memory_order_relaxed);
	}
}
