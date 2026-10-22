#include "WindowsHeaders.h"

#include <Xinput.h>

#include "lumora/platform/GamepadState.h"

#include "lumora/log/Log.h"

LUMORA_LOG_CATEGORY("platform.gamepad")

namespace lumora::platform
{
    namespace
    {
        // XInputGetState on an absent pad enumerates devices (hundreds of microseconds), so a disconnected slot is re-probed
        // on a cadence, not every poll.
        constexpr std::uint32_t kRetryIntervalPolls = 120;

        struct SlotTracker
        {
            bool          connected       = false;
            std::uint32_t pollsUntilRetry = 0;
        };

        SlotTracker g_slots[kMaxGamepads];

        float NormalizeAxis(SHORT raw) noexcept
        {
            const float value = raw >= 0
                ? static_cast<float>(raw) / 32767.f
                : static_cast<float>(raw) / 32768.f;
            return value < -1.f ? -1.f : value > 1.f ? 1.f : value;
        }

        float NormalizeTrigger(BYTE raw) noexcept
        {
            return static_cast<float>(raw) / 255.f;
        }

        std::uint32_t MapButtons(WORD wButtons) noexcept
        {
            std::uint32_t buttons = 0;
            const auto set = [&](WORD mask, GamepadButton button) noexcept
            {
                if (wButtons & mask)
                    buttons |= (1u << static_cast<std::uint32_t>(button));
            };

            set(XINPUT_GAMEPAD_A, GamepadButton::South);
            set(XINPUT_GAMEPAD_B, GamepadButton::East);
            set(XINPUT_GAMEPAD_X, GamepadButton::West);
            set(XINPUT_GAMEPAD_Y, GamepadButton::North);
            set(XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftShoulder);
            set(XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightShoulder);
            set(XINPUT_GAMEPAD_BACK, GamepadButton::Back);
            set(XINPUT_GAMEPAD_START, GamepadButton::Start);
            set(XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftStickPress);
            set(XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightStickPress);
            set(XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp);
            set(XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DPadDown);
            set(XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DPadLeft);
            set(XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DPadRight);

            return buttons;
        }

        void PollSlot(std::uint8_t index, GamepadState& out) noexcept
        {
            SlotTracker& slot = g_slots[index];

            if (!slot.connected && slot.pollsUntilRetry > 0)
            {
                --slot.pollsUntilRetry;
                out = GamepadState{};
                return;
            }

            XINPUT_STATE state{};
            const DWORD result = ::XInputGetState(index, &state);

            if (result == ERROR_SUCCESS)
            {
                out.buttons      = MapButtons(state.Gamepad.wButtons);
                out.leftStickX   = NormalizeAxis(state.Gamepad.sThumbLX);
                out.leftStickY   = NormalizeAxis(state.Gamepad.sThumbLY);
                out.rightStickX  = NormalizeAxis(state.Gamepad.sThumbRX);
                out.rightStickY  = NormalizeAxis(state.Gamepad.sThumbRY);
                out.leftTrigger  = NormalizeTrigger(state.Gamepad.bLeftTrigger);
                out.rightTrigger = NormalizeTrigger(state.Gamepad.bRightTrigger);
                out.connected    = true;

                if (!slot.connected)
                    LOG_INFO("gamepad {} connected", index);

                slot.connected       = true;
                slot.pollsUntilRetry = 0;
            }
            else
            {
                out = GamepadState{};

                if (slot.connected)
                    LOG_INFO("gamepad {} disconnected", index);

                slot.connected       = false;
                slot.pollsUntilRetry = kRetryIntervalPolls;
            }
        }
    }

    void PollGamepads(GamepadState* out) noexcept
    {
        for (std::uint8_t i = 0; i < kMaxGamepads; ++i)
            PollSlot(i, out[i]);
    }

    bool GamepadConnected(std::uint8_t index) noexcept
    {
        return index < kMaxGamepads && g_slots[index].connected;
    }
}
