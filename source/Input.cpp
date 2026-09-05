#include "Input.h"

#include <chrono>
#include <cmath>

#include "Compat.h"
#include "Offsets.h"
#include "Renderer.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <imgui.h>

#include <atomic>
#include <mutex>
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
		ImGuiKey ScancodeToImGuiKey(std::uint32_t a_scancode)
		{
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
			default:   return ImGuiKey_None;
			}
		}

		// -----------------------------------------------------------------------------------
		// XInput button mask (RE::BSWin32GamepadDevice::Key) -> ImGuiKey gamepad navigation.
		// Fed only in controller mode (explicit toggle - never auto-detected; nav-drift rule).
		// -----------------------------------------------------------------------------------
		ImGuiKey GamepadMaskToImGuiKey(std::uint32_t a_mask)
		{
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
						button->GetIDCode() == toggleKey && button->IsDown() &&
						compat::IsHotkeyEnabled())
					{
						renderer::ToggleMainWindow();
						passThrough = false;  // the game never sees the framework's own key
					}
					else if (menuOpen && controllerMode && button &&
						button->GetDevice() == RE::INPUT_DEVICE::kGamepad &&
						button->GetIDCode() == kGamepadStart && button->IsDown())
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

						// Releases pass through so a key/button held across the open transition
						// releases cleanly game-side (a stray release for an unpressed key is a
						// no-op). Everything else is consumed - THIS is what halts the camera,
						// the scroll-zoom and movement while the menu is up.
						passThrough = button && button->IsUp();
					}

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

				if (head)
				{
					*a_events = head;
					func(a_dispatcher, a_events);
				}
				else
				{
					// Everything was consumed - hand the game a live-but-empty list, the
					// corroborated dummy-list idiom, never a null pointer.
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
					const ImGuiKey key = ScancodeToImGuiKey(record.code);
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);

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
					const ImGuiKey key = GamepadMaskToImGuiKey(record.code);
					logger::debug("gamepad event: code=0x{:04X} down={} controllerMode={} -> imguiKey={}",
								  record.code, record.down, controllerMode, static_cast<int>(key));
					if (record.down) { NoteDevice(Device::kGamepad); }
					if (controllerMode && key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);
					}
				}
				break;
			case Record::Kind::kThumbstick:
				if (std::fabs(record.x) > kStickThreshold || std::fabs(record.y) > kStickThreshold)
				{
					NoteDevice(Device::kGamepad);
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
				if (controllerMode)
				{
					const bool editing = g_itemActive.load(std::memory_order_relaxed);
					const bool isLeftStick = record.code == 0;
					const bool inCharge = editing ? !isLeftStick : isLeftStick;
					constexpr float dz = 0.35f;
					const float sx = inCharge ? record.x : 0.0f, sy = inCharge ? record.y : 0.0f;
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

	void GetCursor(float& a_x, float& a_y)
	{
		a_x = g_cursorMirrorX.load(std::memory_order_relaxed);
		a_y = g_cursorMirrorY.load(std::memory_order_relaxed);
	}
}
