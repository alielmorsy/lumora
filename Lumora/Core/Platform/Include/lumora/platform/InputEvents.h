#pragma once

#include <cstdint>

namespace lumora::platform
{
    enum class InputEventType : uint8_t
    {
        PointerMove,
        PointerDown,
        PointerUp,
        Scroll,
        KeyDown,
        KeyUp,
        Text,
    };

    enum class PointerButton : uint8_t
    {
        Left, Right, Middle, X1, X2,
    };

    /// Host-independent logical keys. Printable characters arrive separately as Text events (UTF-8).
    enum class Key : uint16_t
    {
        Unknown = 0,

        // Control
        Backspace, Enter, Tab, Escape, Delete, Space,
        Insert, PrintScreen, Pause,

        // Navigation
        Left, Right, Up, Down,
        Home, End, PageUp, PageDown,

        // Locks
        CapsLock, NumLock, ScrollLock,

        // Modifiers
        LeftShift, RightShift,
        LeftCtrl,  RightCtrl,
        LeftAlt,   RightAlt,
        LeftSuper, RightSuper,

        // Letters
        A, B, C, D, E, F, G, H, I, J, K, L, M,
        N, O, P, Q, R, S, T, U, V, W, X, Y, Z,

        // Number row
        N0, N1, N2, N3, N4, N5, N6, N7, N8, N9,

        // Function keys
        F1,  F2,  F3,  F4,  F5,  F6,
        F7,  F8,  F9,  F10, F11, F12,
        F13, F14, F15, F16, F17, F18,
        F19, F20, F21, F22, F23, F24,

        // Numpad
        Num0, Num1, Num2, Num3, Num4,
        Num5, Num6, Num7, Num8, Num9,
        NumAdd, NumSub, NumMul, NumDiv,
        NumDecimal, NumEnter,

        // Symbols (US layout)
        Grave, Minus, Equal,
        LeftBracket, RightBracket, Backslash,
        Semicolon, Apostrophe,
        Comma, Period, Slash,
    };

    enum class KeyMods : uint8_t
    {
        None  = 0,
        Ctrl  = 1 << 0,
        Shift = 1 << 1,
        Alt   = 1 << 2,
        Super = 1 << 3,
    };

    [[nodiscard]] constexpr KeyMods operator|(KeyMods a, KeyMods b) noexcept
    {
        return static_cast<KeyMods>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }
    [[nodiscard]] constexpr KeyMods operator&(KeyMods a, KeyMods b) noexcept
    {
        return static_cast<KeyMods>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }
    constexpr KeyMods& operator|=(KeyMods& a, KeyMods b) noexcept { return a = a | b; }

    [[nodiscard]] constexpr bool Has(KeyMods set, KeyMods bit) noexcept { return (set & bit) == bit; }

    struct InputVec2
    {
        float x = 0.f;
        float y = 0.f;
    };

    /// One raw input event from the window; trivially copyable. Enter/leave, focus and handled flags are a UI concern and absent here.
    struct InputEvent
    {
        InputEventType type = InputEventType::PointerMove;
        InputVec2      position{};    // client-area pixels, top-left origin
        InputVec2      scrollDelta{}; // wheel notches (x, y); +y scrolls up
        InputVec2      delta{};       // relative pointer motion since the last event; the only motion
                                      // source while the cursor is captured, where `position` is frozen
        PointerButton  button = PointerButton::Left;
        Key            key    = Key::Unknown;
        char           text[32] = {}; // NUL-terminated UTF-8 for Text events
        KeyMods        mods   = KeyMods::None;
    };
}
