#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Vec2.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

namespace lumora
{
    /// The routed input event, built from the host's raw platform event plus dispatcher-only notions (enter/leave/focus
    /// kinds, Consumed). Public because Component::OnEvent receives it.
    enum class EventType : std::uint8_t
    {
        MouseDown, MouseUp, MouseMove, MouseEnter, MouseLeave, Scroll,
        KeyDown, KeyUp, TextInput, FocusGained, FocusLost,
    };

    /// Mirrors lumora::platform::PointerButton value-for-value. Dispatch reacts to the first three; X1/X2 reach a node
    /// only via `PointerInfo::button`.
    enum class PointerButton : std::uint8_t { Left, Right, Middle, X1, X2 };

    /// Host-independent logical keys. Printable characters arrive separately as TextInput (UTF-8); KeyDown carries the key.
    /// Mirrors lumora::platform::Key value-for-value (converted by static_cast under static_asserts); the range helpers
    /// below rely on the block order.
    enum class Key : std::uint16_t
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

    namespace detail
    {
        [[nodiscard]] constexpr bool InRange(Key key, Key first, Key last) noexcept
        {
            return key >= first && key <= last;
        }
    }

    /// A key that produces text when typed unmodified. A text field consuming these KeyDowns keeps bare-letter
    /// shortcuts from firing under the caret.
    [[nodiscard]] constexpr bool IsTextKey(Key key) noexcept
    {
        return key == Key::Space || detail::InRange(key, Key::A, Key::Z) ||
               detail::InRange(key, Key::N0, Key::N9) || detail::InRange(key, Key::Num0, Key::NumDecimal) ||
               detail::InRange(key, Key::Grave, Key::Slash);
    }

    /// Shift/Ctrl/Alt/Super themselves. A chord fires on the non-modifier key, so these never match one.
    [[nodiscard]] constexpr bool IsModifierKey(Key key) noexcept
    {
        return detail::InRange(key, Key::LeftShift, Key::RightSuper);
    }

    /// The DOM `KeyboardEvent.key` spelling of an unshifted key ("Enter", "ArrowLeft", "a", "1"); "Unidentified" for Unknown.
    [[nodiscard]] LUMORA_API std::string_view KeyName(Key key) noexcept;

    enum class KeyMods : std::uint8_t
    {
        None  = 0,
        Ctrl  = 1 << 0,
        Shift = 1 << 1,
        Alt   = 1 << 2,
        Super = 1 << 3,
    };

    [[nodiscard]] constexpr KeyMods operator|(KeyMods a, KeyMods b) noexcept
    {
        return static_cast<KeyMods>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }
    [[nodiscard]] constexpr KeyMods operator&(KeyMods a, KeyMods b) noexcept
    {
        return static_cast<KeyMods>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
    }
    constexpr KeyMods& operator|=(KeyMods& a, KeyMods b) noexcept { return a = a | b; }

    [[nodiscard]] constexpr bool Has(KeyMods set, KeyMods bit) noexcept { return (set & bit) == bit; }

    /// Routing pass: Capture (root to target) then Bubble (target to root); OnEvent can take first refusal on Capture.
    enum class EventPhase : std::uint8_t { Capture, Target, Bubble };

    /// One routed event; trivially copyable (the dispatcher flips Consumed on a private copy). Enter/Leave and Focus* are
    /// dispatcher-derived.
    struct Event
    {
        EventType     Type        = EventType::MouseMove;
        Vec2          Position{};    // surface-space pixels, top-left origin
        Vec2          ScrollDelta{}; // wheel notches (x, y) for Scroll
        PointerButton Button      = PointerButton::Left;
        Key           KeyCode     = Key::Unknown;
        char          Text[32]    = {}; // NUL-terminated UTF-8 for TextInput
        KeyMods       Mods        = KeyMods::None;
        bool          Consumed    = false;
        EventPhase    Phase       = EventPhase::Target; // routing pass; set by the dispatcher

        /// Stop routing: no further node on the capture/target/bubble path sees this event.
        void Consume() noexcept { Consumed = true; }
        [[nodiscard]] bool IsConsumed() const noexcept { return Consumed; }
    };

    static_assert(std::is_trivially_copyable_v<Event>);

    /// Host positions are physical client pixels; layout rects live in logical surface space.
    [[nodiscard]] constexpr Vec2 PhysicalToLogical(Vec2 physical, float contentScale) noexcept
    {
        const float inv = contentScale > 0.f ? 1.f / contentScale : 1.f;
        return {physical.x * inv, physical.y * inv};
    }
}
