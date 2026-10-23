#include <lumora/platform/Clipboard.h>
#include <lumora/platform/Window.h>

#include <emscripten.h>
#include <emscripten/html5.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lumora::platform
{
    namespace
    {
        constexpr const char* kCanvas = "#canvas";

        enum class WebText : int
        {
            Commit = 0,
            Paste  = 1,
        };

        std::string g_inbox;
        std::string g_clipboard;
        bool        g_sawPasteChord = false;

        Key CodeToKey(std::string_view c)
        {
            const auto offset = [](Key first, int n) { return static_cast<Key>(static_cast<uint16_t>(first) + n); };
            const auto digits = [](std::string_view s) {
                return !s.empty() && std::all_of(s.begin(), s.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
            };

            if (c.size() == 4 && c.starts_with("Key") && c[3] >= 'A' && c[3] <= 'Z') return offset(Key::A, c[3] - 'A');
            if (c.size() == 6 && c.starts_with("Digit") && digits(c.substr(5))) return offset(Key::N0, c[5] - '0');
            if (c.size() == 7 && c.starts_with("Numpad") && digits(c.substr(6))) return offset(Key::Num0, c[6] - '0');
            if (c.size() >= 2 && c.size() <= 3 && c[0] == 'F' && digits(c.substr(1)))
            {
                const int n = std::stoi(std::string(c.substr(1)));
                if (n >= 1 && n <= 24) return offset(Key::F1, n - 1);
            }

            // Windows reports both Enter keys as VK_RETURN, so NumpadEnter maps to Enter.
            static constexpr std::pair<std::string_view, Key> kNamed[] = {
                {"Backspace", Key::Backspace}, {"Enter", Key::Enter}, {"NumpadEnter", Key::Enter},
                {"Tab", Key::Tab}, {"Escape", Key::Escape}, {"Delete", Key::Delete}, {"Space", Key::Space},
                {"Insert", Key::Insert}, {"PrintScreen", Key::PrintScreen}, {"Pause", Key::Pause},
                {"ArrowLeft", Key::Left}, {"ArrowRight", Key::Right}, {"ArrowUp", Key::Up}, {"ArrowDown", Key::Down},
                {"Home", Key::Home}, {"End", Key::End}, {"PageUp", Key::PageUp}, {"PageDown", Key::PageDown},
                {"CapsLock", Key::CapsLock}, {"NumLock", Key::NumLock}, {"ScrollLock", Key::ScrollLock},
                {"ShiftLeft", Key::LeftShift}, {"ShiftRight", Key::RightShift},
                {"ControlLeft", Key::LeftCtrl}, {"ControlRight", Key::RightCtrl},
                {"AltLeft", Key::LeftAlt}, {"AltRight", Key::RightAlt},
                {"MetaLeft", Key::LeftSuper}, {"MetaRight", Key::RightSuper},
                {"OSLeft", Key::LeftSuper}, {"OSRight", Key::RightSuper},
                {"NumpadAdd", Key::NumAdd}, {"NumpadSubtract", Key::NumSub}, {"NumpadMultiply", Key::NumMul},
                {"NumpadDivide", Key::NumDiv}, {"NumpadDecimal", Key::NumDecimal},
                {"Backquote", Key::Grave}, {"Minus", Key::Minus}, {"Equal", Key::Equal},
                {"BracketLeft", Key::LeftBracket}, {"BracketRight", Key::RightBracket}, {"Backslash", Key::Backslash},
                {"Semicolon", Key::Semicolon}, {"Quote", Key::Apostrophe},
                {"Comma", Key::Comma}, {"Period", Key::Period}, {"Slash", Key::Slash},
            };
            for (const auto& [name, key] : kNamed)
                if (name == c) return key;
            return Key::Unknown;
        }

        bool IsFunctionKey(Key k) noexcept
        {
            return k >= Key::F1 && k <= Key::F24;
        }

        /// One printable code point, as KeyboardEvent.key gives for character keys ("a", "@", "e"); named keys are longer.
        bool IsPrintable(const char* key) noexcept
        {
            const auto lead = static_cast<unsigned char>(key[0]);
            if (lead < 0x20 || lead == 0x7F) return false;
            const std::size_t len = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            return std::strlen(key) == len;
        }

        template <typename E>
        KeyMods ModsOf(const E& e) noexcept
        {
            KeyMods mods = KeyMods::None;
            if (e.ctrlKey) mods |= KeyMods::Ctrl;
            if (e.shiftKey) mods |= KeyMods::Shift;
            if (e.altKey) mods |= KeyMods::Alt;
            if (e.metaKey) mods |= KeyMods::Super;
            return mods;
        }

        PointerButton ToButton(unsigned short b) noexcept
        {
            switch (b)
            {
            case 1:  return PointerButton::Middle;
            case 2:  return PointerButton::Right;
            case 3:  return PointerButton::X1;
            case 4:  return PointerButton::X2;
            default: return PointerButton::Left;
            }
        }

        EM_JS(void, web_canvas_rect, (double* out), {
            const c = document.querySelector("#canvas");
            const r = c ? c.getBoundingClientRect() : { left: 0, top: 0, width: 0, height: 0 };
            HEAPF64[(out >> 3) + 0] = r.left;
            HEAPF64[(out >> 3) + 1] = r.top;
            HEAPF64[(out >> 3) + 2] = r.width;
            HEAPF64[(out >> 3) + 3] = r.height;
        });

        // Hidden textarea for text/IME input (and the virtual keyboard on mobile), a context-menu blocker and the paste
        // listener. Text reaches C++ through lumora_web_inbox / lumora_web_on_text so no JS-side allocator is needed.
        EM_JS(void, web_install, (), {
            if (Module["lumoraTextArea"]) return;
            const canvas = document.querySelector("#canvas");
            if (canvas) canvas.addEventListener("contextmenu", (e) => e.preventDefault());

            const ta = document.createElement("textarea");
            ta.setAttribute("autocomplete", "off");
            ta.setAttribute("autocorrect", "off");
            ta.setAttribute("autocapitalize", "off");
            ta.setAttribute("spellcheck", "false");
            ta.setAttribute("aria-hidden", "true");
            ta.style.cssText = "position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;border:0;padding:0;margin:0;" +
                               "resize:none;outline:none;overflow:hidden;pointer-events:none;font-size:16px;";
            document.body.appendChild(ta);
            Module["lumoraTextArea"] = ta;

            const enc = new TextEncoder();
            const send = (kind, s) => {
                const bytes = enc.encode(s);
                HEAPU8.set(bytes, _lumora_web_inbox(bytes.length));
                _lumora_web_on_text(kind, bytes.length);
            };
            const commit = () => {
                const v = Array.from(ta.value).filter((ch) => ch.codePointAt(0) >= 32 && ch.codePointAt(0) !== 127).join("");
                ta.value = "";
                if (v) send(0, v);
            };

            let composing = false;
            ta.addEventListener("compositionstart", () => { composing = true; });
            ta.addEventListener("compositionend", () => { composing = false; commit(); });
            ta.addEventListener("input", (e) => { if (!composing && !e.isComposing) commit(); });
            // Soft keyboards send Backspace as an edit (keyCode 229), not a key; physical Backspace is consumed on keydown.
            ta.addEventListener("beforeinput", (e) => {
                if (!composing && e.inputType === "deleteContentBackward") {
                    e.preventDefault();
                    _lumora_web_on_backspace();
                }
            });
            document.addEventListener("paste", (e) => {
                e.preventDefault();
                send(1, e.clipboardData ? e.clipboardData.getData("text/plain") : "");
            });
        });

        EM_JS(void, web_text_focus, (int on), {
            const ta = Module["lumoraTextArea"];
            if (!ta) return;
            if (on) ta.focus({ preventScroll: true });
            else ta.blur();
        });

        EM_JS(int, web_text_focused, (), {
            const ta = Module["lumoraTextArea"];
            return ta && document.activeElement === ta ? 1 : 0;
        });

        EM_JS(void, web_clipboard_write, (const char* ptr, int len), {
            const s = new TextDecoder().decode(HEAPU8.slice(ptr, ptr + len));
            const fallback = () => {
                const prev = document.activeElement;
                const t = document.createElement("textarea");
                t.value = s;
                t.style.cssText = "position:fixed;left:0;top:0;opacity:0;";
                document.body.appendChild(t);
                t.select();
                try { document.execCommand("copy"); } catch (e) {}
                t.remove();
                if (prev && prev.focus) prev.focus({ preventScroll: true });
            };
            if (navigator.clipboard && navigator.clipboard.writeText) navigator.clipboard.writeText(s).catch(fallback);
            else fallback();
        });
    }

    class WebWindow final : public Window
    {
    public:
        explicit WebWindow(const WindowDesc& desc);
        ~WebWindow() override;

        void PumpEvents() override;
        void PumpMessages() override;
        void FlushEvents() override;

        [[nodiscard]] bool ShouldClose() const override { return _shouldClose; }
        void RequestClose() override { _shouldClose = true; }
        void ClearCloseRequest() override { _shouldClose = false; }

        [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;
        [[nodiscard]] uint32_t GetWidth() const override { return _width; }
        [[nodiscard]] uint32_t GetHeight() const override { return _height; }
        [[nodiscard]] float GetDpiScale() const override { return _dpiScale; }
        [[nodiscard]] FullscreenMode GetFullscreenMode() const override { return _fullscreenMode; }

        void SetTitle(std::string_view title) override;
        void SetFullscreenMode(FullscreenMode mode) override;
        void SetTextInputActive(bool active) override;

        void SetCursorMode(CursorMode mode, const CursorClip& clip) override;
        using Window::SetCursorMode;
        [[nodiscard]] CursorMode GetCursorMode() const override { return _cursorMode; }

        [[nodiscard]] ScreenRect GetClientRect() const override;
        void SetClientRect(const ScreenRect& /*rect*/) override {}
        [[nodiscard]] ScreenPoint ClientToScreen(float x, float y) const override;
        void Focus() override {}
        [[nodiscard]] bool IsHidden() const override;

        [[nodiscard]] InputVec2 LastPointer() const noexcept { return _lastPointerPos; }

        void OnText(WebText kind, std::string_view text);
        void PushKey(Key key, KeyMods mods);

    private:
        void Poll();
        [[nodiscard]] InputVec2 ToClient(double clientX, double clientY) const noexcept;
        void PushPointer(InputEventType type, const EmscriptenMouseEvent& e);

        static EM_BOOL OnMouse(int type, const EmscriptenMouseEvent* e, void* user);
        static EM_BOOL OnWheel(int type, const EmscriptenWheelEvent* e, void* user);
        static EM_BOOL OnKey(int type, const EmscriptenKeyboardEvent* e, void* user);
        static EM_BOOL OnTouch(int type, const EmscriptenTouchEvent* e, void* user);
        static EM_BOOL OnFocus(int type, const EmscriptenFocusEvent* e, void* user);
        static EM_BOOL OnFullscreen(int type, const EmscriptenFullscreenChangeEvent* e, void* user);
        static EM_BOOL OnPointerLock(int type, const EmscriptenPointerlockChangeEvent* e, void* user);

        uint32_t       _width          = 0;
        uint32_t       _height         = 0;
        float          _dpiScale       = 1.0f;
        double         _canvasLeft     = 0.0;
        double         _canvasTop      = 0.0;
        bool           _shouldClose    = false;
        bool           _textInputActive = false;
        FullscreenMode _fullscreenMode = FullscreenMode::Windowed;
        CursorMode     _cursorMode     = CursorMode::Normal;
        InputVec2      _lastPointerPos{};
        long           _primaryTouch   = -1;

        std::vector<WindowEvent> _pending;
        std::vector<InputEvent>  _pendingInput;
    };

    namespace
    {
        WebWindow* g_window = nullptr;
    }

    WebWindow::WebWindow(const WindowDesc& desc)
        : _width(desc.width), _height(desc.height)
    {
        g_window = this;
        web_install();
        SetTitle(desc.title);
        Poll();
        _pending.clear();

        emscripten_set_mousedown_callback(kCanvas, this, false, &WebWindow::OnMouse);
        emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnMouse);
        emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnMouse);
        emscripten_set_wheel_callback(kCanvas, this, false, &WebWindow::OnWheel);
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnKey);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnKey);
        emscripten_set_touchstart_callback(kCanvas, this, false, &WebWindow::OnTouch);
        emscripten_set_touchmove_callback(kCanvas, this, false, &WebWindow::OnTouch);
        emscripten_set_touchend_callback(kCanvas, this, false, &WebWindow::OnTouch);
        emscripten_set_touchcancel_callback(kCanvas, this, false, &WebWindow::OnTouch);
        emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnFocus);
        emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, false, &WebWindow::OnFocus);
        emscripten_set_fullscreenchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, false, &WebWindow::OnFullscreen);
        emscripten_set_pointerlockchange_callback(EMSCRIPTEN_EVENT_TARGET_DOCUMENT, this, false, &WebWindow::OnPointerLock);

        if (desc.fullscreenMode != FullscreenMode::Windowed)
            SetFullscreenMode(desc.fullscreenMode);
    }

    WebWindow::~WebWindow()
    {
        emscripten_html5_remove_all_event_listeners();
        if (g_window == this) g_window = nullptr;
    }

    void WebWindow::Poll()
    {
        double rect[4] = {};
        web_canvas_rect(rect);
        _canvasLeft = rect[0];
        _canvasTop = rect[1];

        const auto dpr = static_cast<float>(emscripten_get_device_pixel_ratio());
        if (rect[2] > 0.0 && rect[3] > 0.0)
        {
            const auto w = static_cast<uint32_t>(std::lround(rect[2] * dpr));
            const auto h = static_cast<uint32_t>(std::lround(rect[3] * dpr));
            if (w != _width || h != _height)
            {
                _width = w;
                _height = h;
                _pending.push_back({WindowEventType::Resize, w, h, dpr});
            }
        }

        int bw = 0;
        int bh = 0;
        emscripten_get_canvas_element_size(kCanvas, &bw, &bh);
        if (static_cast<uint32_t>(bw) != _width || static_cast<uint32_t>(bh) != _height)
            emscripten_set_canvas_element_size(kCanvas, static_cast<int>(_width), static_cast<int>(_height));

        if (dpr != _dpiScale)
        {
            _dpiScale = dpr;
            _pending.push_back({WindowEventType::DpiChanged, _width, _height, _dpiScale});
        }
    }

    void WebWindow::PumpEvents()
    {
        PumpMessages();
        FlushEvents();
    }

    void WebWindow::PumpMessages()
    {
        Poll();
    }

    void WebWindow::FlushEvents()
    {
        for (const WindowEvent& e : _pending)
            _events.Publish(e);
        _pending.clear();

        for (const InputEvent& e : _pendingInput)
            _input.Publish(e);
        _pendingInput.clear();
    }

    NativeWindowHandle WebWindow::GetNativeHandle() const
    {
        return NativeWindowHandle{
            .window = const_cast<char*>(kCanvas),
            .instance = nullptr,
            .backend = WindowBackend::Web,
        };
    }

    void WebWindow::SetTitle(std::string_view title)
    {
        emscripten_set_window_title(std::string(title).c_str());
    }

    void WebWindow::SetFullscreenMode(FullscreenMode mode)
    {
        if (mode == _fullscreenMode) return;
        // Deferred: the browser grants fullscreen only inside a user gesture, so this lands on the next input event.
        if (mode == FullscreenMode::Windowed)
            emscripten_exit_fullscreen();
        else
            emscripten_request_fullscreen(kCanvas, true);
        _fullscreenMode = mode;
    }

    void WebWindow::SetTextInputActive(bool active)
    {
        if (_textInputActive == active) return;
        _textInputActive = active;
        web_text_focus(active ? 1 : 0);
    }

    void WebWindow::SetCursorMode(CursorMode mode, const CursorClip& /*clip*/)
    {
        // Pointer Lock has no clip rect; the whole canvas captures.
        if (_cursorMode == mode) return;
        if (mode == CursorMode::Captured)
            emscripten_request_pointerlock(kCanvas, true);
        else
            emscripten_exit_pointerlock();
        _cursorMode = mode;
    }

    ScreenRect WebWindow::GetClientRect() const
    {
        return ScreenRect{
            .x = static_cast<int32_t>(std::lround(_canvasLeft * _dpiScale)),
            .y = static_cast<int32_t>(std::lround(_canvasTop * _dpiScale)),
            .width = static_cast<int32_t>(_width),
            .height = static_cast<int32_t>(_height),
        };
    }

    ScreenPoint WebWindow::ClientToScreen(float x, float y) const
    {
        const ScreenRect r = GetClientRect();
        return {r.x + static_cast<int32_t>(x), r.y + static_cast<int32_t>(y)};
    }

    bool WebWindow::IsHidden() const
    {
        EmscriptenVisibilityChangeEvent v{};
        return emscripten_get_visibility_status(&v) == EMSCRIPTEN_RESULT_SUCCESS && v.hidden;
    }

    InputVec2 WebWindow::ToClient(double clientX, double clientY) const noexcept
    {
        return {static_cast<float>((clientX - _canvasLeft) * _dpiScale),
                static_cast<float>((clientY - _canvasTop) * _dpiScale)};
    }

    void WebWindow::PushPointer(InputEventType type, const EmscriptenMouseEvent& e)
    {
        InputEvent ev{};
        ev.type = type;
        ev.position = ToClient(e.clientX, e.clientY);
        ev.button = ToButton(e.button);
        ev.mods = ModsOf(e);
        _lastPointerPos = ev.position;
        _pendingInput.push_back(ev);
    }

    void WebWindow::PushKey(Key key, KeyMods mods)
    {
        InputEvent ev{};
        ev.key = key;
        ev.mods = mods;
        ev.type = InputEventType::KeyDown;
        _pendingInput.push_back(ev);
        ev.type = InputEventType::KeyUp;
        _pendingInput.push_back(ev);
    }

    void WebWindow::OnText(WebText kind, std::string_view text)
    {
        if (kind == WebText::Paste)
        {
            g_clipboard.assign(text);
            // A context-menu or soft-keyboard paste has no key chord; synthesize one so the UI pulls the text.
            if (!g_sawPasteChord) PushKey(Key::V, KeyMods::Ctrl);
            g_sawPasteChord = false;
            return;
        }

        while (!text.empty())
        {
            std::size_t len = std::min<std::size_t>(text.size(), 31);
            while (len > 0 && len < text.size() && (static_cast<unsigned char>(text[len]) & 0xC0) == 0x80) --len;
            if (len == 0) len = 1;

            InputEvent ev{};
            ev.type = InputEventType::Text;
            std::memcpy(ev.text, text.data(), len);
            _pendingInput.push_back(ev);
            text.remove_prefix(len);
        }
    }

    EM_BOOL WebWindow::OnMouse(int type, const EmscriptenMouseEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        switch (type)
        {
        case EMSCRIPTEN_EVENT_MOUSEDOWN:
            self->PushPointer(InputEventType::PointerDown, *e);
            return true; // keeps the text area focused and stops text selection
        case EMSCRIPTEN_EVENT_MOUSEUP:
            self->PushPointer(InputEventType::PointerUp, *e);
            return e->button == 3 || e->button == 4; // back/forward would navigate the page
        case EMSCRIPTEN_EVENT_MOUSEMOVE:
            if (self->_cursorMode == CursorMode::Captured)
            {
                if (e->movementX == 0 && e->movementY == 0) return false;
                InputEvent ev{};
                ev.type = InputEventType::PointerMove;
                ev.position = self->_lastPointerPos;
                ev.delta = {static_cast<float>(e->movementX) * self->_dpiScale,
                            static_cast<float>(e->movementY) * self->_dpiScale};
                ev.mods = ModsOf(*e);
                self->_pendingInput.push_back(ev);
                return false;
            }
            {
                const InputVec2 last = self->_lastPointerPos;
                self->PushPointer(InputEventType::PointerMove, *e);
                InputEvent& ev = self->_pendingInput.back();
                ev.delta = {ev.position.x - last.x, ev.position.y - last.y};
            }
            return false;
        default:
            return false;
        }
    }

    EM_BOOL WebWindow::OnWheel(int /*type*/, const EmscriptenWheelEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        // Notches: ~100 px or 3 lines per detent; DOM +deltaY scrolls down, Lumora +y scrolls up.
        const double unit = e->deltaMode == DOM_DELTA_PIXEL ? 100.0 : e->deltaMode == DOM_DELTA_LINE ? 3.0 : 1.0;
        InputEvent ev{};
        ev.type = InputEventType::Scroll;
        ev.position = self->ToClient(e->mouse.clientX, e->mouse.clientY);
        ev.scrollDelta = {static_cast<float>(e->deltaX / unit), static_cast<float>(-e->deltaY / unit)};
        ev.mods = ModsOf(e->mouse);
        self->_pendingInput.push_back(ev);
        return true;
    }

    EM_BOOL WebWindow::OnKey(int type, const EmscriptenKeyboardEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        if (e->keyCode == 229) return false; // the IME owns this keystroke

        const Key key = CodeToKey(e->code);
        const KeyMods mods = ModsOf(*e);
        const bool chord = e->ctrlKey || e->metaKey;
        const bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;

        if (key == Key::V && chord) g_sawPasteChord = down;

        InputEvent ev{};
        ev.type = down ? InputEventType::KeyDown : InputEventType::KeyUp;
        ev.key = key;
        ev.mods = mods;
        self->_pendingInput.push_back(ev);

        // Browser shortcuts (F5, F12, Ctrl+R, Ctrl+C/V/X for the paste event...) pass through, except the chords apps
        // commonly bind that would open browser save/open/print/find/bookmark/history or select the page.
        if (IsFunctionKey(key)) return false;
        if (chord)
            return key == Key::S || key == Key::O || key == Key::P || key == Key::F || key == Key::G || key == Key::D ||
                   key == Key::H || key == Key::A;
        if (!down) return true;

        const bool printable = IsPrintable(e->key);
        const bool textFocused = web_text_focused() != 0;
        if (printable && textFocused) return false; // the text area turns it into an input event

        if (printable && (!e->altKey || e->ctrlKey))
        {
            InputEvent text{};
            text.type = InputEventType::Text;
            std::strncpy(text.text, e->key, sizeof(text.text) - 1);
            text.mods = mods;
            self->_pendingInput.push_back(text);
        }
        return true; // Tab focus moves, Backspace navigation, Space/arrow scrolling
    }

    EM_BOOL WebWindow::OnTouch(int type, const EmscriptenTouchEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        for (int i = 0; i < e->numTouches; ++i)
        {
            const EmscriptenTouchPoint& t = e->touches[i];
            if (!t.isChanged) continue;

            InputEvent ev{};
            ev.position = self->ToClient(t.clientX, t.clientY);
            ev.button = PointerButton::Left;

            if (type == EMSCRIPTEN_EVENT_TOUCHSTART)
            {
                if (self->_primaryTouch != -1) continue;
                self->_primaryTouch = t.identifier;
                ev.type = InputEventType::PointerDown;
            }
            else if (t.identifier != self->_primaryTouch)
            {
                continue;
            }
            else if (type == EMSCRIPTEN_EVENT_TOUCHMOVE)
            {
                ev.type = InputEventType::PointerMove;
                ev.delta = {ev.position.x - self->_lastPointerPos.x, ev.position.y - self->_lastPointerPos.y};
            }
            else
            {
                self->_primaryTouch = -1;
                ev.type = InputEventType::PointerUp;
                // Inside the gesture, so mobile browsers raise the soft keyboard for a field that is already active.
                if (self->_textInputActive) web_text_focus(1);
            }
            self->_lastPointerPos = ev.position;
            self->_pendingInput.push_back(ev);
        }
        return true; // no page scroll/zoom and no emulated mouse events
    }

    EM_BOOL WebWindow::OnFocus(int type, const EmscriptenFocusEvent* /*e*/, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        if (type == EMSCRIPTEN_EVENT_BLUR)
        {
            self->SetCursorMode(CursorMode::Normal);
            self->_pending.push_back({WindowEventType::FocusLost, self->_width, self->_height, self->_dpiScale});
        }
        else
        {
            self->_pending.push_back({WindowEventType::FocusGained, self->_width, self->_height, self->_dpiScale});
        }
        return false;
    }

    EM_BOOL WebWindow::OnFullscreen(int /*type*/, const EmscriptenFullscreenChangeEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        if (!e->isFullscreen) self->_fullscreenMode = FullscreenMode::Windowed;
        return false;
    }

    EM_BOOL WebWindow::OnPointerLock(int /*type*/, const EmscriptenPointerlockChangeEvent* e, void* user)
    {
        auto* self = static_cast<WebWindow*>(user);
        if (!e->isActive) self->_cursorMode = CursorMode::Normal;
        return false;
    }

    Ref<Window> Window::Create(const WindowDesc& desc)
    {
        return MakeRef<WebWindow>(desc);
    }

    void Window::PumpAll()
    {
        if (g_window) g_window->PumpEvents();
    }

    bool Window::AnyOpen()
    {
        return g_window != nullptr && !g_window->ShouldClose();
    }

    ScreenPoint Window::CursorScreenPosition()
    {
        if (!g_window) return {0, 0};
        const InputVec2 p = g_window->LastPointer();
        return g_window->ClientToScreen(p.x, p.y);
    }

    Window* Window::WindowAt(ScreenPoint point)
    {
        return g_window && g_window->GetClientRect().Contains(point) ? g_window : nullptr;
    }

    bool Window::IsOnAnyMonitor(const ScreenRect& /*rect*/)
    {
        return true;
    }

    ScreenRect Window::PrimaryWorkArea()
    {
        return g_window ? g_window->GetClientRect() : ScreenRect{0, 0, 1920, 1080};
    }

    // Browsers only hand out clipboard contents inside a paste event, so GetClipboardText returns the text of the latest
    // paste (or of our own last copy); reading the clipboard on demand is not possible.
    bool SetClipboardText(std::string_view text)
    {
        g_clipboard.assign(text);
        web_clipboard_write(g_clipboard.data(), static_cast<int>(g_clipboard.size()));
        return true;
    }

    std::string GetClipboardText()
    {
        return g_clipboard;
    }
}

extern "C"
{
    EMSCRIPTEN_KEEPALIVE char* lumora_web_inbox(int size)
    {
        lumora::platform::g_inbox.resize(static_cast<std::size_t>(size));
        return lumora::platform::g_inbox.data();
    }

    EMSCRIPTEN_KEEPALIVE void lumora_web_on_text(int kind, int size)
    {
        using namespace lumora::platform;
        if (g_window)
            g_window->OnText(static_cast<WebText>(kind), std::string_view(g_inbox.data(), static_cast<std::size_t>(size)));
    }

    EMSCRIPTEN_KEEPALIVE void lumora_web_on_backspace()
    {
        using namespace lumora::platform;
        if (g_window) g_window->PushKey(Key::Backspace, KeyMods::None);
    }
}
