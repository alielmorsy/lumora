#pragma once

#include "lumora/platform/InputEvents.h"

#include <cstdint>

struct xkb_compose_state;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace lumora::platform
{
    struct KeyTranslation
    {
        Key     key  = Key::Unknown;
        KeyMods mods = KeyMods::None;
        char    text[32] = {};   ///< NUL-terminated UTF-8, empty when the press produces no text
    };

    /// xkbcommon keyboard state shared by both backends. The server is the authority on modifiers: feed it UpdateMask()
    /// (Wayland wl_keyboard.modifiers, X11 XkbStateNotify) and never per-key updates.
    class Keyboard
    {
    public:
        Keyboard();
        ~Keyboard();

        Keyboard(const Keyboard&)            = delete;
        Keyboard& operator=(const Keyboard&) = delete;

        [[nodiscard]] xkb_context* Context() const noexcept { return _context; }

        /// Takes ownership of `keymap` (null is ignored) and rebuilds the state; held modifiers are re-applied.
        void SetKeymap(xkb_keymap* keymap);

        void UpdateMask(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t depressedGroup,
                        uint32_t latchedGroup, uint32_t lockedGroup);

        /// `evdevCode` is a Linux input code (an X11 keycode minus 8). A repeat is a second Press of the same code.
        [[nodiscard]] KeyTranslation Press(uint32_t evdevCode);
        [[nodiscard]] KeyTranslation Release(uint32_t evdevCode);

        [[nodiscard]] KeyMods Mods() const;

        /// Whether holding this key should auto-repeat (the Wayland client has to do it itself).
        [[nodiscard]] bool Repeats(uint32_t evdevCode) const;

        /// Drops any half-finished compose sequence (focus loss).
        void ResetCompose();

    private:
        [[nodiscard]] Key KeyFor(uint32_t evdevCode) const;

        xkb_context*       _context = nullptr;
        xkb_keymap*        _keymap  = nullptr;
        xkb_state*         _state   = nullptr;
        xkb_compose_state* _compose = nullptr;
    };

    /// Physical-position fallback (US layout), used for keypad keys and layouts whose base symbols are not Latin.
    [[nodiscard]] Key KeyFromEvdev(uint32_t evdevCode);
}
