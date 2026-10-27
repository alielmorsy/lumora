#include "LinuxWindow.h"

#include "Keyboard.h"

#include "lumora/log/Log.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib-xcb.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>
#include <xkbcommon/xkbcommon-x11.h>
#include <xkbcommon/xkbcommon.h>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

LUMORA_LOG_CATEGORY("Platform.X11")

namespace lumora::platform
{
    namespace
    {
        struct Atoms
        {
            Atom wmProtocols, wmDeleteWindow, netWmName, utf8String, netWmPid, netWmState, netWmStateFullscreen,
                netWmStateSkipTaskbar, netWmStateHidden, netWmBypassCompositor, netActiveWindow, netWorkarea,
                netCurrentDesktop, xdndAware, xdndEnter, xdndPosition, xdndStatus, xdndLeave, xdndDrop, xdndFinished,
                xdndSelection, xdndTypeList, xdndActionCopy, textUriList, clipboard, targets, text, textPlainUtf8,
                incr, lumoraSelection, lumoraClipboard;

            void Init(Display* d)
            {
                static constexpr std::array<std::pair<Atom Atoms::*, const char*>, 31> kTable{{
                    {&Atoms::wmProtocols, "WM_PROTOCOLS"},
                    {&Atoms::wmDeleteWindow, "WM_DELETE_WINDOW"},
                    {&Atoms::netWmName, "_NET_WM_NAME"},
                    {&Atoms::utf8String, "UTF8_STRING"},
                    {&Atoms::netWmPid, "_NET_WM_PID"},
                    {&Atoms::netWmState, "_NET_WM_STATE"},
                    {&Atoms::netWmStateFullscreen, "_NET_WM_STATE_FULLSCREEN"},
                    {&Atoms::netWmStateSkipTaskbar, "_NET_WM_STATE_SKIP_TASKBAR"},
                    {&Atoms::netWmStateHidden, "_NET_WM_STATE_HIDDEN"},
                    {&Atoms::netWmBypassCompositor, "_NET_WM_BYPASS_COMPOSITOR"},
                    {&Atoms::netActiveWindow, "_NET_ACTIVE_WINDOW"},
                    {&Atoms::netWorkarea, "_NET_WORKAREA"},
                    {&Atoms::netCurrentDesktop, "_NET_CURRENT_DESKTOP"},
                    {&Atoms::xdndAware, "XdndAware"},
                    {&Atoms::xdndEnter, "XdndEnter"},
                    {&Atoms::xdndPosition, "XdndPosition"},
                    {&Atoms::xdndStatus, "XdndStatus"},
                    {&Atoms::xdndLeave, "XdndLeave"},
                    {&Atoms::xdndDrop, "XdndDrop"},
                    {&Atoms::xdndFinished, "XdndFinished"},
                    {&Atoms::xdndSelection, "XdndSelection"},
                    {&Atoms::xdndTypeList, "XdndTypeList"},
                    {&Atoms::xdndActionCopy, "XdndActionCopy"},
                    {&Atoms::textUriList, "text/uri-list"},
                    {&Atoms::clipboard, "CLIPBOARD"},
                    {&Atoms::targets, "TARGETS"},
                    {&Atoms::text, "TEXT"},
                    {&Atoms::textPlainUtf8, "text/plain;charset=utf-8"},
                    {&Atoms::incr, "INCR"},
                    {&Atoms::lumoraSelection, "LUMORA_SELECTION"},
                    {&Atoms::lumoraClipboard, "LUMORA_CLIPBOARD"},
                }};

                std::array<char*, kTable.size()> names{};
                std::array<Atom, kTable.size()>  atoms{};
                for (std::size_t i = 0; i < kTable.size(); ++i)
                    names[i] = const_cast<char*>(kTable[i].second);
                XInternAtoms(d, names.data(), static_cast<int>(names.size()), False, atoms.data());
                for (std::size_t i = 0; i < kTable.size(); ++i)
                    this->*kTable[i].first = atoms[i];
            }
        };

        int ErrorHandler(Display* d, XErrorEvent* e)
        {
            char text[128] = {};
            XGetErrorText(d, e->error_code, text, sizeof(text));
            LOG_WARN("X error: {} (request {}.{}, resource 0x{:x})", text, e->request_code, e->minor_code,
                     static_cast<unsigned long>(e->resourceid));
            return 0;
        }

        std::string ReadProperty(Display* d, ::Window w, Atom prop, bool del, Atom* type = nullptr)
        {
            Atom           actual = 0;
            int            format = 0;
            unsigned long  count = 0, after = 0;
            unsigned char* data = nullptr;
            std::string    out;
            if (XGetWindowProperty(d, w, prop, 0, ~0L, del ? True : False, AnyPropertyType, &actual, &format, &count,
                                   &after, &data) == Success)
            {
                if (type) *type = actual;
                if (data && format == 8) out.assign(reinterpret_cast<const char*>(data), count);
            }
            if (data) XFree(data);
            return out;
        }

        std::vector<long> ReadLongs(Display* d, ::Window w, Atom prop)
        {
            Atom           actual = 0;
            int            format = 0;
            unsigned long  count = 0, after = 0;
            unsigned char* data = nullptr;
            std::vector<long> out;
            if (XGetWindowProperty(d, w, prop, 0, ~0L, False, AnyPropertyType, &actual, &format, &count, &after,
                                   &data) == Success &&
                data && format == 32)
            {
                const long* longs = reinterpret_cast<const long*>(data);
                out.assign(longs, longs + count);
            }
            if (data) XFree(data);
            return out;
        }

        void SendClientMessage(Display* d, ::Window target, ::Window window, Atom type, std::array<long, 5> data,
                               long mask)
        {
            XEvent e{};
            e.xclient.type         = ClientMessage;
            e.xclient.window       = window;
            e.xclient.message_type = type;
            e.xclient.format       = 32;
            for (std::size_t i = 0; i < data.size(); ++i)
                e.xclient.data.l[i] = data[i];
            XSendEvent(d, target, False, mask, &e);
        }

        int HexValue(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::string PercentDecode(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '%' && i + 2 < s.size() && HexValue(s[i + 1]) >= 0 && HexValue(s[i + 2]) >= 0)
                {
                    out.push_back(static_cast<char>(HexValue(s[i + 1]) * 16 + HexValue(s[i + 2])));
                    i += 2;
                }
                else
                {
                    out.push_back(s[i]);
                }
            }
            return out;
        }

        std::vector<std::string> ParseUriList(std::string_view data)
        {
            std::vector<std::string> paths;
            while (!data.empty())
            {
                const std::size_t eol = data.find('\n');
                std::string_view  line = data.substr(0, eol);
                data = eol == std::string_view::npos ? std::string_view{} : data.substr(eol + 1);
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
                if (line.empty() || line.front() == '#') continue;

                constexpr std::string_view kScheme = "file://";
                if (line.starts_with(kScheme))
                {
                    line.remove_prefix(kScheme.size());
                    const std::size_t slash = line.find('/');
                    if (slash == std::string_view::npos) continue;
                    const std::string_view host = line.substr(0, slash);
                    if (!host.empty() && host != "localhost") continue;
                    paths.push_back(PercentDecode(line.substr(slash)));
                }
                else if (line.front() == '/')
                {
                    paths.push_back(std::string(line));
                }
            }
            return paths;
        }

        std::string Utf8ToLatin1(std::string_view s)
        {
            std::string out;
            for (std::size_t i = 0; i < s.size();)
            {
                const auto c = static_cast<unsigned char>(s[i]);
                std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
                uint32_t cp = len == 1 ? c : c & (0xFFu >> (len + 1));
                for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
                    cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3Fu);
                out.push_back(cp < 256 ? static_cast<char>(cp) : '?');
                i += len;
            }
            return out;
        }

        std::string Latin1ToUtf8(std::string_view s)
        {
            std::string out;
            for (const char ch : s)
            {
                const auto c = static_cast<unsigned char>(ch);
                if (c < 0x80)
                {
                    out.push_back(ch);
                }
                else
                {
                    out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                }
            }
            return out;
        }

        /// Serves and reads the CLIPBOARD selection on its own connection and thread, so it works with no window and
        /// while the main thread is blocked.
        class X11Clipboard
        {
        public:
            explicit X11Clipboard(const Atoms& atoms) : _a(atoms) {}

            ~X11Clipboard()
            {
                if (_thread.joinable())
                {
                    _stop = true;
                    Kick();
                    _thread.join();
                }
                if (_wakeFd >= 0) ::close(_wakeFd);
                if (_dpy)
                {
                    XDestroyWindow(_dpy, _window);
                    XCloseDisplay(_dpy);
                }
            }

            bool Start()
            {
                _dpy = XOpenDisplay(nullptr);
                if (!_dpy) return false;
                _window = XCreateSimpleWindow(_dpy, DefaultRootWindow(_dpy), 0, 0, 1, 1, 0, 0, 0);
                XSelectInput(_dpy, _window, PropertyChangeMask);
                _wakeFd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
                if (_wakeFd < 0) return false;
                _thread = std::thread([this] { Run(); });
                return true;
            }

            bool Set(std::string_view text)
            {
                {
                    std::lock_guard lock(_mutex);
                    _text.assign(text);
                    XSetSelectionOwner(_dpy, _a.clipboard, _window, CurrentTime);
                    _owned = XGetSelectionOwner(_dpy, _a.clipboard) == _window;
                }
                Kick();
                return _owned;
            }

            std::string Get()
            {
                std::lock_guard single(_getMutex);
                std::unique_lock lock(_mutex);
                if (_owned) return _text;
                if (XGetSelectionOwner(_dpy, _a.clipboard) == 0) return {};

                _req = {};
                _req.active = true;
                _req.target = _a.utf8String;
                XConvertSelection(_dpy, _a.clipboard, _req.target, _a.lumoraClipboard, _window, CurrentTime);
                XFlush(_dpy);
                Kick();

                // A stalled transfer gives up after 500 ms without progress; a live INCR transfer keeps extending it.
                while (!_req.done)
                {
                    _req.progressed = false;
                    if (!_cv.wait_for(lock, std::chrono::milliseconds(500),
                                      [this] { return _req.done || _req.progressed; }))
                        break;
                }
                _req.active = false;
                return _req.done ? std::move(_req.result) : std::string{};
            }

        private:
            struct Request
            {
                bool        active     = false;
                bool        done       = false;
                bool        progressed = false;
                bool        incr       = false;
                Atom        target     = 0;
                std::string result;
            };

            void Kick()
            {
                const uint64_t one = 1;
                [[maybe_unused]] const ssize_t n = ::write(_wakeFd, &one, sizeof(one));
            }

            void Run()
            {
                pollfd fds[2] = {{ConnectionNumber(_dpy), POLLIN, 0}, {_wakeFd, POLLIN, 0}};
                for (;;)
                {
                    while (XPending(_dpy))
                    {
                        XEvent ev;
                        XNextEvent(_dpy, &ev);
                        Handle(ev);
                    }
                    fds[0].revents = fds[1].revents = 0;
                    if (::poll(fds, 2, -1) < 0 && errno != EINTR) break;
                    if (fds[1].revents & POLLIN)
                    {
                        uint64_t drained = 0;
                        [[maybe_unused]] const ssize_t n = ::read(_wakeFd, &drained, sizeof(drained));
                        if (_stop) break;
                    }
                }
            }

            void Handle(XEvent& ev)
            {
                switch (ev.type)
                {
                case SelectionClear:
                    if (ev.xselectionclear.selection == _a.clipboard)
                    {
                        std::lock_guard lock(_mutex);
                        _owned = false;
                    }
                    break;
                case SelectionRequest: Serve(ev.xselectionrequest); break;
                case SelectionNotify: OnSelectionNotify(ev.xselection); break;
                case PropertyNotify:
                    if (ev.xproperty.window == _window && ev.xproperty.atom == _a.lumoraClipboard &&
                        ev.xproperty.state == PropertyNewValue)
                        OnIncrChunk();
                    break;
                default: break;
                }
            }

            void Serve(const XSelectionRequestEvent& rq)
            {
                XSelectionEvent reply{};
                reply.type      = SelectionNotify;
                reply.display   = rq.display;
                reply.requestor = rq.requestor;
                reply.selection = rq.selection;
                reply.target    = rq.target;
                reply.property  = 0;
                reply.time      = rq.time;

                const Atom prop = rq.property ? rq.property : rq.target;
                if (rq.selection == _a.clipboard)
                {
                    std::lock_guard lock(_mutex);
                    // ponytail: no INCR on the serving side, data past the server's max request size is refused
                    const std::size_t limit = static_cast<std::size_t>(XExtendedMaxRequestSize(_dpy)) * 4 - 100;
                    if (rq.target == _a.targets)
                    {
                        const std::array<Atom, 5> list{_a.targets, _a.utf8String, XA_STRING, _a.text,
                                                       _a.textPlainUtf8};
                        XChangeProperty(_dpy, rq.requestor, prop, XA_ATOM, 32, PropModeReplace,
                                        reinterpret_cast<const unsigned char*>(list.data()),
                                        static_cast<int>(list.size()));
                        reply.property = prop;
                    }
                    else if (rq.target == _a.utf8String || rq.target == _a.text || rq.target == _a.textPlainUtf8)
                    {
                        if (_text.size() <= limit)
                        {
                            XChangeProperty(_dpy, rq.requestor, prop, _a.utf8String, 8, PropModeReplace,
                                            reinterpret_cast<const unsigned char*>(_text.data()),
                                            static_cast<int>(_text.size()));
                            reply.property = prop;
                        }
                    }
                    else if (rq.target == XA_STRING)
                    {
                        const std::string latin = Utf8ToLatin1(_text);
                        if (latin.size() <= limit)
                        {
                            XChangeProperty(_dpy, rq.requestor, prop, XA_STRING, 8, PropModeReplace,
                                            reinterpret_cast<const unsigned char*>(latin.data()),
                                            static_cast<int>(latin.size()));
                            reply.property = prop;
                        }
                    }
                }
                XSendEvent(_dpy, rq.requestor, False, NoEventMask, reinterpret_cast<XEvent*>(&reply));
                XFlush(_dpy);
            }

            void OnSelectionNotify(const XSelectionEvent& ev)
            {
                std::lock_guard lock(_mutex);
                if (!_req.active || ev.selection != _a.clipboard) return;

                if (ev.property == 0)
                {
                    if (_req.target == _a.utf8String)
                    {
                        _req.target = XA_STRING;
                        XConvertSelection(_dpy, _a.clipboard, XA_STRING, _a.lumoraClipboard, _window, CurrentTime);
                        XFlush(_dpy);
                        _req.progressed = true;
                    }
                    else
                    {
                        _req.done = true;
                    }
                    _cv.notify_all();
                    return;
                }

                Atom        type = 0;
                std::string data = ReadProperty(_dpy, _window, _a.lumoraClipboard, true, &type);
                if (type == _a.incr)
                {
                    _req.incr       = true;
                    _req.progressed = true;
                }
                else
                {
                    _req.result = _req.target == XA_STRING ? Latin1ToUtf8(data) : std::move(data);
                    _req.done   = true;
                }
                _cv.notify_all();
            }

            void OnIncrChunk()
            {
                std::lock_guard lock(_mutex);
                if (!_req.active || !_req.incr) return;

                const std::string chunk = ReadProperty(_dpy, _window, _a.lumoraClipboard, true);
                if (chunk.empty())
                {
                    if (_req.target == XA_STRING) _req.result = Latin1ToUtf8(_req.result);
                    _req.done = true;
                }
                else
                {
                    _req.result += chunk;
                    _req.progressed = true;
                }
                _cv.notify_all();
            }

            const Atoms&            _a;
            Display*                _dpy    = nullptr;
            ::Window                _window = 0;
            int                     _wakeFd = -1;
            std::thread             _thread;
            std::atomic<bool>       _stop{false};
            std::mutex              _mutex;
            std::mutex              _getMutex;
            std::condition_variable _cv;
            std::string             _text;
            bool                    _owned = false;
            Request                 _req;
        };

        class X11Window;

        class X11Backend final : public DisplayBackend
        {
        public:
            explicit X11Backend(Display* dpy);
            ~X11Backend() override;

            [[nodiscard]] bool Ok() const noexcept { return _ok; }

            [[nodiscard]] WindowBackend Kind() const override { return WindowBackend::Xlib; }
            [[nodiscard]] Ref<Window>   CreateWindow(const WindowDesc& desc) override;
            void                        Pump() override;
            void                        Wait() override;

            [[nodiscard]] ScreenPoint CursorPosition() override;
            [[nodiscard]] Window*     WindowAt(ScreenPoint point) override;
            [[nodiscard]] bool        IsOnAnyMonitor(const ScreenRect& rect) override;
            [[nodiscard]] ScreenRect  PrimaryWorkArea() override;

            bool        SetClipboard(std::string_view text) override;
            std::string GetClipboard() override;

            [[nodiscard]] Display*     Dpy() const noexcept { return _dpy; }
            [[nodiscard]] ::Window     Root() const noexcept { return _root; }
            [[nodiscard]] const Atoms& A() const noexcept { return _a; }
            [[nodiscard]] Keyboard&    Kb() noexcept { return _keyboard; }
            [[nodiscard]] Cursor       DefaultCursor() const noexcept { return _defaultCursor; }
            [[nodiscard]] Cursor       BlankCursor() const noexcept { return _blankCursor; }
            [[nodiscard]] float        DpiScale() const noexcept { return _dpiScale; }
            [[nodiscard]] bool         HasRawMotion() const noexcept { return _hasRawMotion; }

            void Register(::Window id, X11Window* w) { _windows[id] = w; }
            void Unregister(::Window id) { _windows.erase(id); }

        private:
            [[nodiscard]] X11Window* Find(::Window id) const;
            [[nodiscard]] X11Clipboard* Clipboard();
            [[nodiscard]] bool       InitXkb();
            void                     InitXInput();
            void                     InitRandr();
            bool                     LoadKeymap();
            void                     SyncKeyboardState();
            [[nodiscard]] float      ReadDpiScale();
            void                     RefreshDpi();
            void                     Dispatch(XEvent& ev);
            void                     OnXkb(const XEvent& ev);
            void                     OnGeneric(XEvent& ev);
            [[nodiscard]] std::vector<ScreenRect> Monitors(std::size_t& primary);
            [[nodiscard]] bool                    IsTopLevelOf(::Window window, ::Window top);

            Display*  _dpy;
            int       _screen;
            ::Window  _root;
            Atoms     _a{};
            Keyboard  _keyboard;
            Cursor    _defaultCursor = 0;
            Cursor    _blankCursor   = 0;
            float     _dpiScale      = 1.0f;
            bool      _ok            = false;

            xcb_connection_t* _xcb         = nullptr;
            int               _xkbEvent    = 0;
            int32_t           _coreDevice  = -1;
            bool              _hasRawMotion = false;
            int               _xiOpcode    = 0;
            bool              _randr15     = false;

            std::unordered_map<::Window, X11Window*> _windows;

            std::once_flag                _clipboardOnce;
            std::unique_ptr<X11Clipboard> _clipboard;
        };

        class X11Window final : public LinuxWindow
        {
        public:
            X11Window(X11Backend& backend, const WindowDesc& desc);
            ~X11Window() override;

            [[nodiscard]] bool Valid() const noexcept { return _window != 0; }

            void Detach()
            {
                _backend = nullptr;
                _dpy     = nullptr;
                _window  = 0;
            }

            [[nodiscard]] NativeWindowHandle GetNativeHandle() const override
            {
                return NativeWindowHandle{
                    .window   = reinterpret_cast<void*>(_window),
                    .instance = _dpy,
                    .backend  = WindowBackend::Xlib,
                };
            }

            [[nodiscard]] ::Window Id() const noexcept { return _window; }

            void SetTitle(std::string_view title) override;
            void SetFullscreenMode(FullscreenMode mode) override;
            void SetCursorMode(CursorMode mode, const CursorClip& clip) override;
            using Window::SetCursorMode;

            [[nodiscard]] ScreenRect  GetClientRect() const override;
            void                      SetClientRect(const ScreenRect& rect) override;
            [[nodiscard]] ScreenPoint ClientToScreen(float x, float y) const override;
            void                      Focus() override;
            [[nodiscard]] bool        IsHidden() const override;

            void HandleEvent(XEvent& ev);
            void OnRawMotion(double dx, double dy);
            void SetDpiScale(float scale);

        private:
            void Push(WindowEventType type) { _pending.push_back({type, _width, _height, _dpiScale}); }
            void SetSizeHints(bool userPosition, int x, int y, int w, int h);
            void SetTitleProperties(std::string_view title);
            void RefreshState();
            bool ApplyCapture();
            void ReleaseCapture();

            void OnConfigure(const XConfigureEvent& ev);
            void OnClientMessage(const XClientMessageEvent& ev);
            void OnMotion(const XMotionEvent& ev);
            void OnButton(const XButtonEvent& ev);
            void OnKey(const XKeyEvent& ev);
            void OnSelection(const XSelectionEvent& ev);

            void OnDndEnter(const XClientMessageEvent& ev);
            void OnDndPosition(const XClientMessageEvent& ev);
            void OnDndDrop(const XClientMessageEvent& ev);
            void FinishDrop(bool accepted);

            X11Backend* _backend;
            Display*    _dpy;
            ::Window    _root;
            ::Window    _window  = 0;
            ::Window    _confine = 0;
            bool        _resizable    = true;
            bool        _grabbed      = false;
            bool        _hiddenState  = false;
            bool        _fullscreenActual = false;
            int         _captureAnchorX = 0;
            int         _captureAnchorY = 0;

            ::Window  _dndSource   = 0;
            int       _dndVersion  = 0;
            bool      _dndAccept   = false;
            bool      _dndDropping = false;
            InputVec2 _dndPos{};
        };

        // ---- X11Window ----

        X11Window::X11Window(X11Backend& backend, const WindowDesc& desc)
            : _backend(&backend), _dpy(backend.Dpy()), _root(backend.Root())
        {
            const Atoms& a = backend.A();

            _resizable      = desc.resizable;
            _fullscreenMode = desc.fullscreenMode;
            _width          = desc.width;
            _height         = desc.height;
            _dpiScale       = backend.DpiScale();

            XSetWindowAttributes attrs{};
            attrs.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                               PointerMotionMask | StructureNotifyMask | FocusChangeMask | PropertyChangeMask;
            attrs.background_pixmap = 0;
            attrs.border_pixel      = 0;
            attrs.cursor            = backend.DefaultCursor();

            const int x = desc.position ? desc.position->x : 0;
            const int y = desc.position ? desc.position->y : 0;
            _window = XCreateWindow(_dpy, _root, x, y, desc.width, desc.height, 0, CopyFromParent, InputOutput,
                                    CopyFromParent, CWEventMask | CWBackPixmap | CWBorderPixel | CWCursor, &attrs);
            if (!_window)
            {
                LOG_FATAL("failed to create window");
                return;
            }

            SetSizeHints(desc.position.has_value(), x, y, static_cast<int>(desc.width),
                         static_cast<int>(desc.height));

            XClassHint classHint{const_cast<char*>("lumora"), const_cast<char*>("Lumora")};
            XSetClassHint(_dpy, _window, &classHint);

            XWMHints wmHints{};
            wmHints.flags         = InputHint | StateHint;
            wmHints.input         = True;
            wmHints.initial_state = NormalState;
            XSetWMHints(_dpy, _window, &wmHints);

            SetTitleProperties(desc.title);

            Atom protocols[] = {a.wmDeleteWindow};
            XSetWMProtocols(_dpy, _window, protocols, 1);

            const long pid = static_cast<long>(::getpid());
            XChangeProperty(_dpy, _window, a.netWmPid, XA_CARDINAL, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&pid), 1);

            const long dndVersion = 5;
            XChangeProperty(_dpy, _window, a.xdndAware, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(&dndVersion), 1);

            if (desc.owner != nullptr)
            {
                const NativeWindowHandle owner = desc.owner->GetNativeHandle();
                if (owner.backend == WindowBackend::Xlib)
                    XSetTransientForHint(_dpy, _window, reinterpret_cast<::Window>(owner.window));
            }

            std::vector<Atom> state;
            if (desc.owner != nullptr && !desc.showInTaskbar) state.push_back(a.netWmStateSkipTaskbar);
            if (desc.fullscreenMode != FullscreenMode::Windowed)
            {
                state.push_back(a.netWmStateFullscreen);
                _fullscreenActual = true;
            }
            if (!state.empty())
                XChangeProperty(_dpy, _window, a.netWmState, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(state.data()), static_cast<int>(state.size()));
            if (desc.fullscreenMode == FullscreenMode::Exclusive)
            {
                const long on = 1;
                XChangeProperty(_dpy, _window, a.netWmBypassCompositor, XA_CARDINAL, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(&on), 1);
            }

            ::Window root, child;
            int      rx, ry, wx = 0, wy = 0;
            unsigned mask;
            XQueryPointer(_dpy, _window, &root, &child, &rx, &ry, &wx, &wy, &mask);
            _lastPointerPos = InputVec2{static_cast<float>(wx), static_cast<float>(wy)};

            XMapWindow(_dpy, _window);
            XFlush(_dpy);

            backend.Register(_window, this);
        }

        X11Window::~X11Window()
        {
            if (!_dpy) return;
            ReleaseCapture();
            _backend->Unregister(_window);
            XDestroyWindow(_dpy, _window);
            XFlush(_dpy);
        }

        void X11Window::SetSizeHints(bool userPosition, int x, int y, int w, int h)
        {
            XSizeHints hints{};
            hints.flags       = PWinGravity | USSize;
            hints.win_gravity = StaticGravity;
            hints.width       = w;
            hints.height      = h;
            if (userPosition)
            {
                hints.flags |= USPosition;
                hints.x = x;
                hints.y = y;
            }
            if (!_resizable)
            {
                hints.flags |= PMinSize | PMaxSize;
                hints.min_width = hints.max_width = w;
                hints.min_height = hints.max_height = h;
            }
            XSetWMNormalHints(_dpy, _window, &hints);
        }

        void X11Window::SetTitleProperties(std::string_view title)
        {
            const std::string text(title);
            Xutf8SetWMProperties(_dpy, _window, text.c_str(), text.c_str(), nullptr, 0, nullptr, nullptr, nullptr);
            XChangeProperty(_dpy, _window, _backend->A().netWmName, _backend->A().utf8String, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
        }

        void X11Window::SetTitle(std::string_view title)
        {
            if (!_dpy) return;
            SetTitleProperties(title);
            XFlush(_dpy);
        }

        ScreenRect X11Window::GetClientRect() const
        {
            if (!_dpy) return {};
            XWindowAttributes attr{};
            XGetWindowAttributes(_dpy, _window, &attr);
            int      x = 0, y = 0;
            ::Window child;
            XTranslateCoordinates(_dpy, _window, _root, 0, 0, &x, &y, &child);
            return {x, y, attr.width, attr.height};
        }

        void X11Window::SetClientRect(const ScreenRect& rect)
        {
            if (!_dpy) return;
            const int w = std::max(1, rect.width);
            const int h = std::max(1, rect.height);
            if (!_resizable) SetSizeHints(true, rect.x, rect.y, w, h);
            XMoveResizeWindow(_dpy, _window, rect.x, rect.y, static_cast<unsigned>(w), static_cast<unsigned>(h));
            XFlush(_dpy);
        }

        ScreenPoint X11Window::ClientToScreen(float x, float y) const
        {
            if (!_dpy) return {};
            int      sx = 0, sy = 0;
            ::Window child;
            XTranslateCoordinates(_dpy, _window, _root, static_cast<int>(x), static_cast<int>(y), &sx, &sy, &child);
            return {sx, sy};
        }

        void X11Window::Focus()
        {
            if (!_dpy) return;
            SendClientMessage(_dpy, _root, _window, _backend->A().netActiveWindow, {1, CurrentTime, 0, 0, 0},
                              SubstructureNotifyMask | SubstructureRedirectMask);
            XWindowAttributes attr{};
            if (XGetWindowAttributes(_dpy, _window, &attr) && attr.map_state == IsViewable)
            {
                XRaiseWindow(_dpy, _window);
                XSetInputFocus(_dpy, _window, RevertToParent, CurrentTime);
            }
            XFlush(_dpy);
        }

        bool X11Window::IsHidden() const
        {
            if (!_dpy) return true;
            XWindowAttributes attr{};
            XGetWindowAttributes(_dpy, _window, &attr);
            return _hiddenState || attr.map_state != IsViewable;
        }

        void X11Window::SetFullscreenMode(FullscreenMode mode)
        {
            if (!_dpy || _fullscreenMode == mode) return;
            const Atoms& a = _backend->A();

            // Exclusive only asks the compositor to step aside; X11 has no portable mode switch to do the real thing.
            if (mode == FullscreenMode::Exclusive)
            {
                const long on = 1;
                XChangeProperty(_dpy, _window, a.netWmBypassCompositor, XA_CARDINAL, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char*>(&on), 1);
            }
            else
            {
                XDeleteProperty(_dpy, _window, a.netWmBypassCompositor);
            }

            const bool wantFullscreen = mode != FullscreenMode::Windowed;
            if (wantFullscreen != (_fullscreenMode != FullscreenMode::Windowed))
            {
                SendClientMessage(_dpy, _root, _window, a.netWmState,
                                  {wantFullscreen ? 1 : 0, static_cast<long>(a.netWmStateFullscreen), 0, 1, 0},
                                  SubstructureNotifyMask | SubstructureRedirectMask);
            }
            _fullscreenMode = mode;
            XFlush(_dpy);
        }

        bool X11Window::ApplyCapture()
        {
            XWindowAttributes attr{};
            XGetWindowAttributes(_dpy, _window, &attr);
            const int cw = attr.width;
            const int ch = attr.height;

            // Intersected with the client area rather than trusted: a panel rect published before a shrink could
            // confine the pointer outside the window.
            int x0 = 0, y0 = 0, x1 = cw, y1 = ch;
            if (!_cursorClip.Empty())
            {
                x0 = std::max(0, _cursorClip.x);
                y0 = std::max(0, _cursorClip.y);
                x1 = std::min(cw, _cursorClip.x + _cursorClip.width);
                y1 = std::min(ch, _cursorClip.y + _cursorClip.height);
                if (x1 <= x0 || y1 <= y0)
                {
                    x0 = y0 = 0;
                    x1 = cw;
                    y1 = ch;
                }
            }
            const int w = x1 - x0;
            const int h = y1 - y0;
            if (w <= 0 || h <= 0) return false;

            if (!_confine)
            {
                _confine = XCreateWindow(_dpy, _window, x0, y0, static_cast<unsigned>(w), static_cast<unsigned>(h), 0, 0,
                                         InputOnly, CopyFromParent, 0, nullptr);
                XMapWindow(_dpy, _confine);
            }
            else
            {
                XMoveResizeWindow(_dpy, _confine, x0, y0, static_cast<unsigned>(w), static_cast<unsigned>(h));
            }

            _captureAnchorX = x0 + w / 2;
            _captureAnchorY = y0 + h / 2;

            const int result = XGrabPointer(_dpy, _window, True,
                                            ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync,
                                            GrabModeAsync, _confine, _backend->BlankCursor(), CurrentTime);
            if (result != GrabSuccess)
            {
                LOG_WARN("XGrabPointer failed ({})", result);
                return false;
            }
            _grabbed = true;
            XWarpPointer(_dpy, 0, _window, 0, 0, 0, 0, _captureAnchorX, _captureAnchorY);
            XFlush(_dpy);
            return true;
        }

        void X11Window::ReleaseCapture()
        {
            if (_grabbed)
            {
                XUngrabPointer(_dpy, CurrentTime);
                _grabbed = false;
            }
            if (_confine)
            {
                XDestroyWindow(_dpy, _confine);
                _confine = 0;
            }

            ::Window root, child;
            int      rx, ry, wx = 0, wy = 0;
            unsigned mask;
            if (XQueryPointer(_dpy, _window, &root, &child, &rx, &ry, &wx, &wy, &mask))
                _lastPointerPos = InputVec2{static_cast<float>(wx), static_cast<float>(wy)};
            XFlush(_dpy);
        }

        void X11Window::SetCursorMode(CursorMode mode, const CursorClip& clip)
        {
            if (!_dpy) return;

            // The clip is part of the comparison: re-clipping while captured is the per-frame case and must not early-out.
            if (_cursorMode == mode && (mode == CursorMode::Normal || _cursorClip == clip)) return;

            if (mode == CursorMode::Captured)
            {
                _cursorClip = clip;
                if (ApplyCapture())
                {
                    _cursorMode = CursorMode::Captured;
                    return;
                }
            }
            ReleaseCapture();
            _cursorMode = CursorMode::Normal;
        }

        void X11Window::SetDpiScale(float scale)
        {
            _dpiScale = scale;
            Push(WindowEventType::DpiChanged);
        }

        void X11Window::RefreshState()
        {
            const Atoms& a = _backend->A();
            bool         hidden = false, fullscreen = false;
            for (const long atom : ReadLongs(_dpy, _window, a.netWmState))
            {
                hidden |= static_cast<Atom>(atom) == a.netWmStateHidden;
                fullscreen |= static_cast<Atom>(atom) == a.netWmStateFullscreen;
            }
            _hiddenState = hidden;

            // Only a change of the real state is adopted, so a request still in flight is not overwritten.
            if (fullscreen != _fullscreenActual)
            {
                _fullscreenActual = fullscreen;
                if (fullscreen && _fullscreenMode == FullscreenMode::Windowed)
                    _fullscreenMode = FullscreenMode::Borderless;
                else if (!fullscreen && _fullscreenMode != FullscreenMode::Windowed)
                {
                    _fullscreenMode = FullscreenMode::Windowed;
                    XDeleteProperty(_dpy, _window, a.netWmBypassCompositor);
                }
            }
        }

        void X11Window::HandleEvent(XEvent& ev)
        {
            switch (ev.type)
            {
            case ConfigureNotify: OnConfigure(ev.xconfigure); break;
            case ClientMessage: OnClientMessage(ev.xclient); break;
            case FocusIn:
            case FocusOut:
                {
                    const XFocusChangeEvent& f = ev.xfocus;
                    if (f.mode == NotifyGrab || f.mode == NotifyUngrab || f.detail == NotifyPointer ||
                        f.detail == NotifyPointerRoot || f.detail == NotifyDetailNone)
                        break;
                    if (ev.type == FocusIn)
                    {
                        Push(WindowEventType::FocusGained);
                    }
                    else
                    {
                        SetCursorMode(CursorMode::Normal);
                        _backend->Kb().ResetCompose();
                        Push(WindowEventType::FocusLost);
                    }
                    break;
                }
            case MotionNotify: OnMotion(ev.xmotion); break;
            case ButtonPress:
            case ButtonRelease: OnButton(ev.xbutton); break;
            case KeyPress:
            case KeyRelease: OnKey(ev.xkey); break;
            case PropertyNotify:
                if (ev.xproperty.atom == _backend->A().netWmState) RefreshState();
                break;
            case SelectionNotify: OnSelection(ev.xselection); break;
            default: break;
            }
        }

        void X11Window::OnConfigure(const XConfigureEvent& ev)
        {
            const auto w = static_cast<uint32_t>(ev.width);
            const auto h = static_cast<uint32_t>(ev.height);
            if (w == _width && h == _height) return;

            _width  = w;
            _height = h;
            if (_cursorMode == CursorMode::Captured && !ApplyCapture())
            {
                ReleaseCapture();
                _cursorMode = CursorMode::Normal;
            }
            _pending.push_back({WindowEventType::Resize, w, h, _dpiScale});
        }

        void X11Window::OnClientMessage(const XClientMessageEvent& ev)
        {
            const Atoms& a = _backend->A();
            if (ev.message_type == a.wmProtocols)
            {
                if (static_cast<Atom>(ev.data.l[0]) == a.wmDeleteWindow)
                {
                    _shouldClose = true;
                    Push(WindowEventType::Close);
                }
            }
            else if (ev.message_type == a.xdndEnter) OnDndEnter(ev);
            else if (ev.message_type == a.xdndPosition) OnDndPosition(ev);
            else if (ev.message_type == a.xdndDrop) OnDndDrop(ev);
            else if (ev.message_type == a.xdndLeave)
            {
                _dndSource = 0;
                _dndAccept = false;
            }
        }

        void X11Window::OnMotion(const XMotionEvent& ev)
        {
            const KeyMods mods = _backend->Kb().Mods();
            if (_cursorMode == CursorMode::Captured)
            {
                // Raw motion supplies the deltas when XInput2 is there; otherwise recentre and measure from the anchor.
                if (_backend->HasRawMotion()) return;

                const InputVec2 delta{static_cast<float>(ev.x - _captureAnchorX),
                                      static_cast<float>(ev.y - _captureAnchorY)};
                if (delta.x == 0.f && delta.y == 0.f) return;

                InputEvent e{};
                e.type     = InputEventType::PointerMove;
                e.position = _lastPointerPos;
                e.delta    = delta;
                e.mods     = mods;
                _pendingInput.push_back(e);
                XWarpPointer(_dpy, 0, _window, 0, 0, 0, 0, _captureAnchorX, _captureAnchorY);
                return;
            }

            const InputVec2 pos{static_cast<float>(ev.x), static_cast<float>(ev.y)};
            InputEvent      e{};
            e.type     = InputEventType::PointerMove;
            e.position = pos;
            e.delta    = {pos.x - _lastPointerPos.x, pos.y - _lastPointerPos.y};
            e.mods     = mods;
            _pendingInput.push_back(e);
            _lastPointerPos = pos;
        }

        void X11Window::OnRawMotion(double dx, double dy)
        {
            if (_cursorMode != CursorMode::Captured || (dx == 0.0 && dy == 0.0)) return;
            InputEvent e{};
            e.type     = InputEventType::PointerMove;
            e.position = _lastPointerPos;
            e.delta    = {static_cast<float>(dx), static_cast<float>(dy)};
            e.mods     = _backend->Kb().Mods();
            _pendingInput.push_back(e);
        }

        void X11Window::OnButton(const XButtonEvent& ev)
        {
            const bool      down = ev.type == ButtonPress;
            const InputVec2 pos{static_cast<float>(ev.x), static_cast<float>(ev.y)};
            InputEvent      e{};
            e.position = pos;
            e.mods     = _backend->Kb().Mods();

            switch (ev.button)
            {
            case 1: e.button = PointerButton::Left; break;
            case 2: e.button = PointerButton::Middle; break;
            case 3: e.button = PointerButton::Right; break;
            case 8: e.button = PointerButton::X1; break;
            case 9: e.button = PointerButton::X2; break;
            case 4:
            case 5:
            case 6:
            case 7:
                if (!down) return;
                e.type = InputEventType::Scroll;
                if (ev.button == 4) e.scrollDelta.y = 1.f;
                else if (ev.button == 5) e.scrollDelta.y = -1.f;
                else if (ev.button == 6) e.scrollDelta.x = -1.f;
                else e.scrollDelta.x = 1.f;
                _pendingInput.push_back(e);
                return;
            default: return;
            }
            e.type = down ? InputEventType::PointerDown : InputEventType::PointerUp;
            _pendingInput.push_back(e);
        }

        void X11Window::OnKey(const XKeyEvent& ev)
        {
            const bool     down = ev.type == KeyPress;
            const uint32_t code = ev.keycode - 8;
            const KeyTranslation tr = down ? _backend->Kb().Press(code) : _backend->Kb().Release(code);

            InputEvent e{};
            e.type = down ? InputEventType::KeyDown : InputEventType::KeyUp;
            e.key  = tr.key;
            e.mods = tr.mods;
            _pendingInput.push_back(e);

            if (down && tr.text[0] != '\0')
            {
                InputEvent t{};
                t.type = InputEventType::Text;
                std::strncpy(t.text, tr.text, sizeof(t.text) - 1);
                t.mods = tr.mods;
                _pendingInput.push_back(t);
            }
        }

        void X11Window::OnDndEnter(const XClientMessageEvent& ev)
        {
            const Atoms& a = _backend->A();
            _dndSource     = static_cast<::Window>(ev.data.l[0]);
            _dndVersion    = static_cast<int>(ev.data.l[1] >> 24);
            _dndAccept     = false;
            _dndDropping   = false;
            if (_dndVersion > 5) return;

            std::vector<long> types;
            if (ev.data.l[1] & 1)
                types = ReadLongs(_dpy, _dndSource, a.xdndTypeList);
            else
                types.assign(ev.data.l + 2, ev.data.l + 5);
            for (const long t : types)
                _dndAccept |= static_cast<Atom>(t) == a.textUriList;
        }

        void X11Window::OnDndPosition(const XClientMessageEvent& ev)
        {
            const Atoms& a = _backend->A();
            if (static_cast<::Window>(ev.data.l[0]) != _dndSource) return;

            const int rootX = static_cast<int>((ev.data.l[2] >> 16) & 0xFFFF);
            const int rootY = static_cast<int>(ev.data.l[2] & 0xFFFF);
            int       cx = 0, cy = 0;
            ::Window  child;
            XTranslateCoordinates(_dpy, _root, _window, rootX, rootY, &cx, &cy, &child);
            _dndPos = {static_cast<float>(cx), static_cast<float>(cy)};

            SendClientMessage(_dpy, _dndSource, _window, a.xdndStatus,
                              {static_cast<long>(_window), _dndAccept ? 1 : 0, 0, 0,
                               _dndAccept ? static_cast<long>(a.xdndActionCopy) : 0},
                              NoEventMask);
            XFlush(_dpy);
        }

        void X11Window::OnDndDrop(const XClientMessageEvent& ev)
        {
            if (static_cast<::Window>(ev.data.l[0]) != _dndSource) return;
            if (!_dndAccept)
            {
                FinishDrop(false);
                return;
            }
            _dndDropping = true;
            const Time stamp = _dndVersion >= 1 ? static_cast<Time>(ev.data.l[2]) : CurrentTime;
            XConvertSelection(_dpy, _backend->A().xdndSelection, _backend->A().textUriList,
                              _backend->A().lumoraSelection, _window, stamp);
            XFlush(_dpy);
        }

        void X11Window::OnSelection(const XSelectionEvent& ev)
        {
            const Atoms& a = _backend->A();
            if (ev.selection != a.xdndSelection || !_dndDropping) return;

            bool ok = false;
            if (ev.property != 0)
            {
                const std::string data = ReadProperty(_dpy, _window, ev.property, true);
                FileDropEvent     drop{};
                drop.paths    = ParseUriList(data);
                drop.position = _dndPos;
                if (!drop.paths.empty())
                {
                    _pendingDrops.push_back(std::move(drop));
                    ok = true;
                }
            }
            FinishDrop(ok);
        }

        void X11Window::FinishDrop(bool accepted)
        {
            const Atoms& a = _backend->A();
            if (_dndSource != 0 && _dndVersion >= 2)
            {
                SendClientMessage(_dpy, _dndSource, _window, a.xdndFinished,
                                  {static_cast<long>(_window), accepted ? 1 : 0,
                                   accepted ? static_cast<long>(a.xdndActionCopy) : 0, 0, 0},
                                  NoEventMask);
                XFlush(_dpy);
            }
            _dndSource   = 0;
            _dndAccept   = false;
            _dndDropping = false;
        }

        // ---- X11Backend ----

        X11Backend::X11Backend(Display* dpy)
            : _dpy(dpy), _screen(DefaultScreen(dpy)), _root(RootWindow(dpy, DefaultScreen(dpy)))
        {
            _a.Init(_dpy);
            XSetErrorHandler(&ErrorHandler);

            if (_keyboard.Context() == nullptr) return;
            if (!InitXkb()) return;
            InitXInput();
            InitRandr();

            if (!(_defaultCursor = XcursorLibraryLoadCursor(_dpy, "left_ptr")))
                _defaultCursor = XCreateFontCursor(_dpy, XC_left_ptr);

            const char data[1] = {0};
            const Pixmap blank = XCreateBitmapFromData(_dpy, _root, data, 1, 1);
            XColor       black{};
            _blankCursor = XCreatePixmapCursor(_dpy, blank, blank, &black, &black, 0, 0);
            XFreePixmap(_dpy, blank);

            XrmInitialize();
            _dpiScale = ReadDpiScale();
            XSelectInput(_dpy, _root, PropertyChangeMask);
            XFlush(_dpy);
            _ok = true;
        }

        X11Backend::~X11Backend()
        {
            for (auto& [id, window] : _windows)
                window->Detach();
            _windows.clear();
            _clipboard.reset();
            if (_blankCursor) XFreeCursor(_dpy, _blankCursor);
            if (_defaultCursor) XFreeCursor(_dpy, _defaultCursor);
            XCloseDisplay(_dpy);
        }

        bool X11Backend::InitXkb()
        {
            int opcode = 0, errorBase = 0, major = 1, minor = 0;
            if (!XkbQueryExtension(_dpy, &opcode, &_xkbEvent, &errorBase, &major, &minor))
            {
                LOG_ERROR("the X server has no XKB extension");
                return false;
            }
            _xcb = XGetXCBConnection(_dpy);
            if (!_xcb || !xkb_x11_setup_xkb_extension(_xcb, 1, 0, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr,
                                                      nullptr, nullptr, nullptr))
            {
                LOG_ERROR("xkb_x11_setup_xkb_extension failed");
                return false;
            }
            _coreDevice = xkb_x11_get_core_keyboard_device_id(_xcb);
            if (_coreDevice < 0 || !LoadKeymap()) return false;

            XkbSetDetectableAutoRepeat(_dpy, True, nullptr);
            const unsigned long events = XkbNewKeyboardNotifyMask | XkbMapNotifyMask | XkbStateNotifyMask;
            XkbSelectEvents(_dpy, XkbUseCoreKbd, events, events);
            return true;
        }

        bool X11Backend::LoadKeymap()
        {
            xkb_keymap* keymap = xkb_x11_keymap_new_from_device(_keyboard.Context(), _xcb, _coreDevice,
                                                                XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (!keymap)
            {
                LOG_ERROR("could not load the X keyboard map");
                return false;
            }
            _keyboard.SetKeymap(keymap);
            SyncKeyboardState();
            return true;
        }

        void X11Backend::SyncKeyboardState()
        {
            XkbStateRec st{};
            if (XkbGetState(_dpy, XkbUseCoreKbd, &st) == Success)
                _keyboard.UpdateMask(st.base_mods, st.latched_mods, st.locked_mods, st.base_group, st.latched_group,
                                     st.locked_group);
        }

        void X11Backend::InitXInput()
        {
            int event = 0, error = 0;
            if (!XQueryExtension(_dpy, "XInputExtension", &_xiOpcode, &event, &error)) return;

            int major = 2, minor = 0;
            if (XIQueryVersion(_dpy, &major, &minor) != Success) return;

            unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
            XISetMask(mask, XI_RawMotion);
            XIEventMask em{};
            em.deviceid = XIAllMasterDevices;
            em.mask_len = sizeof(mask);
            em.mask     = mask;
            XISelectEvents(_dpy, _root, &em, 1);
            _hasRawMotion = true;
        }

        void X11Backend::InitRandr()
        {
            int event = 0, error = 0;
            if (!XRRQueryExtension(_dpy, &event, &error)) return;
            int major = 0, minor = 0;
            _randr15 = XRRQueryVersion(_dpy, &major, &minor) && (major > 1 || (major == 1 && minor >= 5));
        }

        float X11Backend::ReadDpiScale()
        {
            const std::string resources = ReadProperty(_dpy, _root, XA_RESOURCE_MANAGER, false);
            if (resources.empty()) return 1.0f;

            XrmDatabase db = XrmGetStringDatabase(resources.c_str());
            if (!db) return 1.0f;
            float    scale = 1.0f;
            char*    type  = nullptr;
            XrmValue value{};
            if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &value) && value.addr)
            {
                const double dpi = std::atof(value.addr);
                if (dpi >= 24.0) scale = static_cast<float>(dpi / 96.0);
            }
            XrmDestroyDatabase(db);
            return scale;
        }

        void X11Backend::RefreshDpi()
        {
            const float scale = ReadDpiScale();
            if (std::abs(scale - _dpiScale) < 0.001f) return;
            _dpiScale = scale;
            for (auto& [id, window] : _windows)
                window->SetDpiScale(scale);
        }

        X11Window* X11Backend::Find(::Window id) const
        {
            const auto it = _windows.find(id);
            return it == _windows.end() ? nullptr : it->second;
        }

        Ref<Window> X11Backend::CreateWindow(const WindowDesc& desc)
        {
            Ref<X11Window> window = MakeRef<X11Window>(*this, desc);
            if (!window->Valid()) return nullptr;
            return window;
        }

        void X11Backend::OnXkb(const XEvent& ev)
        {
            const auto* xe = reinterpret_cast<const XkbEvent*>(&ev);
            switch (xe->any.xkb_type)
            {
            case XkbStateNotify:
                _keyboard.UpdateMask(xe->state.base_mods, xe->state.latched_mods, xe->state.locked_mods,
                                     xe->state.base_group, xe->state.latched_group, xe->state.locked_group);
                break;
            case XkbNewKeyboardNotify:
            case XkbMapNotify: LoadKeymap(); break;
            default: break;
            }
        }

        void X11Backend::OnGeneric(XEvent& ev)
        {
            if (!_hasRawMotion || ev.xcookie.extension != _xiOpcode || !XGetEventData(_dpy, &ev.xcookie)) return;

            if (ev.xcookie.evtype == XI_RawMotion)
            {
                const auto*          raw    = static_cast<const XIRawEvent*>(ev.xcookie.data);
                const unsigned char* mask   = raw->valuators.mask;
                const double*        values = raw->valuators.values;
                double               dx = 0.0, dy = 0.0;
                int                  index = 0;
                for (int bit = 0; bit < raw->valuators.mask_len * 8; ++bit)
                {
                    if (!XIMaskIsSet(mask, bit)) continue;
                    if (bit == 0) dx = values[index];
                    else if (bit == 1) dy = values[index];
                    ++index;
                }
                for (auto& [id, window] : _windows)
                    window->OnRawMotion(dx, dy);
            }
            XFreeEventData(_dpy, &ev.xcookie);
        }

        void X11Backend::Dispatch(XEvent& ev)
        {
            if (ev.type == _xkbEvent)
            {
                OnXkb(ev);
                return;
            }
            if (ev.type == GenericEvent)
            {
                OnGeneric(ev);
                return;
            }
            if (ev.type == PropertyNotify && ev.xproperty.window == _root)
            {
                if (ev.xproperty.atom == XA_RESOURCE_MANAGER) RefreshDpi();
                return;
            }
            if (X11Window* window = Find(ev.xany.window)) window->HandleEvent(ev);
        }

        void X11Backend::Pump()
        {
            while (XPending(_dpy))
            {
                XEvent ev;
                XNextEvent(_dpy, &ev);
                Dispatch(ev);
            }
            XFlush(_dpy);
        }

        void X11Backend::Wait()
        {
            XFlush(_dpy);
            if (XPending(_dpy)) return;

            pollfd fds[2] = {{ConnectionNumber(_dpy), POLLIN, 0}, {WakeFd(), POLLIN, 0}};
            ::poll(fds, 2, -1);
            if (fds[1].revents & POLLIN) DrainWake();
        }

        ScreenPoint X11Backend::CursorPosition()
        {
            ::Window root, child;
            int      rx = 0, ry = 0, wx, wy;
            unsigned mask;
            XQueryPointer(_dpy, _root, &root, &child, &rx, &ry, &wx, &wy, &mask);
            return {rx, ry};
        }

        bool X11Backend::IsTopLevelOf(::Window window, ::Window top)
        {
            while (window != 0 && window != _root)
            {
                if (window == top) return true;
                ::Window  root, parent;
                ::Window* kids = nullptr;
                unsigned  count = 0;
                if (!XQueryTree(_dpy, window, &root, &parent, &kids, &count)) return false;
                if (kids) XFree(kids);
                window = parent;
            }
            return false;
        }

        Window* X11Backend::WindowAt(const ScreenPoint point)
        {
            ::Window  root, parent;
            ::Window* kids  = nullptr;
            unsigned  count = 0;
            if (!XQueryTree(_dpy, _root, &root, &parent, &kids, &count)) return nullptr;

            // Children come bottom to top; the first one under the point is what the user sees there.
            X11Window* hit   = nullptr;
            bool       found = false;
            for (unsigned i = count; i-- > 0 && !found;)
            {
                XWindowAttributes attr{};
                if (!XGetWindowAttributes(_dpy, kids[i], &attr) || attr.map_state != IsViewable ||
                    attr.c_class == InputOnly)
                    continue;
                if (point.x < attr.x || point.y < attr.y || point.x >= attr.x + attr.width + 2 * attr.border_width ||
                    point.y >= attr.y + attr.height + 2 * attr.border_width)
                    continue;

                found = true;
                for (auto& [id, window] : _windows)
                {
                    if (IsTopLevelOf(id, kids[i]))
                    {
                        hit = window;
                        break;
                    }
                }
            }
            if (kids) XFree(kids);

            return hit && hit->GetClientRect().Contains(point) ? hit : nullptr;
        }

        std::vector<ScreenRect> X11Backend::Monitors(std::size_t& primary)
        {
            std::vector<ScreenRect> out;
            primary = 0;
            if (_randr15)
            {
                int             count    = 0;
                XRRMonitorInfo* monitors = XRRGetMonitors(_dpy, _root, True, &count);
                for (int i = 0; i < count; ++i)
                {
                    out.push_back({monitors[i].x, monitors[i].y, monitors[i].width, monitors[i].height});
                    if (monitors[i].primary) primary = out.size() - 1;
                }
                if (monitors) XRRFreeMonitors(monitors);
            }
            if (out.empty()) out.push_back({0, 0, DisplayWidth(_dpy, _screen), DisplayHeight(_dpy, _screen)});
            return out;
        }

        bool X11Backend::IsOnAnyMonitor(const ScreenRect& rect)
        {
            std::size_t primary = 0;
            for (const ScreenRect& m : Monitors(primary))
            {
                if (rect.x < m.x + m.width && rect.x + rect.width > m.x && rect.y < m.y + m.height &&
                    rect.y + rect.height > m.y)
                    return true;
            }
            return false;
        }

        ScreenRect X11Backend::PrimaryWorkArea()
        {
            std::size_t             primary  = 0;
            const auto              monitors = Monitors(primary);
            const ScreenRect&       monitor  = monitors[primary];

            // _NET_WORKAREA spans every screen, so it is only trusted after being clipped to the primary monitor.
            const std::vector<long> desktop  = ReadLongs(_dpy, _root, _a.netCurrentDesktop);
            const std::vector<long> areas    = ReadLongs(_dpy, _root, _a.netWorkarea);
            const std::size_t       index    = desktop.empty() ? 0 : static_cast<std::size_t>(desktop[0]);
            if (areas.size() < (index + 1) * 4) return monitor;

            const int x0 = std::max(monitor.x, static_cast<int>(areas[index * 4]));
            const int y0 = std::max(monitor.y, static_cast<int>(areas[index * 4 + 1]));
            const int x1 = std::min(monitor.x + monitor.width, static_cast<int>(areas[index * 4] + areas[index * 4 + 2]));
            const int y1 = std::min(monitor.y + monitor.height, static_cast<int>(areas[index * 4 + 1] + areas[index * 4 + 3]));
            if (x1 <= x0 || y1 <= y0) return monitor;
            return {x0, y0, x1 - x0, y1 - y0};
        }

        bool X11Backend::SetClipboard(std::string_view text)
        {
            X11Clipboard* clipboard = Clipboard();
            return clipboard && clipboard->Set(text);
        }

        std::string X11Backend::GetClipboard()
        {
            X11Clipboard* clipboard = Clipboard();
            return clipboard ? clipboard->Get() : std::string{};
        }

        X11Clipboard* X11Backend::Clipboard()
        {
            std::call_once(_clipboardOnce, [this]
            {
                auto clipboard = std::make_unique<X11Clipboard>(_a);
                if (clipboard->Start()) _clipboard = std::move(clipboard);
                else LOG_WARN("clipboard connection failed");
            });
            return _clipboard.get();
        }
    }

    std::unique_ptr<DisplayBackend> MakeX11Backend()
    {
        static std::once_flag threads;
        std::call_once(threads, [] { XInitThreads(); });

        Display* dpy = XOpenDisplay(nullptr);
        if (!dpy) return nullptr;

        auto backend = std::make_unique<X11Backend>(dpy);
        if (!backend->Ok()) return nullptr;
        return backend;
    }
}
