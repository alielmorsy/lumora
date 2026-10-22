#include "AndroidWindow.h"

#include <android/configuration.h>
#include <android/keycodes.h>

#include <algorithm>
#include <cstring>

namespace lumora::platform
{
    namespace
    {
        AndroidWindow* g_currentWindow = nullptr;

        Key MapAndroidKeyCode(int32_t keyCode)
        {
            switch (keyCode)
            {
                case AKEYCODE_BACK:           return Key::Escape;
                case AKEYCODE_ESCAPE:         return Key::Escape;
                case AKEYCODE_DEL:            return Key::Backspace;
                case AKEYCODE_ENTER:          return Key::Enter;
                case AKEYCODE_TAB:            return Key::Tab;
                case AKEYCODE_SPACE:          return Key::Space;
                case AKEYCODE_FORWARD_DEL:    return Key::Delete;
                case AKEYCODE_DPAD_UP:        return Key::Up;
                case AKEYCODE_DPAD_DOWN:      return Key::Down;
                case AKEYCODE_DPAD_LEFT:      return Key::Left;
                case AKEYCODE_DPAD_RIGHT:     return Key::Right;
                case AKEYCODE_MOVE_HOME:      return Key::Home;
                case AKEYCODE_MOVE_END:       return Key::End;
                case AKEYCODE_PAGE_UP:        return Key::PageUp;
                case AKEYCODE_PAGE_DOWN:      return Key::PageDown;
                case AKEYCODE_SHIFT_LEFT:     return Key::LeftShift;
                case AKEYCODE_SHIFT_RIGHT:    return Key::RightShift;
                case AKEYCODE_CTRL_LEFT:      return Key::LeftCtrl;
                case AKEYCODE_CTRL_RIGHT:     return Key::RightCtrl;
                case AKEYCODE_ALT_LEFT:       return Key::LeftAlt;
                case AKEYCODE_ALT_RIGHT:      return Key::RightAlt;
                case AKEYCODE_CAPS_LOCK:      return Key::CapsLock;
                case AKEYCODE_NUM_LOCK:       return Key::NumLock;
                case AKEYCODE_SCROLL_LOCK:    return Key::ScrollLock;
                case AKEYCODE_A:              return Key::A;
                case AKEYCODE_B:              return Key::B;
                case AKEYCODE_C:              return Key::C;
                case AKEYCODE_D:              return Key::D;
                case AKEYCODE_E:              return Key::E;
                case AKEYCODE_F:              return Key::F;
                case AKEYCODE_G:              return Key::G;
                case AKEYCODE_H:              return Key::H;
                case AKEYCODE_I:              return Key::I;
                case AKEYCODE_J:              return Key::J;
                case AKEYCODE_K:              return Key::K;
                case AKEYCODE_L:              return Key::L;
                case AKEYCODE_M:              return Key::M;
                case AKEYCODE_N:              return Key::N;
                case AKEYCODE_O:              return Key::O;
                case AKEYCODE_P:              return Key::P;
                case AKEYCODE_Q:              return Key::Q;
                case AKEYCODE_R:              return Key::R;
                case AKEYCODE_S:              return Key::S;
                case AKEYCODE_T:              return Key::T;
                case AKEYCODE_U:              return Key::U;
                case AKEYCODE_V:              return Key::V;
                case AKEYCODE_W:              return Key::W;
                case AKEYCODE_X:              return Key::X;
                case AKEYCODE_Y:              return Key::Y;
                case AKEYCODE_Z:              return Key::Z;
                case AKEYCODE_0:              return Key::N0;
                case AKEYCODE_1:              return Key::N1;
                case AKEYCODE_2:              return Key::N2;
                case AKEYCODE_3:              return Key::N3;
                case AKEYCODE_4:              return Key::N4;
                case AKEYCODE_5:              return Key::N5;
                case AKEYCODE_6:              return Key::N6;
                case AKEYCODE_7:              return Key::N7;
                case AKEYCODE_8:              return Key::N8;
                case AKEYCODE_9:              return Key::N9;
                case AKEYCODE_NUMPAD_0:       return Key::Num0;
                case AKEYCODE_NUMPAD_1:       return Key::Num1;
                case AKEYCODE_NUMPAD_2:       return Key::Num2;
                case AKEYCODE_NUMPAD_3:       return Key::Num3;
                case AKEYCODE_NUMPAD_4:       return Key::Num4;
                case AKEYCODE_NUMPAD_5:       return Key::Num5;
                case AKEYCODE_NUMPAD_6:       return Key::Num6;
                case AKEYCODE_NUMPAD_7:       return Key::Num7;
                case AKEYCODE_NUMPAD_8:       return Key::Num8;
                case AKEYCODE_NUMPAD_9:       return Key::Num9;
                case AKEYCODE_NUMPAD_ADD:      return Key::NumAdd;
                case AKEYCODE_NUMPAD_SUBTRACT: return Key::NumSub;
                case AKEYCODE_NUMPAD_MULTIPLY: return Key::NumMul;
                case AKEYCODE_NUMPAD_DIVIDE:   return Key::NumDiv;
                case AKEYCODE_NUMPAD_DOT:      return Key::NumDecimal;
                case AKEYCODE_NUMPAD_ENTER:    return Key::NumEnter;
                case AKEYCODE_GRAVE:          return Key::Grave;
                case AKEYCODE_MINUS:          return Key::Minus;
                case AKEYCODE_EQUALS:         return Key::Equal;
                case AKEYCODE_LEFT_BRACKET:   return Key::LeftBracket;
                case AKEYCODE_RIGHT_BRACKET:  return Key::RightBracket;
                case AKEYCODE_BACKSLASH:      return Key::Backslash;
                case AKEYCODE_SEMICOLON:      return Key::Semicolon;
                case AKEYCODE_APOSTROPHE:     return Key::Apostrophe;
                case AKEYCODE_COMMA:          return Key::Comma;
                case AKEYCODE_PERIOD:         return Key::Period;
                case AKEYCODE_SLASH:          return Key::Slash;
                default:                      return Key::Unknown;
            }
        }

        void HandleAppCmd(android_app* app, int32_t cmd)
        {
            auto* self = static_cast<AndroidWindow*>(app->userData);
            if (!self) return;

            switch (cmd)
            {
                case APP_CMD_INIT_WINDOW:
                    self->OnInitWindow(app->window);
                    break;
                case APP_CMD_TERM_WINDOW:
                    self->OnTermWindow();
                    break;
                case APP_CMD_GAINED_FOCUS:
                    self->OnFocusChanged(true);
                    break;
                case APP_CMD_LOST_FOCUS:
                    self->OnFocusChanged(false);
                    break;
                case APP_CMD_CONFIG_CHANGED:
                    self->OnConfigChanged();
                    break;
                case APP_CMD_PAUSE:
                    self->OnPause();
                    break;
                case APP_CMD_RESUME:
                    self->OnResume();
                    break;
                case APP_CMD_DESTROY:
                    self->RequestClose();
                    break;
                default:
                    break;
            }
        }
    }

    AndroidWindow::AndroidWindow(const WindowDesc& desc)
        : _fullscreenMode(desc.fullscreenMode)
    {
        g_currentWindow = this;
        android_app* app = android::App();
        if (app)
        {
            app->userData = this;
            app->onAppCmd = HandleAppCmd;
            UpdateDpi();

            while (!_window && !app->destroyRequested)
            {
                int events = 0;
                android_poll_source* source = nullptr;
                if (ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
                {
                    if (source)
                    {
                        source->process(app, source);
                    }
                }
            }
        }
    }

    AndroidWindow::~AndroidWindow()
    {
        android_app* app = android::App();
        if (app && app->userData == this)
        {
            app->userData = nullptr;
            app->onAppCmd = nullptr;
        }
        if (g_currentWindow == this)
        {
            g_currentWindow = nullptr;
        }
    }

    void AndroidWindow::OnInitWindow(ANativeWindow* window)
    {
        _window = window;
        if (_window)
        {
            _width = static_cast<uint32_t>(ANativeWindow_getWidth(_window));
            _height = static_cast<uint32_t>(ANativeWindow_getHeight(_window));
        }
        UpdateDpi();
        _pendingEvents.push_back(WindowEvent{
            .type = WindowEventType::Resize,
            .width = _width,
            .height = _height,
            .dpiScale = _dpiScale,
        });
    }

    void AndroidWindow::OnTermWindow()
    {
        _window = nullptr;
        _width = 0;
        _height = 0;
        _pendingEvents.push_back(WindowEvent{
            .type = WindowEventType::Resize,
            .width = 0,
            .height = 0,
            .dpiScale = _dpiScale,
        });
    }

    void AndroidWindow::OnFocusChanged(bool focused)
    {
        _focused = focused;
        _pendingEvents.push_back(WindowEvent{
            .type = focused ? WindowEventType::FocusGained : WindowEventType::FocusLost,
            .width = _width,
            .height = _height,
            .dpiScale = _dpiScale,
        });
    }

    void AndroidWindow::OnConfigChanged()
    {
        UpdateDpi();
        if (_window)
        {
            const auto w = static_cast<uint32_t>(ANativeWindow_getWidth(_window));
            const auto h = static_cast<uint32_t>(ANativeWindow_getHeight(_window));
            if (w != _width || h != _height)
            {
                _width = w;
                _height = h;
                _pendingEvents.push_back(WindowEvent{
                    .type = WindowEventType::Resize,
                    .width = _width,
                    .height = _height,
                    .dpiScale = _dpiScale,
                });
            }
        }
    }

    void AndroidWindow::OnPause()
    {
        _paused = true;
    }

    void AndroidWindow::OnResume()
    {
        _paused = false;
    }

    void AndroidWindow::UpdateDpi()
    {
        android_app* app = android::App();
        if (app && app->config)
        {
            const int32_t density = AConfiguration_getDensity(app->config);
            if (density > 0)
            {
                _dpiScale = static_cast<float>(density) / 160.0f;
            }
        }
    }

    NativeWindowHandle AndroidWindow::GetNativeHandle() const noexcept
    {
        return NativeWindowHandle{
            .window = _window,
            .instance = nullptr,
            .backend = WindowBackend::Android,
        };
    }

    void AndroidWindow::SetTitle([[maybe_unused]] std::string_view title)
    {
    }

    void AndroidWindow::SetFullscreenMode(FullscreenMode mode)
    {
        _fullscreenMode = mode;
    }

    void AndroidWindow::SetCursorMode([[maybe_unused]] CursorMode mode,
                                      [[maybe_unused]] const CursorClip& clip)
    {
    }

    ScreenRect AndroidWindow::GetClientRect() const noexcept
    {
        return ScreenRect{
            .x = 0,
            .y = 0,
            .width = static_cast<int32_t>(_width),
            .height = static_cast<int32_t>(_height),
        };
    }

    void AndroidWindow::SetClientRect([[maybe_unused]] const ScreenRect& rect)
    {
    }

    ScreenPoint AndroidWindow::ClientToScreen(float x, float y) const noexcept
    {
        return ScreenPoint{static_cast<int32_t>(x), static_cast<int32_t>(y)};
    }

    void AndroidWindow::Focus()
    {
    }

    bool AndroidWindow::IsHidden() const noexcept
    {
        return _window == nullptr || _paused;
    }

    void AndroidWindow::SetTextInputActive(bool active)
    {
        if (_textInputActive == active) return;
        _textInputActive = active;

        android_app* app = android::App();
        if (!app || !app->activity) return;

        if (active)
        {
            GameActivity_showSoftInput(app->activity, 0);
        }
        else
        {
            GameActivity_hideSoftInput(app->activity, 0);
        }
    }

    void AndroidWindow::ProcessInputEvents()
    {
        android_app* app = android::App();
        if (!app) return;

        android_input_buffer* inputBuffer = android_app_swap_input_buffers(app);
        if (!inputBuffer) return;

        for (size_t i = 0; i < inputBuffer->motionEventsCount; ++i)
        {
            const GameActivityMotionEvent* motion = &inputBuffer->motionEvents[i];
            const int action = motion->action & AMOTION_EVENT_ACTION_MASK;
            const int pointerIdx = (motion->action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

            if (pointerIdx == 0 && motion->pointerCount > 0)
            {
                const float x = GameActivityPointerAxes_getX(&motion->pointers[0]);
                const float y = GameActivityPointerAxes_getY(&motion->pointers[0]);
                InputEvent ev{};
                ev.position = {x, y};
                ev.button = PointerButton::Left;

                if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
                {
                    ev.type = InputEventType::PointerDown;
                    _lastTouchPos = {x, y};
                    _pendingInput.push_back(ev);
                }
                else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP || action == AMOTION_EVENT_ACTION_CANCEL)
                {
                    ev.type = InputEventType::PointerUp;
                    _lastTouchPos = {x, y};
                    _pendingInput.push_back(ev);
                }
                else if (action == AMOTION_EVENT_ACTION_MOVE)
                {
                    ev.type = InputEventType::PointerMove;
                    ev.delta = {x - _lastTouchPos.x, y - _lastTouchPos.y};
                    _lastTouchPos = {x, y};
                    _pendingInput.push_back(ev);
                }
            }
        }
        android_app_clear_motion_events(inputBuffer);

        for (size_t i = 0; i < inputBuffer->keyEventsCount; ++i)
        {
            const GameActivityKeyEvent* key = &inputBuffer->keyEvents[i];
            const Key mapped = MapAndroidKeyCode(key->keyCode);
            if (mapped != Key::Unknown)
            {
                InputEvent ev{};
                ev.key = mapped;
                if (key->action == AKEY_EVENT_ACTION_DOWN)
                {
                    ev.type = InputEventType::KeyDown;
                    _pendingInput.push_back(ev);
                }
                else if (key->action == AKEY_EVENT_ACTION_UP)
                {
                    ev.type = InputEventType::KeyUp;
                    _pendingInput.push_back(ev);
                }
            }
        }
        android_app_clear_key_events(inputBuffer);
    }

    void AndroidWindow::ProcessTextInput()
    {
        android_app* app = android::App();
        if (!app || !app->textInputState) return;

        GameActivity_getTextInputState(app->activity, [](void* ctx, const GameTextInputState* state) {
            auto* self = static_cast<AndroidWindow*>(ctx);
            if (state && state->text_UTF8 && state->text_length > 0)
            {
                const char* ptr = state->text_UTF8;
                int remaining = state->text_length;
                while (remaining > 0)
                {
                    int chunkLen = std::min(remaining, 31);
                    while (chunkLen > 0 && (static_cast<unsigned char>(ptr[chunkLen]) & 0xC0) == 0x80)
                    {
                        --chunkLen;
                    }
                    if (chunkLen == 0) chunkLen = 1;

                    InputEvent ev{};
                    ev.type = InputEventType::Text;
                    std::memcpy(ev.text, ptr, static_cast<size_t>(chunkLen));
                    ev.text[chunkLen] = '\0';
                    self->PendingInput().push_back(ev);

                    ptr += chunkLen;
                    remaining -= chunkLen;
                }

                GameTextInputState emptyState{};
                emptyState.text_UTF8 = "";
                emptyState.text_length = 0;
                emptyState.selection = {0, 0};
                emptyState.composingRegion = {-1, -1};
                android_app* a = android::App();
                if (a && a->activity)
                {
                    GameActivity_setTextInputState(a->activity, &emptyState);
                }
            }
        }, this);
        app->textInputState = 0;
    }

    void AndroidWindow::PumpMessages()
    {
        android_app* app = android::App();
        if (!app) return;

        int timeout = (_paused || !_window) ? -1 : 0;
        int events = 0;
        android_poll_source* source = nullptr;

        while (ALooper_pollOnce(timeout, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0)
        {
            if (source)
            {
                source->process(app, source);
            }
            if (app->destroyRequested)
            {
                _shouldClose = true;
                break;
            }
            timeout = 0;
        }

        ProcessInputEvents();
        ProcessTextInput();
    }

    void AndroidWindow::FlushEvents()
    {
        for (const auto& ev : _pendingEvents)
        {
            _events.Publish(ev);
        }
        _pendingEvents.clear();

        for (const auto& in : _pendingInput)
        {
            _input.Publish(in);
        }
        _pendingInput.clear();
    }

    void AndroidWindow::PumpEvents()
    {
        PumpMessages();
        FlushEvents();
    }

    Ref<Window> Window::Create(const WindowDesc& desc)
    {
        return MakeRef<AndroidWindow>(desc);
    }

    void Window::PumpAll()
    {
        if (g_currentWindow)
        {
            g_currentWindow->PumpEvents();
        }
    }

    bool Window::AnyOpen()
    {
        return g_currentWindow != nullptr && !g_currentWindow->ShouldClose();
    }

    ScreenPoint Window::CursorScreenPosition()
    {
        if (g_currentWindow)
        {
            return g_currentWindow->GetLastTouchPoint();
        }
        return {0, 0};
    }

    Window* Window::WindowAt(ScreenPoint point)
    {
        if (g_currentWindow && g_currentWindow->GetClientRect().Contains(point))
        {
            return g_currentWindow;
        }
        return nullptr;
    }

    bool Window::IsOnAnyMonitor([[maybe_unused]] const ScreenRect& rect)
    {
        return true;
    }

    ScreenRect Window::PrimaryWorkArea()
    {
        if (g_currentWindow)
        {
            return g_currentWindow->GetClientRect();
        }
        return {0, 0, 1920, 1080};
    }
}
