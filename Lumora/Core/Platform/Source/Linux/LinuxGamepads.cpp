#include "lumora/platform/GamepadState.h"

#include "lumora/log/Log.h"

#include <fcntl.h>
#include <linux/joystick.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

LUMORA_LOG_CATEGORY("platform.gamepad")

namespace lumora::platform
{
    namespace
    {
        // Opening an absent /dev/input/jsN is cheap, but a disconnected slot is still re-probed on a cadence, not every poll.
        constexpr std::uint32_t kRetryIntervalPolls = 120;

        struct Slot
        {
            int           fd              = -1;
            std::uint32_t pollsUntilRetry = 0;
            GamepadState  state{};
        };

        Slot g_slots[kMaxGamepads];

        float NormalizeAxis(const short raw) noexcept
        {
            const float value = raw >= 0 ? static_cast<float>(raw) / 32767.f : static_cast<float>(raw) / 32768.f;
            return value < -1.f ? -1.f : value > 1.f ? 1.f : value;
        }

        float NormalizeTrigger(const short raw) noexcept
        {
            return (static_cast<float>(raw) + 32768.f) / 65535.f;
        }

        void SetButton(GamepadState& state, const GamepadButton button, const bool down) noexcept
        {
            const std::uint32_t bit = 1u << static_cast<std::uint32_t>(button);
            state.buttons = down ? (state.buttons | bit) : (state.buttons & ~bit);
        }

        // ponytail: the xpad / Xbox layout of the legacy joystick API; other pads need the SDL controller database.
        void ApplyButton(GamepadState& state, const std::uint8_t number, const bool down) noexcept
        {
            static constexpr GamepadButton map[] = {
                GamepadButton::South,        GamepadButton::East,           GamepadButton::West,
                GamepadButton::North,        GamepadButton::LeftShoulder,   GamepadButton::RightShoulder,
                GamepadButton::Back,         GamepadButton::Start,          GamepadButton::Count /* guide */,
                GamepadButton::LeftStickPress, GamepadButton::RightStickPress};
            if (number < std::size(map) && map[number] != GamepadButton::Count) SetButton(state, map[number], down);
        }

        void ApplyAxis(GamepadState& state, const std::uint8_t number, const short value) noexcept
        {
            switch (number)
            {
            case 0: state.leftStickX = NormalizeAxis(value); break;
            case 1: state.leftStickY = -NormalizeAxis(value); break;
            case 2: state.leftTrigger = NormalizeTrigger(value); break;
            case 3: state.rightStickX = NormalizeAxis(value); break;
            case 4: state.rightStickY = -NormalizeAxis(value); break;
            case 5: state.rightTrigger = NormalizeTrigger(value); break;
            case 6:
                SetButton(state, GamepadButton::DPadLeft, value < 0);
                SetButton(state, GamepadButton::DPadRight, value > 0);
                break;
            case 7:
                SetButton(state, GamepadButton::DPadUp, value < 0);
                SetButton(state, GamepadButton::DPadDown, value > 0);
                break;
            default: break;
            }
        }

        void Disconnect(const std::uint8_t index, Slot& slot) noexcept
        {
            if (slot.fd >= 0)
            {
                ::close(slot.fd);
                LOG_INFO("gamepad {} disconnected", index);
            }
            slot = Slot{};
            slot.pollsUntilRetry = kRetryIntervalPolls;
        }

        void PollSlot(const std::uint8_t index, GamepadState& out) noexcept
        {
            Slot& slot = g_slots[index];

            if (slot.fd < 0)
            {
                if (slot.pollsUntilRetry > 0)
                {
                    --slot.pollsUntilRetry;
                    out = GamepadState{};
                    return;
                }

                char path[32];
                std::snprintf(path, sizeof(path), "/dev/input/js%u", index);
                slot.fd = ::open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
                if (slot.fd < 0)
                {
                    slot.pollsUntilRetry = kRetryIntervalPolls;
                    out = GamepadState{};
                    return;
                }
                slot.state           = GamepadState{};
                slot.state.connected = true;
                LOG_INFO("gamepad {} connected", index);
            }

            js_event event;
            for (;;)
            {
                const ssize_t n = ::read(slot.fd, &event, sizeof(event));
                if (n == static_cast<ssize_t>(sizeof(event)))
                {
                    const std::uint8_t type = event.type & ~JS_EVENT_INIT;
                    if (type == JS_EVENT_BUTTON) ApplyButton(slot.state, event.number, event.value != 0);
                    else if (type == JS_EVENT_AXIS) ApplyAxis(slot.state, event.number, event.value);
                    continue;
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR)
                {
                    Disconnect(index, slot);
                    out = GamepadState{};
                    return;
                }
                break;
            }
            out = slot.state;
        }
    }

    void PollGamepads(GamepadState* out) noexcept
    {
        for (std::uint8_t i = 0; i < kMaxGamepads; ++i)
            PollSlot(i, out[i]);
    }

    bool GamepadConnected(const std::uint8_t index) noexcept
    {
        return index < kMaxGamepads && g_slots[index].fd >= 0;
    }
}
