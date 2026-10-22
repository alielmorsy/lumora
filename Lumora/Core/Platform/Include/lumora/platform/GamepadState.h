#pragma once

#include <lumora/Api.h>

#include <cstdint>

namespace lumora::platform
{
    inline constexpr std::uint8_t kMaxGamepads = 4;

    /// Bit positions in GamepadState::buttons (Xbox layout).
    enum class GamepadButton : std::uint8_t
    {
        South = 0, East, West, North,
        LeftShoulder, RightShoulder,
        Back, Start,
        LeftStickPress, RightStickPress,
        DPadUp, DPadDown, DPadLeft, DPadRight,
        Count,
    };

    /// One pad as of the last poll. Axes are normalised: sticks -1..1 with +y up, triggers 0..1. Deadzones are not applied;
    /// they are authored per binding.
    struct GamepadState
    {
        std::uint32_t buttons      = 0;
        float         leftStickX   = 0.f;
        float         leftStickY   = 0.f;
        float         rightStickX  = 0.f;
        float         rightStickY  = 0.f;
        float         leftTrigger  = 0.f;
        float         rightTrigger = 0.f;
        bool          connected    = false;

        [[nodiscard]] bool Down(GamepadButton b) const noexcept
        {
            return (buttons & (1u << static_cast<std::uint32_t>(b))) != 0;
        }
    };

    /// Polls every pad into `out` (kMaxGamepads entries); a disconnected pad is written as a default state, so it reads as all released.
    LUMORA_API void PollGamepads(GamepadState* out) noexcept;

    [[nodiscard]] LUMORA_API bool GamepadConnected(std::uint8_t index) noexcept;
}
