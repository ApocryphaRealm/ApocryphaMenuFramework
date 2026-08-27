#include "Input.h"

#include "Offsets.h"
#include "Renderer.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <imgui.h>

#include <mutex>
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
				kCharacter,
			};

			Kind kind{};
			std::uint32_t code = 0;   // idCode: mouse button index / DIK scancode / XInput mask / unicode
			bool down = false;        // press (true) or release (false) transitions only
			float x = 0.0f;           // mouse deltas / wheel direction
			float y = 0.0f;
		};

		std::mutex g_queueLock;
		std::vector<Record> g_queue;

		// Software cursor, owned by the render thread. The game recentres/hides the OS cursor
		// at will, so the only trustworthy position is one we integrate ourselves from the
		// MouseMoveEvent deltas (the Wheeler-lineage approach from the survey).
		float g_cursorX = 0.0f;
		float g_cursorY = 0.0f;

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

				RE::InputEvent* head = *a_events;
				RE::InputEvent* previous = nullptr;
				RE::InputEvent* current = head;

				while (current)
				{
					RE::InputEvent* next = current->next;
					bool passThrough = true;

					const RE::ButtonEvent* button = current->AsButtonEvent();

					if (button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
						button->GetIDCode() == toggleKey && button->IsDown())
					{
						renderer::ToggleMainWindow();
						passThrough = false;  // the game never sees the framework's own key
					}
					else if (menuOpen)
					{
						CopyForImGui(current);

						// Releases pass through so a key/button held across the open transition
						// releases cleanly game-side (a stray release for an unpressed key is a
						// no-op). Everything else is consumed - THIS is what halts the camera,
						// the scroll-zoom and movement while the menu is up.
						passThrough = button && button->IsUp();
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
			drained.swap(g_queue);
		}

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 display = io.DisplaySize;
		const bool controllerMode = settings::Get().controllerMode;

		for (const Record& record : drained)
		{
			switch (record.kind)
			{
			case Record::Kind::kMouseMove:
				g_cursorX += record.x;
				g_cursorY += record.y;
				g_cursorX = g_cursorX < 0.0f ? 0.0f : (g_cursorX > display.x - 1.0f ? display.x - 1.0f : g_cursorX);
				g_cursorY = g_cursorY < 0.0f ? 0.0f : (g_cursorY > display.y - 1.0f ? display.y - 1.0f : g_cursorY);
				break;
			case Record::Kind::kMouseButton:
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
					const ImGuiKey key = ScancodeToImGuiKey(record.code);
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);

						// Modifier flags tracked explicitly - "modifier keys are not left/right
						// side conscious" (survey, ModExplorerMenu's translation notes).
						if (key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift)
							io.AddKeyEvent(ImGuiKey_ModShift, record.down);
						else if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl)
							io.AddKeyEvent(ImGuiKey_ModCtrl, record.down);
						else if (key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt)
							io.AddKeyEvent(ImGuiKey_ModAlt, record.down);
					}
					break;
				}
			case Record::Kind::kGamepad:
				if (controllerMode)
				{
					const ImGuiKey key = GamepadMaskToImGuiKey(record.code);
					if (key != ImGuiKey_None)
					{
						io.AddKeyEvent(key, record.down);
					}
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
}
