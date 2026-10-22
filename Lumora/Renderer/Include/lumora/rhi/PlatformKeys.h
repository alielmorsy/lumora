#pragma once

#include <lumora/Input/Event.h>
#include <lumora/platform/InputEvents.h>

#include <cstdint>
#include <type_traits>

namespace lumora
{
    /// Where lumora's input vocabulary meets the platform's. The enums mirror each other value for value, so conversions are
    /// casts; these asserts fail the build if one side gains a key the other lacks.
    static_assert(std::is_same_v<std::underlying_type_t<Key>, std::underlying_type_t<platform::Key>>);
    static_assert(static_cast<std::uint16_t>(Key::Unknown) == static_cast<std::uint16_t>(platform::Key::Unknown));
    static_assert(static_cast<std::uint16_t>(Key::Backspace) == static_cast<std::uint16_t>(platform::Key::Backspace));
    static_assert(static_cast<std::uint16_t>(Key::Pause) == static_cast<std::uint16_t>(platform::Key::Pause));
    static_assert(static_cast<std::uint16_t>(Key::Left) == static_cast<std::uint16_t>(platform::Key::Left));
    static_assert(static_cast<std::uint16_t>(Key::PageDown) == static_cast<std::uint16_t>(platform::Key::PageDown));
    static_assert(static_cast<std::uint16_t>(Key::CapsLock) == static_cast<std::uint16_t>(platform::Key::CapsLock));
    static_assert(static_cast<std::uint16_t>(Key::LeftShift) == static_cast<std::uint16_t>(platform::Key::LeftShift));
    static_assert(static_cast<std::uint16_t>(Key::RightSuper) == static_cast<std::uint16_t>(platform::Key::RightSuper));
    static_assert(static_cast<std::uint16_t>(Key::A) == static_cast<std::uint16_t>(platform::Key::A));
    static_assert(static_cast<std::uint16_t>(Key::Z) == static_cast<std::uint16_t>(platform::Key::Z));
    static_assert(static_cast<std::uint16_t>(Key::N0) == static_cast<std::uint16_t>(platform::Key::N0));
    static_assert(static_cast<std::uint16_t>(Key::N9) == static_cast<std::uint16_t>(platform::Key::N9));
    static_assert(static_cast<std::uint16_t>(Key::F1) == static_cast<std::uint16_t>(platform::Key::F1));
    static_assert(static_cast<std::uint16_t>(Key::F24) == static_cast<std::uint16_t>(platform::Key::F24));
    static_assert(static_cast<std::uint16_t>(Key::Num0) == static_cast<std::uint16_t>(platform::Key::Num0));
    static_assert(static_cast<std::uint16_t>(Key::NumEnter) == static_cast<std::uint16_t>(platform::Key::NumEnter));
    static_assert(static_cast<std::uint16_t>(Key::Grave) == static_cast<std::uint16_t>(platform::Key::Grave));
    static_assert(static_cast<std::uint16_t>(Key::Slash) == static_cast<std::uint16_t>(platform::Key::Slash));

    static_assert(static_cast<std::uint8_t>(PointerButton::Left) == static_cast<std::uint8_t>(platform::PointerButton::Left));
    static_assert(static_cast<std::uint8_t>(PointerButton::Right) == static_cast<std::uint8_t>(platform::PointerButton::Right));
    static_assert(static_cast<std::uint8_t>(PointerButton::Middle) == static_cast<std::uint8_t>(platform::PointerButton::Middle));
    static_assert(static_cast<std::uint8_t>(PointerButton::X1) == static_cast<std::uint8_t>(platform::PointerButton::X1));
    static_assert(static_cast<std::uint8_t>(PointerButton::X2) == static_cast<std::uint8_t>(platform::PointerButton::X2));

    static_assert(static_cast<std::uint8_t>(KeyMods::Ctrl) == static_cast<std::uint8_t>(platform::KeyMods::Ctrl));
    static_assert(static_cast<std::uint8_t>(KeyMods::Shift) == static_cast<std::uint8_t>(platform::KeyMods::Shift));
    static_assert(static_cast<std::uint8_t>(KeyMods::Alt) == static_cast<std::uint8_t>(platform::KeyMods::Alt));
    static_assert(static_cast<std::uint8_t>(KeyMods::Super) == static_cast<std::uint8_t>(platform::KeyMods::Super));

    [[nodiscard]] constexpr Key           ToLumora(platform::Key key) noexcept { return static_cast<Key>(key); }
    [[nodiscard]] constexpr PointerButton ToLumora(platform::PointerButton button) noexcept
    {
        return static_cast<PointerButton>(button);
    }
    [[nodiscard]] constexpr KeyMods ToLumora(platform::KeyMods mods) noexcept { return static_cast<KeyMods>(mods); }

    [[nodiscard]] constexpr platform::Key           ToPlatform(Key key) noexcept { return static_cast<platform::Key>(key); }
    [[nodiscard]] constexpr platform::PointerButton ToPlatform(PointerButton button) noexcept
    {
        return static_cast<platform::PointerButton>(button);
    }
    [[nodiscard]] constexpr platform::KeyMods ToPlatform(KeyMods mods) noexcept
    {
        return static_cast<platform::KeyMods>(mods);
    }
} // namespace lumora
