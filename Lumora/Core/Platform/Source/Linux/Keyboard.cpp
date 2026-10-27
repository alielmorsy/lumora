#include "Keyboard.h"

#include <linux/input-event-codes.h>
#include <xkbcommon/xkbcommon-compose.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xkbcommon/xkbcommon.h>

#include <clocale>
#include <cstdlib>
#include <cstring>

namespace lumora::platform
{
    namespace
    {
        Key KeyFromKeysym(const xkb_keysym_t sym)
        {
            if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_a));
            if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z) return static_cast<Key>(static_cast<int>(Key::A) + (sym - XKB_KEY_A));
            if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) return static_cast<Key>(static_cast<int>(Key::N0) + (sym - XKB_KEY_0));
            if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F24) return static_cast<Key>(static_cast<int>(Key::F1) + (sym - XKB_KEY_F1));

            switch (sym)
            {
            case XKB_KEY_BackSpace:    return Key::Backspace;
            case XKB_KEY_Return:       return Key::Enter;
            case XKB_KEY_Tab:
            case XKB_KEY_ISO_Left_Tab: return Key::Tab;
            case XKB_KEY_Escape:       return Key::Escape;
            case XKB_KEY_Delete:       return Key::Delete;
            case XKB_KEY_space:        return Key::Space;
            case XKB_KEY_Insert:       return Key::Insert;
            case XKB_KEY_Print:        return Key::PrintScreen;
            case XKB_KEY_Pause:        return Key::Pause;

            case XKB_KEY_Left:         return Key::Left;
            case XKB_KEY_Right:        return Key::Right;
            case XKB_KEY_Up:           return Key::Up;
            case XKB_KEY_Down:         return Key::Down;
            case XKB_KEY_Home:         return Key::Home;
            case XKB_KEY_End:          return Key::End;
            case XKB_KEY_Page_Up:      return Key::PageUp;
            case XKB_KEY_Page_Down:    return Key::PageDown;

            case XKB_KEY_Caps_Lock:    return Key::CapsLock;
            case XKB_KEY_Num_Lock:     return Key::NumLock;
            case XKB_KEY_Scroll_Lock:  return Key::ScrollLock;

            case XKB_KEY_Shift_L:      return Key::LeftShift;
            case XKB_KEY_Shift_R:      return Key::RightShift;
            case XKB_KEY_Control_L:    return Key::LeftCtrl;
            case XKB_KEY_Control_R:    return Key::RightCtrl;
            case XKB_KEY_Alt_L:
            case XKB_KEY_Meta_L:       return Key::LeftAlt;
            case XKB_KEY_Alt_R:
            case XKB_KEY_Meta_R:
            case XKB_KEY_ISO_Level3_Shift: return Key::RightAlt;
            case XKB_KEY_Super_L:      return Key::LeftSuper;
            case XKB_KEY_Super_R:      return Key::RightSuper;

            case XKB_KEY_grave:        return Key::Grave;
            case XKB_KEY_minus:        return Key::Minus;
            case XKB_KEY_equal:        return Key::Equal;
            case XKB_KEY_bracketleft:  return Key::LeftBracket;
            case XKB_KEY_bracketright: return Key::RightBracket;
            case XKB_KEY_backslash:    return Key::Backslash;
            case XKB_KEY_semicolon:    return Key::Semicolon;
            case XKB_KEY_apostrophe:   return Key::Apostrophe;
            case XKB_KEY_comma:        return Key::Comma;
            case XKB_KEY_period:       return Key::Period;
            case XKB_KEY_slash:        return Key::Slash;

            default: return Key::Unknown;
            }
        }

        bool IsKeypad(const uint32_t code)
        {
            return (code >= KEY_KP7 && code <= KEY_KPDOT) || code == KEY_KPENTER || code == KEY_KPSLASH ||
                   code == KEY_KPASTERISK;
        }

        bool IsControl(const uint32_t codepoint) { return codepoint < 0x20 || codepoint == 0x7F; }

        void CopyText(char (&dst)[32], const char* src)
        {
            std::strncpy(dst, src, sizeof(dst) - 1);
            dst[sizeof(dst) - 1] = '\0';
        }
    }

    Key KeyFromEvdev(const uint32_t code)
    {
        switch (code)
        {
        case KEY_ESC:       return Key::Escape;
        case KEY_1: return Key::N1; case KEY_2: return Key::N2; case KEY_3: return Key::N3;
        case KEY_4: return Key::N4; case KEY_5: return Key::N5; case KEY_6: return Key::N6;
        case KEY_7: return Key::N7; case KEY_8: return Key::N8; case KEY_9: return Key::N9;
        case KEY_0: return Key::N0;
        case KEY_MINUS:     return Key::Minus;
        case KEY_EQUAL:     return Key::Equal;
        case KEY_BACKSPACE: return Key::Backspace;
        case KEY_TAB:       return Key::Tab;
        case KEY_Q: return Key::Q; case KEY_W: return Key::W; case KEY_E: return Key::E;
        case KEY_R: return Key::R; case KEY_T: return Key::T; case KEY_Y: return Key::Y;
        case KEY_U: return Key::U; case KEY_I: return Key::I; case KEY_O: return Key::O;
        case KEY_P: return Key::P;
        case KEY_LEFTBRACE:  return Key::LeftBracket;
        case KEY_RIGHTBRACE: return Key::RightBracket;
        case KEY_ENTER:      return Key::Enter;
        case KEY_LEFTCTRL:   return Key::LeftCtrl;
        case KEY_A: return Key::A; case KEY_S: return Key::S; case KEY_D: return Key::D;
        case KEY_F: return Key::F; case KEY_G: return Key::G; case KEY_H: return Key::H;
        case KEY_J: return Key::J; case KEY_K: return Key::K; case KEY_L: return Key::L;
        case KEY_SEMICOLON:  return Key::Semicolon;
        case KEY_APOSTROPHE: return Key::Apostrophe;
        case KEY_GRAVE:      return Key::Grave;
        case KEY_LEFTSHIFT:  return Key::LeftShift;
        case KEY_BACKSLASH:  return Key::Backslash;
        case KEY_Z: return Key::Z; case KEY_X: return Key::X; case KEY_C: return Key::C;
        case KEY_V: return Key::V; case KEY_B: return Key::B; case KEY_N: return Key::N;
        case KEY_M: return Key::M;
        case KEY_COMMA:      return Key::Comma;
        case KEY_DOT:        return Key::Period;
        case KEY_SLASH:      return Key::Slash;
        case KEY_RIGHTSHIFT: return Key::RightShift;
        case KEY_LEFTALT:    return Key::LeftAlt;
        case KEY_SPACE:      return Key::Space;
        case KEY_CAPSLOCK:   return Key::CapsLock;
        case KEY_F1:  return Key::F1;  case KEY_F2:  return Key::F2;  case KEY_F3:  return Key::F3;
        case KEY_F4:  return Key::F4;  case KEY_F5:  return Key::F5;  case KEY_F6:  return Key::F6;
        case KEY_F7:  return Key::F7;  case KEY_F8:  return Key::F8;  case KEY_F9:  return Key::F9;
        case KEY_F10: return Key::F10; case KEY_F11: return Key::F11; case KEY_F12: return Key::F12;
        case KEY_NUMLOCK:    return Key::NumLock;
        case KEY_SCROLLLOCK: return Key::ScrollLock;

        case KEY_KP0: return Key::Num0; case KEY_KP1: return Key::Num1; case KEY_KP2: return Key::Num2;
        case KEY_KP3: return Key::Num3; case KEY_KP4: return Key::Num4; case KEY_KP5: return Key::Num5;
        case KEY_KP6: return Key::Num6; case KEY_KP7: return Key::Num7; case KEY_KP8: return Key::Num8;
        case KEY_KP9: return Key::Num9;
        case KEY_KPPLUS:     return Key::NumAdd;
        case KEY_KPMINUS:    return Key::NumSub;
        case KEY_KPASTERISK: return Key::NumMul;
        case KEY_KPSLASH:    return Key::NumDiv;
        case KEY_KPDOT:      return Key::NumDecimal;
        case KEY_KPENTER:    return Key::NumEnter;

        case KEY_RIGHTCTRL:  return Key::RightCtrl;
        case KEY_SYSRQ:      return Key::PrintScreen;
        case KEY_RIGHTALT:   return Key::RightAlt;
        case KEY_HOME:       return Key::Home;
        case KEY_UP:         return Key::Up;
        case KEY_PAGEUP:     return Key::PageUp;
        case KEY_LEFT:       return Key::Left;
        case KEY_RIGHT:      return Key::Right;
        case KEY_END:        return Key::End;
        case KEY_DOWN:       return Key::Down;
        case KEY_PAGEDOWN:   return Key::PageDown;
        case KEY_INSERT:     return Key::Insert;
        case KEY_DELETE:     return Key::Delete;
        case KEY_PAUSE:      return Key::Pause;
        case KEY_LEFTMETA:   return Key::LeftSuper;
        case KEY_RIGHTMETA:  return Key::RightSuper;

        default:
            if (code >= KEY_F13 && code <= KEY_F24)
                return static_cast<Key>(static_cast<int>(Key::F13) + (code - KEY_F13));
            return Key::Unknown;
        }
    }

    Keyboard::Keyboard()
    {
        _context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);

        const char* locale = std::getenv("LC_ALL");
        if (!locale || !*locale) locale = std::getenv("LC_CTYPE");
        if (!locale || !*locale) locale = std::getenv("LANG");
        if (!locale || !*locale) locale = "C";

        if (xkb_compose_table* table = xkb_compose_table_new_from_locale(_context, locale, XKB_COMPOSE_COMPILE_NO_FLAGS))
        {
            _compose = xkb_compose_state_new(table, XKB_COMPOSE_STATE_NO_FLAGS);
            xkb_compose_table_unref(table);
        }
    }

    Keyboard::~Keyboard()
    {
        if (_compose) xkb_compose_state_unref(_compose);
        if (_state) xkb_state_unref(_state);
        if (_keymap) xkb_keymap_unref(_keymap);
        if (_context) xkb_context_unref(_context);
    }

    void Keyboard::SetKeymap(xkb_keymap* keymap)
    {
        if (!keymap) return;

        xkb_state* state = xkb_state_new(keymap);
        if (_state)
        {
            xkb_state_update_mask(state, xkb_state_serialize_mods(_state, XKB_STATE_MODS_DEPRESSED),
                                  xkb_state_serialize_mods(_state, XKB_STATE_MODS_LATCHED),
                                  xkb_state_serialize_mods(_state, XKB_STATE_MODS_LOCKED), 0, 0,
                                  xkb_state_serialize_layout(_state, XKB_STATE_LAYOUT_LOCKED));
            xkb_state_unref(_state);
        }
        if (_keymap) xkb_keymap_unref(_keymap);

        _keymap = keymap;
        _state  = state;
    }

    void Keyboard::UpdateMask(const uint32_t depressed, const uint32_t latched, const uint32_t locked,
                              const uint32_t depressedGroup, const uint32_t latchedGroup, const uint32_t lockedGroup)
    {
        if (_state)
            xkb_state_update_mask(_state, depressed, latched, locked, depressedGroup, latchedGroup, lockedGroup);
    }

    KeyMods Keyboard::Mods() const
    {
        KeyMods mods = KeyMods::None;
        if (!_state) return mods;

        const auto active = [this](const char* name)
        {
            return xkb_state_mod_name_is_active(_state, name, XKB_STATE_MODS_EFFECTIVE) > 0;
        };
        if (active(XKB_MOD_NAME_CTRL)) mods |= KeyMods::Ctrl;
        if (active(XKB_MOD_NAME_SHIFT)) mods |= KeyMods::Shift;
        if (active(XKB_MOD_NAME_ALT)) mods |= KeyMods::Alt;
        if (active(XKB_MOD_NAME_LOGO)) mods |= KeyMods::Super;
        return mods;
    }

    bool Keyboard::Repeats(const uint32_t evdevCode) const
    {
        return _keymap && xkb_keymap_key_repeats(_keymap, evdevCode + 8) != 0;
    }

    void Keyboard::ResetCompose()
    {
        if (_compose) xkb_compose_state_reset(_compose);
    }

    Key Keyboard::KeyFor(const uint32_t evdevCode) const
    {
        if (IsKeypad(evdevCode)) return KeyFromEvdev(evdevCode);

        if (_state && _keymap)
        {
            // The level-0 symbol, so Shift+1 still reads as the 1 key; only a Latin base symbol is trusted (Dvorak and
            // AZERTY follow the layout, Cyrillic or Arabic fall through to the physical position).
            const xkb_keycode_t code   = evdevCode + 8;
            const xkb_layout_index_t layout = xkb_state_key_get_layout(_state, code);
            const xkb_keysym_t* syms = nullptr;
            if (xkb_keymap_key_get_syms_by_level(_keymap, code, layout, 0, &syms) > 0)
            {
                const Key key = KeyFromKeysym(syms[0]);
                if (key != Key::Unknown) return key;
            }
        }
        return KeyFromEvdev(evdevCode);
    }

    KeyTranslation Keyboard::Press(const uint32_t evdevCode)
    {
        KeyTranslation out;
        out.key  = KeyFor(evdevCode);
        out.mods = Mods();
        if (!_state) return out;

        // Alt combinations are commands, not text (WM_SYSCHAR on Windows); Ctrl ones produce control characters.
        if (Has(out.mods, KeyMods::Alt) || Has(out.mods, KeyMods::Super)) return out;

        const xkb_keycode_t code = evdevCode + 8;
        const xkb_keysym_t  sym  = xkb_state_key_get_one_sym(_state, code);

        if (_compose && sym != XKB_KEY_NoSymbol)
        {
            xkb_compose_state_feed(_compose, sym);
            switch (xkb_compose_state_get_status(_compose))
            {
            case XKB_COMPOSE_COMPOSING:
                return out;
            case XKB_COMPOSE_COMPOSED:
            {
                char buffer[32];
                xkb_compose_state_get_utf8(_compose, buffer, sizeof(buffer));
                xkb_compose_state_reset(_compose);
                CopyText(out.text, buffer);
                return out;
            }
            case XKB_COMPOSE_CANCELLED:
                xkb_compose_state_reset(_compose);
                return out;
            case XKB_COMPOSE_NOTHING:
                break;
            }
        }

        char buffer[32];
        const int length = xkb_state_key_get_utf8(_state, code, buffer, sizeof(buffer));
        if (length > 0)
        {
            const unsigned char first = static_cast<unsigned char>(buffer[0]);
            if (!(length == 1 && IsControl(first))) CopyText(out.text, buffer);
        }
        return out;
    }

    KeyTranslation Keyboard::Release(const uint32_t evdevCode)
    {
        KeyTranslation out;
        out.key  = KeyFor(evdevCode);
        out.mods = Mods();
        return out;
    }
}
