#include "WindowsWindow.h"

#include "lumora/log/Log.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("Platform.Windows")

namespace lumora::platform
{
    namespace
    {
        const wchar_t* const kClassName = L"LumoraWindowClass";

        std::vector<WindowsWindow*> g_windows;
        std::once_flag              g_dpiOnce;

        void DrainThreadQueue()
        {
            MSG msg;
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }

        std::wstring Widen(std::string_view s)
        {
            if (s.empty()) return {};
            const int len = ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring out(static_cast<std::size_t>(len), L'\0');
            ::MultiByteToWideChar(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
            return out;
        }

        std::string Narrow(std::wstring_view s)
        {
            if (s.empty()) return {};
            const int len = ::WideCharToMultiByte(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(len), '\0');
            ::WideCharToMultiByte(
                CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len, nullptr, nullptr);
            return out;
        }

        KeyMods CurrentMods()
        {
            KeyMods mods = KeyMods::None;
            if (::GetKeyState(VK_CONTROL) & 0x8000) mods |= KeyMods::Ctrl;
            if (::GetKeyState(VK_SHIFT) & 0x8000) mods |= KeyMods::Shift;
            if (::GetKeyState(VK_MENU) & 0x8000) mods |= KeyMods::Alt;
            if ((::GetKeyState(VK_LWIN) | ::GetKeyState(VK_RWIN)) & 0x8000) mods |= KeyMods::Super;
            return mods;
        }

        InputVec2 ClientPos(LPARAM lParam)
        {
            return InputVec2{
                static_cast<float>(GET_X_LPARAM(lParam)),
                static_cast<float>(GET_Y_LPARAM(lParam))
            };
        }

        Key VkToKey(WPARAM vk, LPARAM lParam)
        {
            // Resolve generic modifier VKs to their left/right variants via scan code.
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU)
            {
                const UINT sc  = LOBYTE(HIWORD(lParam));
                const UINT ext = ::MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
                if (ext) vk = static_cast<WPARAM>(ext);
            }

            switch (vk)
            {
            // Control
            case VK_BACK:      return Key::Backspace;
            case VK_RETURN:    return Key::Enter;
            case VK_TAB:       return Key::Tab;
            case VK_ESCAPE:    return Key::Escape;
            case VK_DELETE:    return Key::Delete;
            case VK_SPACE:     return Key::Space;
            case VK_INSERT:    return Key::Insert;
            case VK_SNAPSHOT:  return Key::PrintScreen;
            case VK_PAUSE:     return Key::Pause;

            // Navigation
            case VK_LEFT:      return Key::Left;
            case VK_RIGHT:     return Key::Right;
            case VK_UP:        return Key::Up;
            case VK_DOWN:      return Key::Down;
            case VK_HOME:      return Key::Home;
            case VK_END:       return Key::End;
            case VK_PRIOR:     return Key::PageUp;
            case VK_NEXT:      return Key::PageDown;

            // Locks
            case VK_CAPITAL:   return Key::CapsLock;
            case VK_NUMLOCK:   return Key::NumLock;
            case VK_SCROLL:    return Key::ScrollLock;

            // Modifiers
            case VK_LSHIFT:    return Key::LeftShift;
            case VK_RSHIFT:    return Key::RightShift;
            case VK_LCONTROL:  return Key::LeftCtrl;
            case VK_RCONTROL:  return Key::RightCtrl;
            case VK_LMENU:     return Key::LeftAlt;
            case VK_RMENU:     return Key::RightAlt;
            case VK_LWIN:      return Key::LeftSuper;
            case VK_RWIN:      return Key::RightSuper;

            // Letters
            case 'A': return Key::A; case 'B': return Key::B; case 'C': return Key::C;
            case 'D': return Key::D; case 'E': return Key::E; case 'F': return Key::F;
            case 'G': return Key::G; case 'H': return Key::H; case 'I': return Key::I;
            case 'J': return Key::J; case 'K': return Key::K; case 'L': return Key::L;
            case 'M': return Key::M; case 'N': return Key::N; case 'O': return Key::O;
            case 'P': return Key::P; case 'Q': return Key::Q; case 'R': return Key::R;
            case 'S': return Key::S; case 'T': return Key::T; case 'U': return Key::U;
            case 'V': return Key::V; case 'W': return Key::W; case 'X': return Key::X;
            case 'Y': return Key::Y; case 'Z': return Key::Z;

            // Number row
            case '0': return Key::N0; case '1': return Key::N1; case '2': return Key::N2;
            case '3': return Key::N3; case '4': return Key::N4; case '5': return Key::N5;
            case '6': return Key::N6; case '7': return Key::N7; case '8': return Key::N8;
            case '9': return Key::N9;

            // Function keys
            case VK_F1:  return Key::F1;  case VK_F2:  return Key::F2;
            case VK_F3:  return Key::F3;  case VK_F4:  return Key::F4;
            case VK_F5:  return Key::F5;  case VK_F6:  return Key::F6;
            case VK_F7:  return Key::F7;  case VK_F8:  return Key::F8;
            case VK_F9:  return Key::F9;  case VK_F10: return Key::F10;
            case VK_F11: return Key::F11; case VK_F12: return Key::F12;
            case VK_F13: return Key::F13; case VK_F14: return Key::F14;
            case VK_F15: return Key::F15; case VK_F16: return Key::F16;
            case VK_F17: return Key::F17; case VK_F18: return Key::F18;
            case VK_F19: return Key::F19; case VK_F20: return Key::F20;
            case VK_F21: return Key::F21; case VK_F22: return Key::F22;
            case VK_F23: return Key::F23; case VK_F24: return Key::F24;

            // Numpad
            case VK_NUMPAD0: return Key::Num0; case VK_NUMPAD1: return Key::Num1;
            case VK_NUMPAD2: return Key::Num2; case VK_NUMPAD3: return Key::Num3;
            case VK_NUMPAD4: return Key::Num4; case VK_NUMPAD5: return Key::Num5;
            case VK_NUMPAD6: return Key::Num6; case VK_NUMPAD7: return Key::Num7;
            case VK_NUMPAD8: return Key::Num8; case VK_NUMPAD9: return Key::Num9;
            case VK_ADD:      return Key::NumAdd;
            case VK_SUBTRACT: return Key::NumSub;
            case VK_MULTIPLY: return Key::NumMul;
            case VK_DIVIDE:   return Key::NumDiv;
            case VK_DECIMAL:  return Key::NumDecimal;

            // Symbols (US layout OEM keys)
            case VK_OEM_3:      return Key::Grave;
            case VK_OEM_MINUS:  return Key::Minus;
            case VK_OEM_PLUS:   return Key::Equal;
            case VK_OEM_4:      return Key::LeftBracket;
            case VK_OEM_6:      return Key::RightBracket;
            case VK_OEM_5:      return Key::Backslash;
            case VK_OEM_1:      return Key::Semicolon;
            case VK_OEM_7:      return Key::Apostrophe;
            case VK_OEM_COMMA:  return Key::Comma;
            case VK_OEM_PERIOD: return Key::Period;
            case VK_OEM_2:      return Key::Slash;

            default: return Key::Unknown;
            }
        }
    }

    Ref<Window> Window::Create(const WindowDesc& desc)
    {
        return MakeRef<WindowsWindow>(desc);
    }

    WindowsWindow::WindowsWindow(const WindowDesc& desc)
    {

        std::call_once(g_dpiOnce, []
        {
            ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        });

        _hinstance = ::GetModuleHandleW(nullptr);

        WNDCLASSEXW wc{};
        if (!::GetClassInfoExW(_hinstance, kClassName, &wc))
        {
            wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
            wc.lpfnWndProc = &WindowsWindow::WndProc;
            wc.hInstance = _hinstance;
            wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kClassName;

            if (!::RegisterClassExW(&wc))
            {
                LOG_FATAL("failed to register window class");
                return;
            }
        }

        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!desc.resizable)
            style &= ~static_cast<DWORD>(WS_THICKFRAME | WS_MAXIMIZEBOX);

        DWORD exStyle = 0;
        if (desc.owner != nullptr && desc.showInTaskbar) exStyle |= WS_EX_APPWINDOW;

        const int originX = desc.position ? desc.position->x : 0;
        const int originY = desc.position ? desc.position->y : 0;
        RECT rect{originX, originY, originX + static_cast<LONG>(desc.width), originY + static_cast<LONG>(desc.height)};
        ::AdjustWindowRectEx(&rect, style, FALSE, exStyle);

        const std::wstring title = Widen(desc.title);
        const HWND owner = desc.owner != nullptr ? static_cast<HWND>(desc.owner->GetNativeHandle().window) : nullptr;

        _hwnd = ::CreateWindowExW(
            exStyle, kClassName, title.c_str(), style,
            desc.position ? rect.left : CW_USEDEFAULT, desc.position ? rect.top : CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top,
            owner, nullptr, _hinstance, this);

        if (!_hwnd)
        {
            LOG_FATAL("failed to create window");
            return;
        }

        _width = desc.width;
        _height = desc.height;
        _dpiScale = static_cast<float>(::GetDpiForWindow(_hwnd)) / 96.0f;

        if (desc.position)
            SetClientRect({desc.position->x, desc.position->y, static_cast<int32_t>(desc.width),
                           static_cast<int32_t>(desc.height)});

        ::ShowWindow(_hwnd, SW_SHOW);
        ::DragAcceptFiles(_hwnd, TRUE);
        for (const UINT message : {UINT{WM_DROPFILES}, UINT{WM_COPYDATA}, UINT{0x0049} /* WM_COPYGLOBALDATA */})
        {
            if (!::ChangeWindowMessageFilterEx(_hwnd, message, MSGFLT_ALLOW, nullptr))
            {
                LOG_WARN("ChangeWindowMessageFilterEx(0x{:04X}) failed ({}); file drops from a non-elevated "
                         "Explorer may be blocked", message, ::GetLastError());
            }
        }

        POINT initialPos{};
        ::GetCursorPos(&initialPos);
        ::ScreenToClient(_hwnd, &initialPos);
        _lastPointerPos = InputVec2{static_cast<float>(initialPos.x), static_cast<float>(initialPos.y)};

        if (desc.fullscreenMode != FullscreenMode::Windowed)
            SetFullscreenMode(desc.fullscreenMode);

        g_windows.push_back(this);
    }

    WindowsWindow::~WindowsWindow()
    {
        std::erase(g_windows, this);

        if (_hwnd)
        {
            ::DestroyWindow(_hwnd);
            _hwnd = nullptr;
        }
    }

    void WindowsWindow::WaitEvents()
    {
        ::WaitMessage();
    }

    void WindowsWindow::Wake()
    {
        if (_hwnd)
        {
            ::PostMessageW(_hwnd, WM_NULL, 0, 0);
        }
    }

    void WindowsWindow::PumpEvents()
    {
        PumpMessages();
        FlushEvents();
    }

    void WindowsWindow::PumpMessages()
    {
        DrainThreadQueue();
    }

    void WindowsWindow::FlushEvents()
    {
        for (const WindowEvent& e : _pending)
            _events.Publish(e);
        _pending.clear();

        for (const InputEvent& e : _pendingInput)
            _input.Publish(e);
        _pendingInput.clear();

        for (const FileDropEvent& e : _pendingDrops)
            _fileDrops.Publish(e);
        _pendingDrops.clear();
    }

    void Window::PumpAll()
    {
        DrainThreadQueue();
        const std::vector<WindowsWindow*> live = g_windows;
        for (WindowsWindow* w : live)
        {
            // A prior flush may have destroyed this window (handler dropped its Ref); skip stale entries.
            if (std::find(g_windows.begin(), g_windows.end(), w) != g_windows.end())
                w->FlushEvents();
        }
    }

    bool Window::AnyOpen()
    {
        return std::ranges::any_of(g_windows,
                                   [](const WindowsWindow* w) { return !w->ShouldClose(); });
    }

    ScreenPoint Window::CursorScreenPosition()
    {
        POINT p{};
        ::GetCursorPos(&p);
        return {p.x, p.y};
    }

    Window* Window::WindowAt(const ScreenPoint point)
    {
        const HWND hit = ::GetAncestor(::WindowFromPoint(POINT{point.x, point.y}), GA_ROOT);
        if (hit == nullptr) return nullptr;
        for (WindowsWindow* w : g_windows)
            if (static_cast<HWND>(w->GetNativeHandle().window) == hit && w->GetClientRect().Contains(point)) return w;
        return nullptr;
    }

    bool Window::IsOnAnyMonitor(const ScreenRect& rect)
    {
        const RECT r{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
        return ::MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != nullptr;
    }

    ScreenRect Window::PrimaryWorkArea()
    {
        MONITORINFO info{.cbSize = sizeof(MONITORINFO)};
        ::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &info);
        return {info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left,
                info.rcWork.bottom - info.rcWork.top};
    }

    NativeWindowHandle WindowsWindow::GetNativeHandle() const
    {
        return NativeWindowHandle{
            .window = _hwnd,
            .instance = _hinstance,
            .backend = WindowBackend::Win32,
        };
    }

    void WindowsWindow::SetTitle(std::string_view title)
    {
        if (_hwnd) ::SetWindowTextW(_hwnd, Widen(title).c_str());
    }

    ScreenRect WindowsWindow::GetClientRect() const
    {
        RECT r{};
        ::GetClientRect(_hwnd, &r);
        POINT origin{0, 0};
        ::ClientToScreen(_hwnd, &origin);
        return {origin.x, origin.y, r.right - r.left, r.bottom - r.top};
    }

    void WindowsWindow::SetClientRect(const ScreenRect& rect)
    {
        RECT r{rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
        const auto style = static_cast<DWORD>(::GetWindowLongW(_hwnd, GWL_STYLE));
        const auto exStyle = static_cast<DWORD>(::GetWindowLongW(_hwnd, GWL_EXSTYLE));
        ::AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, ::GetDpiForWindow(_hwnd));
        ::SetWindowPos(_hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ScreenPoint WindowsWindow::ClientToScreen(const float x, const float y) const
    {
        POINT p{static_cast<LONG>(x), static_cast<LONG>(y)};
        ::ClientToScreen(_hwnd, &p);
        return {p.x, p.y};
    }

    void WindowsWindow::Focus() { ::SetForegroundWindow(_hwnd); }

    bool WindowsWindow::IsHidden() const { return ::IsIconic(_hwnd) != FALSE || ::IsWindowVisible(_hwnd) == FALSE; }

    void WindowsWindow::SetFullscreenMode(FullscreenMode mode)
    {
        if (_fullscreenMode == mode || !_hwnd)
            return;

        // Always restore display settings when leaving exclusive mode.
        if (_fullscreenMode == FullscreenMode::Exclusive)
        {
            ::ChangeDisplaySettingsExW(_exclusiveDevice.c_str(), nullptr, nullptr, 0, nullptr);
            _exclusiveDevice.clear();
        }

        if (mode == FullscreenMode::Windowed)
        {
            ::SetWindowLongW(_hwnd, GWL_STYLE, _savedStyle);
            ::SetWindowLongW(_hwnd, GWL_EXSTYLE, _savedExStyle);
            ::SetWindowPlacement(_hwnd, &_savedPlacement);
            ::SetWindowPos(_hwnd, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        else
        {
            // Save windowed state the first time we leave it.
            if (_fullscreenMode == FullscreenMode::Windowed)
            {
                _savedStyle = ::GetWindowLongW(_hwnd, GWL_STYLE);
                _savedExStyle = ::GetWindowLongW(_hwnd, GWL_EXSTYLE);
                _savedPlacement.length = sizeof(WINDOWPLACEMENT);
                ::GetWindowPlacement(_hwnd, &_savedPlacement);
            }

            MONITORINFOEXW mi{};
            mi.cbSize = sizeof(MONITORINFOEXW);
            ::GetMonitorInfoW(::MonitorFromWindow(_hwnd, MONITOR_DEFAULTTONEAREST), &mi);

            if (mode == FullscreenMode::Borderless)
            {
                ::SetWindowLongW(_hwnd, GWL_STYLE,
                                 _savedStyle & ~static_cast<LONG>(WS_OVERLAPPEDWINDOW));
                ::SetWindowLongW(_hwnd, GWL_EXSTYLE,
                                 _savedExStyle & ~static_cast<LONG>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                     WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
                ::SetWindowPos(_hwnd, HWND_TOP,
                               mi.rcMonitor.left, mi.rcMonitor.top,
                               mi.rcMonitor.right - mi.rcMonitor.left,
                               mi.rcMonitor.bottom - mi.rcMonitor.top,
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            }
            else // Exclusive
            {
                DEVMODEW dm{};
                dm.dmSize = sizeof(DEVMODEW);
                ::EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm);

                if (::ChangeDisplaySettingsExW(mi.szDevice, &dm, nullptr, CDS_FULLSCREEN, nullptr)
                    != DISP_CHANGE_SUCCESSFUL)
                {
                    LOG_ERROR("ChangeDisplaySettingsExW failed; falling back to borderless");
                    mode = FullscreenMode::Borderless;
                    ::SetWindowLongW(_hwnd, GWL_STYLE,
                                     _savedStyle & ~static_cast<LONG>(WS_OVERLAPPEDWINDOW));
                    ::SetWindowLongW(_hwnd, GWL_EXSTYLE,
                                     _savedExStyle & ~static_cast<LONG>(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                         WS_EX_CLIENTEDGE | WS_EX_STATICEDGE));
                    ::SetWindowPos(_hwnd, HWND_TOP,
                                   mi.rcMonitor.left, mi.rcMonitor.top,
                                   mi.rcMonitor.right - mi.rcMonitor.left,
                                   mi.rcMonitor.bottom - mi.rcMonitor.top,
                                   SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
                }
                else
                {
                    _exclusiveDevice = mi.szDevice;
                    ::SetWindowLongW(_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                    ::SetWindowLongW(_hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
                    ::SetWindowPos(_hwnd, HWND_TOP, 0, 0,
                                   static_cast<int>(dm.dmPelsWidth),
                                   static_cast<int>(dm.dmPelsHeight),
                                   SWP_FRAMECHANGED);
                }
            }
        }

        _fullscreenMode = mode;
    }

    void WindowsWindow::ApplyCapture()
    {
        RECT client{};
        ::GetClientRect(_hwnd, &client);

        // Intersected with the client area rather than trusted: a panel rect published before a shrink could clip the pointer
        // outside the window.
        RECT region = client;
        if (!_cursorClip.Empty())
        {
            region.left   = std::max<LONG>(client.left,   _cursorClip.x);
            region.top    = std::max<LONG>(client.top,    _cursorClip.y);
            region.right  = std::min<LONG>(client.right,  _cursorClip.x + _cursorClip.width);
            region.bottom = std::min<LONG>(client.bottom, _cursorClip.y + _cursorClip.height);
            if (region.right <= region.left || region.bottom <= region.top)
                region = client;
        }

        POINT topLeft{region.left, region.top};
        POINT bottomRight{region.right, region.bottom};
        ::ClientToScreen(_hwnd, &topLeft);
        ::ClientToScreen(_hwnd, &bottomRight);

        _cursorAnchor = POINT{
            (topLeft.x + bottomRight.x) / 2,
            (topLeft.y + bottomRight.y) / 2
        };
        ::SetCursorPos(_cursorAnchor.x, _cursorAnchor.y);

        const RECT clip{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
        ::ClipCursor(&clip);
    }

    void WindowsWindow::SetCursorMode(CursorMode mode, const CursorClip& clip)
    {
        if (!_hwnd)
            return;

        // The clip is part of the comparison: re-clipping while captured is the per-frame case and must not early-out.
        if (_cursorMode == mode && (mode == CursorMode::Normal || _cursorClip == clip))
            return;

        const bool wasCaptured = _cursorMode == CursorMode::Captured;
        _cursorClip = clip;

        if (mode == CursorMode::Captured)
        {
            // Win32's show-cursor counter is a counter, not a flag: touch it only on the transition so a per-frame re-clip
            // does not drive it negative.
            if (!wasCaptured)
                while (::ShowCursor(FALSE) >= 0) {}

            ApplyCapture();
        }
        else
        {
            ::ClipCursor(nullptr);
            while (::ShowCursor(TRUE) < 0) {}

            POINT pos{};
            ::GetCursorPos(&pos);
            ::ScreenToClient(_hwnd, &pos);
            _lastPointerPos = InputVec2{static_cast<float>(pos.x), static_cast<float>(pos.y)};
        }

        _cursorMode = mode;
    }

    LRESULT CALLBACK WindowsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (msg == WM_NCCREATE)
        {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        }

        auto* self = reinterpret_cast<WindowsWindow*>(
            ::GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (self) return self->HandleMessage(hwnd, msg, wParam, lParam);

        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT WindowsWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_SIZE:
            {
                const uint32_t w = LOWORD(lParam);
                const uint32_t h = HIWORD(lParam);
                if (wParam != SIZE_MINIMIZED && (w != _width || h != _height))
                {
                    _width = w;
                    _height = h;
                    if (_cursorMode == CursorMode::Captured) ApplyCapture();
                    _pending.push_back({WindowEventType::Resize, w, h, _dpiScale});
                }
                return 0;
            }
        case WM_MOVE:
            {
                // The clip is in screen coordinates; without re-clipping, dragging the window leaves the pointer fenced into
                // its old position.
                if (_cursorMode == CursorMode::Captured) ApplyCapture();
                return 0;
            }
        case WM_CLOSE:
            {
                _shouldClose = true;
                _pending.push_back({WindowEventType::Close, _width, _height, _dpiScale});
                return 0;
            }
        case WM_SETFOCUS:
            _pending.push_back({WindowEventType::FocusGained, _width, _height, _dpiScale});
            return 0;
        case WM_KILLFOCUS:
            SetCursorMode(CursorMode::Normal);
            _pending.push_back({WindowEventType::FocusLost, _width, _height, _dpiScale});
            return 0;
        case WM_DPICHANGED:
            {
                _dpiScale = static_cast<float>(HIWORD(wParam)) / 96.0f;
                const RECT* const r = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(hwnd, nullptr, r->left, r->top,
                               r->right - r->left, r->bottom - r->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
                _pending.push_back({WindowEventType::DpiChanged, _width, _height, _dpiScale});
                return 0;
            }
        case WM_MOUSEMOVE:
            {
                if (_cursorMode == CursorMode::Captured)
                {
                    POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    ::ClientToScreen(hwnd, &screen);

                    const InputVec2 delta{
                        static_cast<float>(screen.x - _cursorAnchor.x),
                        static_cast<float>(screen.y - _cursorAnchor.y)
                    };
                    if (delta.x == 0.f && delta.y == 0.f)
                        return 0;

                    InputEvent e{};
                    e.type = InputEventType::PointerMove;
                    e.position = _lastPointerPos;
                    e.delta = delta;
                    e.mods = CurrentMods();
                    _pendingInput.push_back(e);

                    ::SetCursorPos(_cursorAnchor.x, _cursorAnchor.y);
                    return 0;
                }

                const InputVec2 pos = ClientPos(lParam);
                InputEvent e{};
                e.type = InputEventType::PointerMove;
                e.position = pos;
                e.delta = {pos.x - _lastPointerPos.x, pos.y - _lastPointerPos.y};
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                _lastPointerPos = pos;
                return 0;
            }
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            {
                InputEvent e{};
                e.type = InputEventType::PointerDown;
                e.position = ClientPos(lParam);
                e.button = msg == WM_LBUTTONDOWN
                               ? PointerButton::Left
                               : msg == WM_RBUTTONDOWN
                               ? PointerButton::Right
                               : PointerButton::Middle;
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                if (::GetCapture() != hwnd) ::SetCapture(hwnd);
                return 0;
            }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
            {
                InputEvent e{};
                e.type = InputEventType::PointerUp;
                e.position = ClientPos(lParam);
                e.button = msg == WM_LBUTTONUP
                               ? PointerButton::Left
                               : msg == WM_RBUTTONUP
                               ? PointerButton::Right
                               : PointerButton::Middle;
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                if (::GetCapture() == hwnd) ::ReleaseCapture();
                return 0;
            }
        case WM_XBUTTONDOWN:
            {
                InputEvent e{};
                e.type = InputEventType::PointerDown;
                e.position = ClientPos(lParam);
                e.button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? PointerButton::X1 : PointerButton::X2;
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                if (::GetCapture() != hwnd) ::SetCapture(hwnd);
                return TRUE;
            }
        case WM_XBUTTONUP:
            {
                InputEvent e{};
                e.type = InputEventType::PointerUp;
                e.position = ClientPos(lParam);
                e.button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? PointerButton::X1 : PointerButton::X2;
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                if (::GetCapture() == hwnd) ::ReleaseCapture();
                return TRUE;
            }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ::ScreenToClient(hwnd, &pt);
                const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                    static_cast<float>(WHEEL_DELTA);
                InputEvent e{};
                e.type = InputEventType::Scroll;
                e.position = {static_cast<float>(pt.x), static_cast<float>(pt.y)};
                if (msg == WM_MOUSEWHEEL) e.scrollDelta.y = delta;
                else e.scrollDelta.x = delta;
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                return 0;
            }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            {
                InputEvent e{};
                e.type = InputEventType::KeyDown;
                e.key = VkToKey(wParam, lParam);
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                break;
            }
        case WM_KEYUP:
        case WM_SYSKEYUP:
            {
                InputEvent e{};
                e.type = InputEventType::KeyUp;
                e.key = VkToKey(wParam, lParam);
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                break;
            }
        case WM_CHAR:
            {
                const auto code = static_cast<wchar_t>(wParam);
                if (code >= 0xD800 && code <= 0xDBFF)
                {
                    _pendingSurrogate = code;
                    return 0;
                }

                wchar_t wbuf[2];
                int wlen = 0;
                if (code >= 0xDC00 && code <= 0xDFFF)
                {
                    if (_pendingSurrogate == 0) return 0;
                    wbuf[0] = _pendingSurrogate;
                    wbuf[1] = code;
                    wlen = 2;
                    _pendingSurrogate = 0;
                }
                else
                {
                    _pendingSurrogate = 0;
                    if (code < 0x20 || code == 0x7F) return 0;
                    wbuf[0] = code;
                    wlen = 1;
                }

                InputEvent e{};
                e.type = InputEventType::Text;
                ::WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, e.text,
                                      static_cast<int>(sizeof(e.text) - 1), nullptr, nullptr);
                e.mods = CurrentMods();
                _pendingInput.push_back(e);
                return 0;
            }
        case WM_DROPFILES:
            {
                auto         drop = reinterpret_cast<HDROP>(wParam);
                FileDropEvent e{};

                POINT pt{};
                ::DragQueryPoint(drop, &pt);
                e.position = {static_cast<float>(pt.x), static_cast<float>(pt.y)};

                const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFFu, nullptr, 0);
                e.paths.reserve(count);
                for (UINT i = 0; i < count; ++i)
                {
                    const UINT len = ::DragQueryFileW(drop, i, nullptr, 0);
                    if (len == 0) continue;
                    std::wstring w(len, L'\0');
                    ::DragQueryFileW(drop, i, w.data(), len + 1);
                    e.paths.push_back(Narrow(w));
                }
                ::DragFinish(drop);

                if (!e.paths.empty()) _pendingDrops.push_back(std::move(e));
                return 0;
            }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            ::ValidateRect(hwnd, nullptr);
            return 0;
        case WM_SYSCOMMAND:
            if (_fullscreenMode == FullscreenMode::Exclusive)
            {
                const WPARAM cmd = wParam & 0xFFF0;
                if (cmd == SC_SCREENSAVE || cmd == SC_MONITORPOWER)
                    return 0;
            }
            break;
        case WM_NCDESTROY:
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        default:
            break;
        }
        return ::DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}
