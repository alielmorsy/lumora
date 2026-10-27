#include "Keyboard.h"
#include "LinuxWindow.h"

#include "lumora/log/Log.h"
#include "lumora/os/Environment.h"

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include <linux/input-event-codes.h>

#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#if defined(LUMORA_HAVE_LIBDECOR)
#include <libdecor.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("Platform.Wayland")

namespace lumora::platform
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        constexpr int         kTransferTimeoutMs = 500;
        constexpr std::size_t kMaxTransferBytes  = 16u << 20;
        constexpr const char* kTextMimes[]       = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"};
        constexpr const char* kUriListMime       = "text/uri-list";

        class WaylandBackend;
        class WaylandWindow;

        WaylandBackend* g_backend = nullptr;

        struct Output
        {
            wl_output* output = nullptr;
            uint32_t   name   = 0;
            int32_t    x = 0, y = 0;
            int32_t    width = 0, height = 0;
            int32_t    scale     = 1;
            int32_t    transform = 0;

            [[nodiscard]] ScreenRect Rect() const
            {
                // Mode sizes are physical, geometry x/y are compositor-logical: scaling the origin keeps side-by-side outputs of
                // one scale adjacent, which is all a layout without global coordinates can promise.
                const bool swap = (transform & 1) != 0;
                return {x * scale, y * scale, swap ? height : width, swap ? width : height};
            }
        };

        struct Offer
        {
            wl_data_offer*           offer = nullptr;
            std::vector<std::string> mimes;

            [[nodiscard]] bool Has(std::string_view mime) const
            {
                return std::ranges::any_of(mimes, [&](const std::string& m) { return m == mime; });
            }
        };

        std::string ReadAll(const int fd, const int timeoutMs)
        {
            std::string out;
            char        buf[4096];
            for (;;)
            {
                pollfd pfd{fd, POLLIN, 0};
                const int ready = ::poll(&pfd, 1, timeoutMs);
                if (ready < 0 && errno == EINTR) continue;
                if (ready <= 0) break;

                const ssize_t n = ::read(fd, buf, sizeof(buf));
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                out.append(buf, static_cast<std::size_t>(n));
                if (out.size() > kMaxTransferBytes) break;
            }
            return out;
        }

        /// A reader that closed early raises SIGPIPE on write; block it for the duration and swallow what it queued.
        class SigPipeGuard
        {
        public:
            SigPipeGuard()
            {
                sigemptyset(&_set);
                sigaddset(&_set, SIGPIPE);
                pthread_sigmask(SIG_BLOCK, &_set, &_old);
            }

            ~SigPipeGuard()
            {
                const timespec none{};
                while (::sigtimedwait(&_set, nullptr, &none) > 0) {}
                pthread_sigmask(SIG_SETMASK, &_old, nullptr);
            }

            SigPipeGuard(const SigPipeGuard&)            = delete;
            SigPipeGuard& operator=(const SigPipeGuard&) = delete;

        private:
            sigset_t _set{};
            sigset_t _old{};
        };

        void WriteAllAndClose(const int fd, const std::string& text)
        {
            ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
            const SigPipeGuard guard;

            std::size_t offset = 0;
            while (offset < text.size())
            {
                const ssize_t n = ::write(fd, text.data() + offset, text.size() - offset);
                if (n > 0)
                {
                    offset += static_cast<std::size_t>(n);
                    continue;
                }
                if (n < 0 && errno == EINTR) continue;
                if (n < 0 && errno == EAGAIN)
                {
                    pollfd pfd{fd, POLLOUT, 0};
                    if (::poll(&pfd, 1, kTransferTimeoutMs) > 0) continue;
                }
                break;
            }
            ::close(fd);
        }

        int HexValue(const char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        std::vector<std::string> ParseUriList(const std::string_view text)
        {
            std::vector<std::string> paths;
            std::size_t              pos = 0;
            while (pos < text.size())
            {
                std::size_t end = text.find_first_of("\r\n", pos);
                if (end == std::string_view::npos) end = text.size();
                std::string_view line = text.substr(pos, end - pos);
                pos                   = end + 1;

                constexpr std::string_view kScheme = "file://";
                if (line.empty() || line.front() == '#' || !line.starts_with(kScheme)) continue;
                line.remove_prefix(kScheme.size());
                if (const std::size_t slash = line.find('/'); slash != std::string_view::npos) line.remove_prefix(slash);

                std::string path;
                for (std::size_t i = 0; i < line.size(); ++i)
                {
                    const int hi = line[i] == '%' && i + 2 < line.size() ? HexValue(line[i + 1]) : -1;
                    const int lo = hi >= 0 ? HexValue(line[i + 2]) : -1;
                    if (hi >= 0 && lo >= 0)
                    {
                        path.push_back(static_cast<char>(hi * 16 + lo));
                        i += 2;
                    }
                    else
                    {
                        path.push_back(line[i]);
                    }
                }
                if (!path.empty()) paths.push_back(std::move(path));
            }
            return paths;
        }

        class WaylandWindow final : public LinuxWindow
        {
        public:
            WaylandWindow(WaylandBackend& backend, const WindowDesc& desc);
            ~WaylandWindow() override;

            [[nodiscard]] bool IsValid() const { return _surface != nullptr && !_failed; }
            [[nodiscard]] wl_surface* Surface() const { return _surface; }

            [[nodiscard]] NativeWindowHandle GetNativeHandle() const override;
            void SetTitle(std::string_view title) override;
            void SetFullscreenMode(FullscreenMode mode) override;
            void SetCursorMode(CursorMode mode, const CursorClip& clip) override;
            using Window::SetCursorMode;

            [[nodiscard]] ScreenRect  GetClientRect() const override { return {0, 0, static_cast<int32_t>(_width), static_cast<int32_t>(_height)}; }
            void                      SetClientRect(const ScreenRect& rect) override;
            [[nodiscard]] ScreenPoint ClientToScreen(float x, float y) const override { return {static_cast<int32_t>(x), static_cast<int32_t>(y)}; }
            void                      Focus() override {}
            [[nodiscard]] bool        IsHidden() const override { return _hidden; }

            void Emit(const InputEvent& e) { _pendingInput.push_back(e); }
            void EmitWindow(const WindowEventType type) { _pending.push_back({type, _width, _height, _dpiScale}); }
            void EmitKey(bool pressed, const KeyTranslation& t);
            void EmitDrop(FileDropEvent&& e) { _pendingDrops.push_back(std::move(e)); }
            void ForceClose();
            [[nodiscard]] InputVec2 LastPointer() const { return _lastPointerPos; }
            void SetLastPointer(const InputVec2 p) { _lastPointerPos = p; }
            [[nodiscard]] int Scale() const { return _scale; }

            void OnToplevelConfigure(int32_t w, int32_t h, const wl_array* states);
            void OnSurfaceConfigure(uint32_t serial);
            void ApplyConfigure(int32_t w, int32_t h, bool fullscreen, bool suspended);
            void OnSurfaceEnter(wl_output* output);
            void OnSurfaceLeave(wl_output* output);
            void OnPreferredScale(int32_t scale);
            void SetScale(int32_t scale);
            void RecomputePhysical();

#if defined(LUMORA_HAVE_LIBDECOR)
            void OnFrameConfigure(libdecor_configuration* configuration);
            void OnFrameCommit();
#endif

            bool                         _configured = false;
            bool                         _failed     = false;
            wl_surface*                  _surface    = nullptr;
            xdg_surface*                 _xdgSurface = nullptr;
            xdg_toplevel*                _toplevel   = nullptr;
            zxdg_toplevel_decoration_v1* _decoration = nullptr;
            zwp_locked_pointer_v1*       _locked     = nullptr;
#if defined(LUMORA_HAVE_LIBDECOR)
            libdecor_frame* _frame = nullptr;
#endif

        private:
            WaylandBackend&      _be;
            int32_t              _logicalW = 0;
            int32_t              _logicalH = 0;
            int32_t              _scale    = 1;
            bool                 _hidden   = false;
            bool                 _scaleFromOutputs = false;
            std::optional<bool>  _fsRequested;
            int32_t              _windowedW = 0, _windowedH = 0;
            int32_t              _cfgW = 0, _cfgH = 0;
            bool                 _cfgFullscreen = false, _cfgSuspended = false;
            std::vector<wl_output*> _entered;
        };

        class WaylandBackend final : public DisplayBackend
        {
        public:
            WaylandBackend() = default;
            ~WaylandBackend() override;

            bool Init();

            [[nodiscard]] WindowBackend Kind() const override { return WindowBackend::Wayland; }
            [[nodiscard]] Ref<Window>   CreateWindow(const WindowDesc& desc) override;
            void                        Pump() override;
            void                        Wait() override;
            [[nodiscard]] ScreenPoint   CursorPosition() override;
            [[nodiscard]] Window*       WindowAt(ScreenPoint point) override;
            [[nodiscard]] bool          IsOnAnyMonitor(const ScreenRect& rect) override;
            [[nodiscard]] ScreenRect    PrimaryWorkArea() override;
            bool                        SetClipboard(std::string_view text) override;
            std::string                 GetClipboard() override;

            // Registry
            void OnGlobal(wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
            void OnGlobalRemove(uint32_t name);
            void AddOutput(wl_registry* registry, uint32_t name, uint32_t version);

            // Seat
            void OnSeatCapabilities(uint32_t caps);
            void MaybeCreateDataDevice();

            // Pointer
            void OnPointerEnter(uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
            void OnPointerLeave(wl_surface* surface);
            void OnPointerMotion(wl_fixed_t x, wl_fixed_t y);
            void OnPointerButton(uint32_t serial, uint32_t button, uint32_t state);
            void OnPointerFrame();
            void OnRelativeMotion(wl_fixed_t dx, wl_fixed_t dy);

            // Keyboard
            void OnKeymap(uint32_t format, int fd, uint32_t size);
            void OnKeyboardEnter(uint32_t serial, wl_surface* surface);
            void OnKeyboardLeave();
            void OnKey(uint32_t serial, uint32_t key, uint32_t state);
            void TickRepeat();

            // Data device
            void OnDataOffer(wl_data_offer* offer);
            void OnDndEnter(uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer);
            void OnDndMotion(wl_fixed_t x, wl_fixed_t y);
            void OnDndLeave();
            void OnDndDrop();
            void OnSelection(wl_data_offer* offer);
            void OnSourceSend(const char* mime, int fd);
            void OnSourceCancelled(wl_data_source* source);
            void DestroyOffer(Offer* offer);
            std::string Receive(wl_data_offer* offer, const char* mime);

            // Window support
            bool LockPointer(WaylandWindow* window, zwp_locked_pointer_v1*& out);
            void ApplyCursor();
            void ForgetWindow(const WaylandWindow* window);
            [[nodiscard]] WaylandWindow* Find(wl_surface* surface) const;
            [[nodiscard]] int32_t MaxOutputScale() const;
            [[nodiscard]] Output* FindOutput(wl_output* output) const;
            void FlushDisplay();
            void CheckError();

            wl_display*                     _display    = nullptr;
            wl_registry*                    _registry   = nullptr;
            wl_compositor*                  _compositor = nullptr;
            wl_shm*                         _shm        = nullptr;
            xdg_wm_base*                    _wmBase     = nullptr;
            wl_seat*                        _seat       = nullptr;
            wl_data_device_manager*         _dataMgr    = nullptr;
            wl_data_device*                 _dataDevice = nullptr;
            zxdg_decoration_manager_v1*     _decorMgr   = nullptr;
            zwp_pointer_constraints_v1*     _constraints = nullptr;
            zwp_relative_pointer_manager_v1* _relativeMgr = nullptr;
            wl_pointer*                     _pointer    = nullptr;
            wl_keyboard*                    _keyboardObj = nullptr;
            zwp_relative_pointer_v1*        _relativePointer = nullptr;
            std::vector<std::unique_ptr<Output>> _outputs;
#if defined(LUMORA_HAVE_LIBDECOR)
            libdecor* _decor = nullptr;
#endif

            Keyboard       _keyboard;
            WaylandWindow* _kbFocus      = nullptr;
            WaylandWindow* _pointerFocus = nullptr;
            uint32_t       _lastSerial   = 0;
            uint32_t       _pointerEnterSerial = 0;

            int32_t         _repeatRate  = 0;
            int32_t         _repeatDelay = 0;
            uint32_t        _repeatKey   = 0;
            Clock::time_point _repeatNext{};

            double  _axisValue[2]    = {0.0, 0.0};
            int32_t _axisDiscrete[2] = {0, 0};
            bool    _axisPending     = false;

            wl_cursor_theme* _cursorTheme   = nullptr;
            int32_t          _cursorScale   = 0;
            wl_surface*      _cursorSurface = nullptr;

            Offer*         _selectionOffer = nullptr;
            Offer*         _dragOffer      = nullptr;
            WaylandWindow* _dragWindow     = nullptr;
            InputVec2      _dragPos{};
            wl_data_source* _ownedSource   = nullptr;
            std::string     _ownedText;

            bool _failed          = false;
            bool _warnedNoLock    = false;
        };

        // ---- listeners ----

        // Listener tables list only the events used; the trailing members stay null, which libwayland ignores.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

        WaylandBackend* Be(void* data) { return static_cast<WaylandBackend*>(data); }
        WaylandWindow*  Win(void* data) { return static_cast<WaylandWindow*>(data); }

        const wl_registry_listener kRegistryListener = {
            .global        = [](void* d, wl_registry* r, uint32_t name, const char* iface, uint32_t v) { Be(d)->OnGlobal(r, name, iface, v); },
            .global_remove = [](void* d, wl_registry*, uint32_t name) { Be(d)->OnGlobalRemove(name); },
        };

        const xdg_wm_base_listener kWmBaseListener = {
            .ping = [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
        };

        const wl_seat_listener kSeatListener = {
            .capabilities = [](void* d, wl_seat*, uint32_t caps) { Be(d)->OnSeatCapabilities(caps); },
            .name         = [](void*, wl_seat*, const char*) {},
        };

        const wl_output_listener kOutputListener = {
            .geometry = [](void* d, wl_output*, int32_t x, int32_t y, int32_t, int32_t, int32_t, const char*, const char*, int32_t transform)
            {
                auto* o = static_cast<Output*>(d);
                o->x = x;
                o->y = y;
                o->transform = transform;
            },
            .mode = [](void* d, wl_output*, uint32_t flags, int32_t w, int32_t h, int32_t)
            {
                auto* o = static_cast<Output*>(d);
                if (flags & WL_OUTPUT_MODE_CURRENT)
                {
                    o->width  = w;
                    o->height = h;
                }
            },
            .done  = [](void*, wl_output*) {},
            .scale = [](void* d, wl_output*, int32_t factor) { static_cast<Output*>(d)->scale = std::max(1, factor); },
        };

        const wl_pointer_listener kPointerListener = {
            .enter  = [](void* d, wl_pointer*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y) { Be(d)->OnPointerEnter(serial, s, x, y); },
            .leave  = [](void* d, wl_pointer*, uint32_t, wl_surface* s) { Be(d)->OnPointerLeave(s); },
            .motion = [](void* d, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) { Be(d)->OnPointerMotion(x, y); },
            .button = [](void* d, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) { Be(d)->OnPointerButton(serial, button, state); },
            .axis   = [](void* d, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
            {
                auto* be = Be(d);
                if (axis < 2) be->_axisValue[axis] += wl_fixed_to_double(value);
                be->_axisPending = true;
            },
            .frame       = [](void* d, wl_pointer*) { Be(d)->OnPointerFrame(); },
            .axis_source = [](void*, wl_pointer*, uint32_t) {},
            .axis_stop   = [](void*, wl_pointer*, uint32_t, uint32_t) {},
            .axis_discrete = [](void* d, wl_pointer*, uint32_t axis, int32_t discrete)
            {
                auto* be = Be(d);
                if (axis < 2) be->_axisDiscrete[axis] += discrete;
                be->_axisPending = true;
            },
        };

        const zwp_relative_pointer_v1_listener kRelativeListener = {
            .relative_motion = [](void* d, zwp_relative_pointer_v1*, uint32_t, uint32_t, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t, wl_fixed_t)
            {
                Be(d)->OnRelativeMotion(dx, dy);
            },
        };

        const wl_keyboard_listener kKeyboardListener = {
            .keymap    = [](void* d, wl_keyboard*, uint32_t format, int fd, uint32_t size) { Be(d)->OnKeymap(format, fd, size); },
            .enter     = [](void* d, wl_keyboard*, uint32_t serial, wl_surface* s, wl_array*) { Be(d)->OnKeyboardEnter(serial, s); },
            .leave     = [](void* d, wl_keyboard*, uint32_t, wl_surface*) { Be(d)->OnKeyboardLeave(); },
            .key       = [](void* d, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) { Be(d)->OnKey(serial, key, state); },
            .modifiers = [](void* d, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
            {
                Be(d)->_keyboard.UpdateMask(depressed, latched, locked, 0, 0, group);
            },
            .repeat_info = [](void* d, wl_keyboard*, int32_t rate, int32_t delay)
            {
                Be(d)->_repeatRate  = rate;
                Be(d)->_repeatDelay = delay;
            },
        };

        const wl_surface_listener kSurfaceListener = {
            .enter  = [](void* d, wl_surface*, wl_output* o) { Win(d)->OnSurfaceEnter(o); },
            .leave  = [](void* d, wl_surface*, wl_output* o) { Win(d)->OnSurfaceLeave(o); },
            .preferred_buffer_scale     = [](void* d, wl_surface*, int32_t scale) { Win(d)->OnPreferredScale(scale); },
            .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
        };

        const xdg_surface_listener kXdgSurfaceListener = {
            .configure = [](void* d, xdg_surface*, uint32_t serial) { Win(d)->OnSurfaceConfigure(serial); },
        };

        const xdg_toplevel_listener kToplevelListener = {
            .configure = [](void* d, xdg_toplevel*, int32_t w, int32_t h, wl_array* states) { Win(d)->OnToplevelConfigure(w, h, states); },
            .close     = [](void* d, xdg_toplevel*) { Win(d)->ForceClose(); },
        };

        const zxdg_toplevel_decoration_v1_listener kDecorationListener = {
            .configure = [](void*, zxdg_toplevel_decoration_v1*, uint32_t mode)
            {
                if (mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
                    LOG_WARN("compositor chose client-side decorations; the window will have no title bar");
            },
        };

        const wl_data_offer_listener kOfferListener = {
            .offer = [](void* d, wl_data_offer*, const char* mime) { static_cast<Offer*>(d)->mimes.emplace_back(mime); },
            .source_actions = [](void*, wl_data_offer*, uint32_t) {},
            .action         = [](void*, wl_data_offer*, uint32_t) {},
        };

        const wl_data_device_listener kDataDeviceListener = {
            .data_offer = [](void* d, wl_data_device*, wl_data_offer* o) { Be(d)->OnDataOffer(o); },
            .enter      = [](void* d, wl_data_device*, uint32_t serial, wl_surface* s, wl_fixed_t x, wl_fixed_t y, wl_data_offer* o) { Be(d)->OnDndEnter(serial, s, x, y, o); },
            .leave      = [](void* d, wl_data_device*) { Be(d)->OnDndLeave(); },
            .motion     = [](void* d, wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y) { Be(d)->OnDndMotion(x, y); },
            .drop       = [](void* d, wl_data_device*) { Be(d)->OnDndDrop(); },
            .selection  = [](void* d, wl_data_device*, wl_data_offer* o) { Be(d)->OnSelection(o); },
        };

        const wl_data_source_listener kSourceListener = {
            .target = [](void*, wl_data_source*, const char*) {},
            .send   = [](void* d, wl_data_source*, const char* mime, int fd) { Be(d)->OnSourceSend(mime, fd); },
            .cancelled = [](void* d, wl_data_source* s) { Be(d)->OnSourceCancelled(s); },
            .dnd_drop_performed = [](void*, wl_data_source*) {},
            .dnd_finished       = [](void*, wl_data_source*) {},
            .action             = [](void*, wl_data_source*, uint32_t) {},
        };

#if defined(LUMORA_HAVE_LIBDECOR)
        libdecor_interface kDecorInterface = {
            .error = [](libdecor*, libdecor_error, const char* message) { LOG_ERROR("libdecor: {}", message); },
        };

        libdecor_frame_interface kFrameInterface = {
            .configure = [](libdecor_frame*, libdecor_configuration* c, void* d) { Win(d)->OnFrameConfigure(c); },
            .close     = [](libdecor_frame*, void* d) { Win(d)->ForceClose(); },
            .commit    = [](libdecor_frame*, void* d) { Win(d)->OnFrameCommit(); },
        };
#endif

#pragma GCC diagnostic pop

        // ---- WaylandWindow ----

        WaylandWindow::WaylandWindow(WaylandBackend& backend, const WindowDesc& desc)
            : _be(backend)
        {
            _logicalW = static_cast<int32_t>(std::max(1u, desc.width));
            _logicalH = static_cast<int32_t>(std::max(1u, desc.height));
            _scale    = _be.MaxOutputScale();
            _dpiScale = static_cast<float>(_scale);
            RecomputePhysical();

            _surface = wl_compositor_create_surface(_be._compositor);
            if (!_surface)
            {
                LOG_FATAL("failed to create wl_surface");
                return;
            }
            wl_surface_add_listener(_surface, &kSurfaceListener, this);
            _scaleFromOutputs = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(_surface)) < 6;
            wl_surface_set_buffer_scale(_surface, _scale);

            const std::string appId = os::ExecutablePath().filename().string();
            const auto* parent = desc.owner != nullptr ? static_cast<const WaylandWindow*>(desc.owner) : nullptr;

#if defined(LUMORA_HAVE_LIBDECOR)
            if (_be._decor)
            {
                _frame = libdecor_decorate(_be._decor, _surface, &kFrameInterface, this);
                if (!_frame)
                {
                    LOG_FATAL("libdecor_decorate failed");
                    return;
                }
                libdecor_frame_set_app_id(_frame, appId.c_str());
                libdecor_frame_set_title(_frame, desc.title.c_str());
                if (!desc.resizable)
                {
                    libdecor_frame_set_min_content_size(_frame, _logicalW, _logicalH);
                    libdecor_frame_set_max_content_size(_frame, _logicalW, _logicalH);
                    libdecor_frame_unset_capabilities(_frame, LIBDECOR_ACTION_RESIZE);
                }
                if (parent && parent->_frame) libdecor_frame_set_parent(_frame, parent->_frame);
                if (desc.fullscreenMode != FullscreenMode::Windowed)
                {
                    _fsRequested    = true;
                    _fullscreenMode = FullscreenMode::Borderless;
                    libdecor_frame_set_fullscreen(_frame, nullptr);
                }
                libdecor_frame_map(_frame);

                for (int i = 0; i < 40 && !_configured; ++i)
                    libdecor_dispatch(_be._decor, 50);
            }
            else
#endif
            {
                _xdgSurface = xdg_wm_base_get_xdg_surface(_be._wmBase, _surface);
                xdg_surface_add_listener(_xdgSurface, &kXdgSurfaceListener, this);
                _toplevel = xdg_surface_get_toplevel(_xdgSurface);
                xdg_toplevel_add_listener(_toplevel, &kToplevelListener, this);
                xdg_toplevel_set_title(_toplevel, desc.title.c_str());
                xdg_toplevel_set_app_id(_toplevel, appId.c_str());
                if (!desc.resizable)
                {
                    xdg_toplevel_set_min_size(_toplevel, _logicalW, _logicalH);
                    xdg_toplevel_set_max_size(_toplevel, _logicalW, _logicalH);
                }
                if (parent && parent->_toplevel) xdg_toplevel_set_parent(_toplevel, parent->_toplevel);
                if (_be._decorMgr)
                {
                    _decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(_be._decorMgr, _toplevel);
                    zxdg_toplevel_decoration_v1_add_listener(_decoration, &kDecorationListener, this);
                    zxdg_toplevel_decoration_v1_set_mode(_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
                }
                if (desc.fullscreenMode != FullscreenMode::Windowed)
                {
                    _fsRequested    = true;
                    _fullscreenMode = FullscreenMode::Borderless;
                    xdg_toplevel_set_fullscreen(_toplevel, nullptr);
                }
                wl_surface_commit(_surface);

                for (int i = 0; i < 40 && !_configured; ++i)
                    if (wl_display_roundtrip(_be._display) < 0) break;
            }

            if (!_configured)
                LOG_WARN("no initial configure from the compositor; using the requested size");

            // Wayland cannot place a window; desc.position is ignored.
            _pending.clear();
        }

        WaylandWindow::~WaylandWindow()
        {
            _be.ForgetWindow(this);
            if (_locked) zwp_locked_pointer_v1_destroy(_locked);
            if (_decoration) zxdg_toplevel_decoration_v1_destroy(_decoration);
#if defined(LUMORA_HAVE_LIBDECOR)
            if (_frame) libdecor_frame_unref(_frame);
#endif
            if (_toplevel) xdg_toplevel_destroy(_toplevel);
            if (_xdgSurface) xdg_surface_destroy(_xdgSurface);
            if (_surface) wl_surface_destroy(_surface);
            _be.FlushDisplay();
        }

        NativeWindowHandle WaylandWindow::GetNativeHandle() const
        {
            return NativeWindowHandle{
                .window   = _surface,
                .instance = _be._display,
                .backend  = WindowBackend::Wayland,
            };
        }

        void WaylandWindow::SetTitle(const std::string_view title)
        {
            const std::string text(title);
#if defined(LUMORA_HAVE_LIBDECOR)
            if (_frame) libdecor_frame_set_title(_frame, text.c_str());
#endif
            if (_toplevel) xdg_toplevel_set_title(_toplevel, text.c_str());
        }

        void WaylandWindow::SetFullscreenMode(const FullscreenMode mode)
        {
            if (_fullscreenMode == mode) return;

            // Wayland has no display mode switching, so Exclusive is Borderless.
            const bool fullscreen = mode != FullscreenMode::Windowed;
            if (fullscreen && _fullscreenMode == FullscreenMode::Windowed)
            {
                _windowedW = _logicalW;
                _windowedH = _logicalH;
            }
            _fsRequested = fullscreen;
#if defined(LUMORA_HAVE_LIBDECOR)
            if (_frame)
            {
                if (fullscreen) libdecor_frame_set_fullscreen(_frame, nullptr);
                else libdecor_frame_unset_fullscreen(_frame);
            }
#endif
            if (_toplevel)
            {
                if (fullscreen) xdg_toplevel_set_fullscreen(_toplevel, nullptr);
                else xdg_toplevel_unset_fullscreen(_toplevel);
            }
            _fullscreenMode = mode;
            _be.FlushDisplay();
        }

        void WaylandWindow::SetCursorMode(const CursorMode mode, const CursorClip& clip)
        {
            if (!_surface) return;
            if (_cursorMode == mode && (mode == CursorMode::Normal || _cursorClip == clip)) return;

            // A locked pointer cannot also be confined to a sub-rect, so the clip is recorded but not honoured.
            _cursorClip = clip;

            if (mode == CursorMode::Captured)
            {
                if (_cursorMode != CursorMode::Captured && !_be.LockPointer(this, _locked)) return;
            }
            else if (_locked)
            {
                zwp_locked_pointer_v1_destroy(_locked);
                _locked = nullptr;
            }

            _cursorMode = mode;
            _be.ApplyCursor();
            _be.FlushDisplay();
        }

        // Wayland exposes no screen coordinates and a client cannot demand a size: this only records the size the caller
        // wants to render at and reports it, the compositor stays the authority on the next configure.
        void WaylandWindow::SetClientRect(const ScreenRect& rect)
        {
            if (rect.width <= 0 || rect.height <= 0) return;
            const int32_t w = std::max(1, rect.width / _scale);
            const int32_t h = std::max(1, rect.height / _scale);
            if (w == _logicalW && h == _logicalH) return;
            _logicalW = w;
            _logicalH = h;
            RecomputePhysical();
            EmitWindow(WindowEventType::Resize);
        }

        void WaylandWindow::EmitKey(const bool pressed, const KeyTranslation& t)
        {
            InputEvent e{};
            e.type = pressed ? InputEventType::KeyDown : InputEventType::KeyUp;
            e.key  = t.key;
            e.mods = t.mods;
            Emit(e);

            if (pressed && t.text[0] != '\0')
            {
                InputEvent text{};
                text.type = InputEventType::Text;
                std::memcpy(text.text, t.text, sizeof(text.text) - 1);
                text.mods = t.mods;
                Emit(text);
            }
        }

        void WaylandWindow::ForceClose()
        {
            if (_shouldClose) return;
            _shouldClose = true;
            EmitWindow(WindowEventType::Close);
        }

        void WaylandWindow::RecomputePhysical()
        {
            _width  = static_cast<uint32_t>(_logicalW * _scale);
            _height = static_cast<uint32_t>(_logicalH * _scale);
        }

        void WaylandWindow::OnToplevelConfigure(const int32_t w, const int32_t h, const wl_array* states)
        {
            _cfgW = w;
            _cfgH = h;
            _cfgFullscreen = false;
            _cfgSuspended  = false;
            const auto* begin = static_cast<const uint32_t*>(states->data);
            for (const uint32_t* s = begin; s < begin + states->size / sizeof(uint32_t); ++s)
            {
                if (*s == XDG_TOPLEVEL_STATE_FULLSCREEN) _cfgFullscreen = true;
#if defined(XDG_TOPLEVEL_STATE_SUSPENDED_SINCE_VERSION)
                if (*s == XDG_TOPLEVEL_STATE_SUSPENDED) _cfgSuspended = true;
#endif
            }
        }

        void WaylandWindow::OnSurfaceConfigure(const uint32_t serial)
        {
            xdg_surface_ack_configure(_xdgSurface, serial);
            ApplyConfigure(_cfgW, _cfgH, _cfgFullscreen, _cfgSuspended);
            _configured = true;
        }

        void WaylandWindow::ApplyConfigure(const int32_t w, const int32_t h, const bool fullscreen, const bool suspended)
        {
            // 0x0 means the client picks: leaving fullscreen then restores the size it had before.
            int32_t newW = w, newH = h;
            const bool windowed = !fullscreen && !(_fsRequested && *_fsRequested);
            if (windowed && (w <= 0 || h <= 0) && _windowedW > 0)
            {
                newW = _windowedW;
                newH = _windowedH;
            }
            if (windowed) _windowedW = _windowedH = 0;

            if (newW > 0 && newH > 0 && (newW != _logicalW || newH != _logicalH))
            {
                _logicalW = newW;
                _logicalH = newH;
                RecomputePhysical();
                if (_configured) EmitWindow(WindowEventType::Resize);
            }

            // A configure that predates our own fullscreen request must not flip the mode back.
            if (_fsRequested)
            {
                if (fullscreen == *_fsRequested) _fsRequested.reset();
            }
            else if (fullscreen && _fullscreenMode == FullscreenMode::Windowed)
            {
                _fullscreenMode = FullscreenMode::Borderless;
            }
            else if (!fullscreen && _fullscreenMode != FullscreenMode::Windowed)
            {
                _fullscreenMode = FullscreenMode::Windowed;
            }

            _hidden = suspended;
        }

        void WaylandWindow::OnSurfaceEnter(wl_output* output)
        {
            _entered.push_back(output);
            if (!_scaleFromOutputs) return;
            int32_t scale = 1;
            for (wl_output* o : _entered)
                if (const Output* info = _be.FindOutput(o)) scale = std::max(scale, info->scale);
            SetScale(scale);
        }

        void WaylandWindow::OnSurfaceLeave(wl_output* output)
        {
            std::erase(_entered, output);
        }

        void WaylandWindow::OnPreferredScale(const int32_t scale)
        {
            SetScale(std::max(1, scale));
        }

        void WaylandWindow::SetScale(const int32_t scale)
        {
            if (scale == _scale) return;
            _scale    = scale;
            _dpiScale = static_cast<float>(scale);
            wl_surface_set_buffer_scale(_surface, scale);
            RecomputePhysical();
            if (_configured)
            {
                EmitWindow(WindowEventType::DpiChanged);
                EmitWindow(WindowEventType::Resize);
            }
            _be.ApplyCursor();
        }

#if defined(LUMORA_HAVE_LIBDECOR)
        void WaylandWindow::OnFrameConfigure(libdecor_configuration* configuration)
        {
            int w = 0, h = 0;
            if (!libdecor_configuration_get_content_size(configuration, _frame, &w, &h))
                w = h = 0;

            libdecor_window_state state{};
            const bool fullscreen = libdecor_configuration_get_window_state(configuration, &state) &&
                                    (state & LIBDECOR_WINDOW_STATE_FULLSCREEN) != 0;
            ApplyConfigure(w, h, fullscreen, false);

            libdecor_state* next = libdecor_state_new(_logicalW, _logicalH);
            libdecor_frame_commit(_frame, next, configuration);
            libdecor_state_free(next);
            _configured = true;
        }

        void WaylandWindow::OnFrameCommit()
        {
            wl_surface_commit(_surface);
        }
#endif

        // ---- WaylandBackend ----

        WaylandBackend::~WaylandBackend()
        {
            if (g_backend == this) g_backend = nullptr;
            if (_ownedSource) wl_data_source_destroy(_ownedSource);
            if (_cursorTheme) wl_cursor_theme_destroy(_cursorTheme);
#if defined(LUMORA_HAVE_LIBDECOR)
            if (_decor) libdecor_unref(_decor);
#endif
            if (_display) wl_display_disconnect(_display);
        }

        bool WaylandBackend::Init()
        {
            _display = wl_display_connect(nullptr);
            if (!_display) return false;

            _registry = wl_display_get_registry(_display);
            wl_registry_add_listener(_registry, &kRegistryListener, this);
            wl_display_roundtrip(_display);

            if (!_compositor || !_wmBase)
            {
                LOG_WARN("compositor lacks wl_compositor or xdg_wm_base");
                return false;
            }
            xdg_wm_base_add_listener(_wmBase, &kWmBaseListener, this);

            // Outputs, seat capabilities, then the keymap and selection those capabilities trigger.
            wl_display_roundtrip(_display);
            MaybeCreateDataDevice();
            wl_display_roundtrip(_display);

#if defined(LUMORA_HAVE_LIBDECOR)
            if (!_decorMgr)
            {
                _decor = libdecor_new(_display, &kDecorInterface);
                if (_decor) LOG_INFO("no xdg-decoration, using libdecor for client-side frames");
            }
#endif
            if (!_decorMgr
#if defined(LUMORA_HAVE_LIBDECOR)
                && !_decor
#endif
            )
                LOG_WARN("no server-side decorations and no libdecor: windows will have no title bar");

            g_backend = this;
            return true;
        }

        void WaylandBackend::OnGlobal(wl_registry* registry, const uint32_t name, const char* interface, const uint32_t version)
        {
            const auto bind = [&](const wl_interface& iface, const uint32_t cap)
            {
                return wl_registry_bind(registry, name, &iface, std::min(version, cap));
            };
            const std::string_view id = interface;

            if (id == wl_compositor_interface.name) _compositor = static_cast<wl_compositor*>(bind(wl_compositor_interface, 6));
            else if (id == wl_shm_interface.name) _shm = static_cast<wl_shm*>(bind(wl_shm_interface, 1));
            else if (id == xdg_wm_base_interface.name) _wmBase = static_cast<xdg_wm_base*>(bind(xdg_wm_base_interface, 6));
            else if (id == wl_data_device_manager_interface.name) _dataMgr = static_cast<wl_data_device_manager*>(bind(wl_data_device_manager_interface, 3));
            else if (id == zxdg_decoration_manager_v1_interface.name) _decorMgr = static_cast<zxdg_decoration_manager_v1*>(bind(zxdg_decoration_manager_v1_interface, 1));
            else if (id == zwp_pointer_constraints_v1_interface.name) _constraints = static_cast<zwp_pointer_constraints_v1*>(bind(zwp_pointer_constraints_v1_interface, 1));
            else if (id == zwp_relative_pointer_manager_v1_interface.name) _relativeMgr = static_cast<zwp_relative_pointer_manager_v1*>(bind(zwp_relative_pointer_manager_v1_interface, 1));
            else if (id == wl_seat_interface.name && !_seat)
            {
                _seat = static_cast<wl_seat*>(bind(wl_seat_interface, 5));
                wl_seat_add_listener(_seat, &kSeatListener, this);
            }
            else if (id == wl_output_interface.name) AddOutput(registry, name, version);
        }

        void WaylandBackend::AddOutput(wl_registry* registry, const uint32_t name, const uint32_t version)
        {
            auto info    = std::make_unique<Output>();
            info->name   = name;
            info->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 3u)));
            wl_output_add_listener(info->output, &kOutputListener, info.get());
            _outputs.push_back(std::move(info));
        }

        void WaylandBackend::OnGlobalRemove(const uint32_t name)
        {
            std::erase_if(_outputs, [&](const std::unique_ptr<Output>& o)
            {
                if (o->name != name) return false;
                wl_output_release(o->output);
                return true;
            });
        }

        void WaylandBackend::OnSeatCapabilities(const uint32_t caps)
        {
            if ((caps & WL_SEAT_CAPABILITY_POINTER) && !_pointer)
            {
                _pointer = wl_seat_get_pointer(_seat);
                wl_pointer_add_listener(_pointer, &kPointerListener, this);
                if (_relativeMgr)
                {
                    _relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(_relativeMgr, _pointer);
                    zwp_relative_pointer_v1_add_listener(_relativePointer, &kRelativeListener, this);
                }
            }
            else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && _pointer)
            {
                if (_relativePointer) zwp_relative_pointer_v1_destroy(_relativePointer);
                _relativePointer = nullptr;
                wl_pointer_release(_pointer);
                _pointer      = nullptr;
                _pointerFocus = nullptr;
            }

            if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !_keyboardObj)
            {
                _keyboardObj = wl_seat_get_keyboard(_seat);
                wl_keyboard_add_listener(_keyboardObj, &kKeyboardListener, this);
            }
            else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && _keyboardObj)
            {
                wl_keyboard_release(_keyboardObj);
                _keyboardObj = nullptr;
                _kbFocus     = nullptr;
                _repeatKey   = 0;
            }
        }

        void WaylandBackend::MaybeCreateDataDevice()
        {
            if (_dataDevice || !_dataMgr || !_seat) return;
            _dataDevice = wl_data_device_manager_get_data_device(_dataMgr, _seat);
            wl_data_device_add_listener(_dataDevice, &kDataDeviceListener, this);
        }

        // ---- pointer ----

        void WaylandBackend::OnPointerEnter(const uint32_t serial, wl_surface* surface, const wl_fixed_t x, const wl_fixed_t y)
        {
            _lastSerial         = serial;
            _pointerEnterSerial = serial;
            _pointerFocus       = Find(surface);
            if (!_pointerFocus) return;

            const double scale = _pointerFocus->Scale();
            const InputVec2 pos{static_cast<float>(wl_fixed_to_double(x) * scale), static_cast<float>(wl_fixed_to_double(y) * scale)};
            if (_pointerFocus->GetCursorMode() != CursorMode::Captured)
            {
                _pointerFocus->SetLastPointer(pos);
                InputEvent e{};
                e.type     = InputEventType::PointerMove;
                e.position = pos;
                e.mods     = _keyboard.Mods();
                _pointerFocus->Emit(e);
            }
            ApplyCursor();
        }

        void WaylandBackend::OnPointerLeave(wl_surface* surface)
        {
            if (_pointerFocus && _pointerFocus->Surface() == surface) _pointerFocus = nullptr;
        }

        void WaylandBackend::OnPointerMotion(const wl_fixed_t x, const wl_fixed_t y)
        {
            if (!_pointerFocus || _pointerFocus->GetCursorMode() == CursorMode::Captured) return;

            const double scale = _pointerFocus->Scale();
            const InputVec2 pos{static_cast<float>(wl_fixed_to_double(x) * scale), static_cast<float>(wl_fixed_to_double(y) * scale)};
            const InputVec2 last = _pointerFocus->LastPointer();

            InputEvent e{};
            e.type     = InputEventType::PointerMove;
            e.position = pos;
            e.delta    = {pos.x - last.x, pos.y - last.y};
            e.mods     = _keyboard.Mods();
            _pointerFocus->Emit(e);
            _pointerFocus->SetLastPointer(pos);
        }

        void WaylandBackend::OnPointerButton(const uint32_t serial, const uint32_t button, const uint32_t state)
        {
            _lastSerial = serial;
            if (!_pointerFocus) return;

            InputEvent e{};
            switch (button)
            {
            case BTN_LEFT:   e.button = PointerButton::Left;   break;
            case BTN_RIGHT:  e.button = PointerButton::Right;  break;
            case BTN_MIDDLE: e.button = PointerButton::Middle; break;
            case BTN_SIDE:   e.button = PointerButton::X1;     break;
            case BTN_EXTRA:  e.button = PointerButton::X2;     break;
            default: return;
            }
            e.type     = state == WL_POINTER_BUTTON_STATE_PRESSED ? InputEventType::PointerDown : InputEventType::PointerUp;
            e.position = _pointerFocus->LastPointer();
            e.mods     = _keyboard.Mods();
            _pointerFocus->Emit(e);
        }

        void WaylandBackend::OnPointerFrame()
        {
            if (!_axisPending) return;
            const double h = _axisDiscrete[1] != 0 ? _axisDiscrete[1] : _axisValue[1] / 10.0;
            const double v = _axisDiscrete[0] != 0 ? _axisDiscrete[0] : _axisValue[0] / 10.0;
            _axisValue[0] = _axisValue[1] = 0.0;
            _axisDiscrete[0] = _axisDiscrete[1] = 0;
            _axisPending = false;

            if (!_pointerFocus || (h == 0.0 && v == 0.0)) return;

            InputEvent e{};
            e.type        = InputEventType::Scroll;
            e.position    = _pointerFocus->LastPointer();
            e.scrollDelta = {static_cast<float>(h), static_cast<float>(-v)};   // Wayland's +y is down, ours is up
            e.mods        = _keyboard.Mods();
            _pointerFocus->Emit(e);
        }

        void WaylandBackend::OnRelativeMotion(const wl_fixed_t dx, const wl_fixed_t dy)
        {
            if (!_pointerFocus || _pointerFocus->GetCursorMode() != CursorMode::Captured) return;

            const double scale = _pointerFocus->Scale();
            InputEvent e{};
            e.type     = InputEventType::PointerMove;
            e.position = _pointerFocus->LastPointer();
            e.delta    = {static_cast<float>(wl_fixed_to_double(dx) * scale), static_cast<float>(wl_fixed_to_double(dy) * scale)};
            e.mods     = _keyboard.Mods();
            _pointerFocus->Emit(e);
        }

        // ---- keyboard ----

        void WaylandBackend::OnKeymap(const uint32_t format, const int fd, const uint32_t size)
        {
            if (format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
            {
                void* map = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (map != MAP_FAILED)
                {
                    xkb_keymap* keymap = xkb_keymap_new_from_string(_keyboard.Context(), static_cast<const char*>(map),
                                                                    XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
                    ::munmap(map, size);
                    if (keymap) _keyboard.SetKeymap(keymap);
                    else LOG_WARN("failed to compile the compositor keymap");
                }
            }
            ::close(fd);
        }

        void WaylandBackend::OnKeyboardEnter(const uint32_t serial, wl_surface* surface)
        {
            _lastSerial = serial;
            _kbFocus    = Find(surface);
            if (_kbFocus) _kbFocus->EmitWindow(WindowEventType::FocusGained);
        }

        void WaylandBackend::OnKeyboardLeave()
        {
            _repeatKey = 0;
            _keyboard.ResetCompose();
            if (!_kbFocus) return;
            WaylandWindow* window = _kbFocus;
            _kbFocus = nullptr;
            window->SetCursorMode(CursorMode::Normal);
            window->EmitWindow(WindowEventType::FocusLost);
        }

        void WaylandBackend::OnKey(const uint32_t serial, const uint32_t key, const uint32_t state)
        {
            _lastSerial = serial;
            if (!_kbFocus) return;

            const bool pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
            const KeyTranslation t = pressed ? _keyboard.Press(key) : _keyboard.Release(key);
            _kbFocus->EmitKey(pressed, t);

            if (pressed && _repeatRate > 0 && _keyboard.Repeats(key))
            {
                _repeatKey  = key;
                _repeatNext = Clock::now() + std::chrono::milliseconds(_repeatDelay);
            }
            else if (!pressed && key == _repeatKey)
            {
                _repeatKey = 0;
            }
        }

        void WaylandBackend::TickRepeat()
        {
            if (_repeatKey == 0 || !_kbFocus || _repeatRate <= 0) return;
            const auto now = Clock::now();
            if (now < _repeatNext) return;

            // One repeat per pump: a stalled frame drops repeats instead of flooding the queue afterwards.
            _kbFocus->EmitKey(true, _keyboard.Press(_repeatKey));
            _repeatNext = now + std::chrono::microseconds(1'000'000 / _repeatRate);
        }

        // ---- data device ----

        void WaylandBackend::OnDataOffer(wl_data_offer* offer)
        {
            auto* info  = new Offer{offer, {}};
            wl_data_offer_add_listener(offer, &kOfferListener, info);
        }

        void WaylandBackend::DestroyOffer(Offer* offer)
        {
            if (!offer) return;
            wl_data_offer_destroy(offer->offer);
            delete offer;
        }

        void WaylandBackend::OnSelection(wl_data_offer* offer)
        {
            DestroyOffer(_selectionOffer);
            _selectionOffer = offer ? static_cast<Offer*>(wl_data_offer_get_user_data(offer)) : nullptr;
        }

        void WaylandBackend::OnDndEnter(const uint32_t serial, wl_surface* surface, const wl_fixed_t x, const wl_fixed_t y, wl_data_offer* offer)
        {
            DestroyOffer(_dragOffer);
            _dragOffer  = offer ? static_cast<Offer*>(wl_data_offer_get_user_data(offer)) : nullptr;
            _dragWindow = Find(surface);
            if (!_dragOffer || !_dragWindow) return;

            const double scale = _dragWindow->Scale();
            _dragPos = {static_cast<float>(wl_fixed_to_double(x) * scale), static_cast<float>(wl_fixed_to_double(y) * scale)};

            if (_dragOffer->Has(kUriListMime))
            {
                wl_data_offer_accept(offer, serial, kUriListMime);
                if (wl_data_offer_get_version(offer) >= 3)
                    wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
            }
            else
            {
                wl_data_offer_accept(offer, serial, nullptr);
            }
        }

        void WaylandBackend::OnDndMotion(const wl_fixed_t x, const wl_fixed_t y)
        {
            if (!_dragWindow) return;
            const double scale = _dragWindow->Scale();
            _dragPos = {static_cast<float>(wl_fixed_to_double(x) * scale), static_cast<float>(wl_fixed_to_double(y) * scale)};
        }

        void WaylandBackend::OnDndLeave()
        {
            DestroyOffer(_dragOffer);
            _dragOffer  = nullptr;
            _dragWindow = nullptr;
        }

        void WaylandBackend::OnDndDrop()
        {
            Offer*         offer  = _dragOffer;
            WaylandWindow* window = _dragWindow;
            _dragOffer  = nullptr;
            _dragWindow = nullptr;
            if (!offer) return;

            if (window && offer->Has(kUriListMime))
            {
                FileDropEvent e{};
                e.paths    = ParseUriList(Receive(offer->offer, kUriListMime));
                e.position = _dragPos;
                if (wl_data_offer_get_version(offer->offer) >= 3) wl_data_offer_finish(offer->offer);
                if (!e.paths.empty()) window->EmitDrop(std::move(e));
            }
            DestroyOffer(offer);
        }

        std::string WaylandBackend::Receive(wl_data_offer* offer, const char* mime)
        {
            int fds[2];
            if (::pipe2(fds, O_CLOEXEC) != 0) return {};
            wl_data_offer_receive(offer, mime, fds[1]);
            ::close(fds[1]);
            FlushDisplay();

            // The sender is another client and answers on its own; nothing here needs dispatching meanwhile.
            std::string text = ReadAll(fds[0], kTransferTimeoutMs);
            ::close(fds[0]);
            return text;
        }

        void WaylandBackend::OnSourceSend(const char* mime, const int fd)
        {
            const bool known = std::ranges::any_of(kTextMimes, [&](const char* m) { return std::strcmp(m, mime) == 0; });
            if (!known)
            {
                ::close(fd);
                return;
            }
            WriteAllAndClose(fd, _ownedText);
        }

        void WaylandBackend::OnSourceCancelled(wl_data_source* source)
        {
            if (source == _ownedSource)
            {
                _ownedSource = nullptr;
                _ownedText.clear();
            }
            wl_data_source_destroy(source);
        }

        bool WaylandBackend::SetClipboard(const std::string_view text)
        {
            MaybeCreateDataDevice();
            if (!_dataMgr || !_dataDevice || _lastSerial == 0) return false;

            wl_data_source* source = wl_data_device_manager_create_data_source(_dataMgr);
            wl_data_source_add_listener(source, &kSourceListener, this);
            for (const char* mime : kTextMimes) wl_data_source_offer(source, mime);

            wl_data_device_set_selection(_dataDevice, source, _lastSerial);
            if (_ownedSource) wl_data_source_destroy(_ownedSource);
            _ownedSource = source;
            _ownedText.assign(text);
            FlushDisplay();
            return true;
        }

        std::string WaylandBackend::GetClipboard()
        {
            if (_ownedSource) return _ownedText;
            if (!_selectionOffer) return {};

            for (const char* mime : kTextMimes)
                if (_selectionOffer->Has(mime)) return Receive(_selectionOffer->offer, mime);
            return {};
        }

        // ---- window support ----

        bool WaylandBackend::LockPointer(WaylandWindow* window, zwp_locked_pointer_v1*& out)
        {
            if (!_constraints || !_pointer || !_relativeMgr)
            {
                if (!_warnedNoLock)
                    LOG_WARN("compositor lacks pointer-constraints or relative-pointer; cursor capture unavailable");
                _warnedNoLock = true;
                return false;
            }
            out = zwp_pointer_constraints_v1_lock_pointer(_constraints, window->Surface(), _pointer, nullptr,
                                                          ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
            return out != nullptr;
        }

        void WaylandBackend::ApplyCursor()
        {
            if (!_pointer || !_pointerFocus || !_shm || !_compositor) return;

            if (_pointerFocus->GetCursorMode() == CursorMode::Captured)
            {
                wl_pointer_set_cursor(_pointer, _pointerEnterSerial, nullptr, 0, 0);
                return;
            }

            const int32_t scale = _pointerFocus->Scale();
            if (!_cursorTheme || _cursorScale != scale)
            {
                if (_cursorTheme) wl_cursor_theme_destroy(_cursorTheme);
                const char* name = std::getenv("XCURSOR_THEME");
                const char* size = std::getenv("XCURSOR_SIZE");
                const int   base = size ? std::atoi(size) : 0;
                _cursorTheme = wl_cursor_theme_load(name && *name ? name : nullptr, (base > 0 ? base : 24) * scale, _shm);
                _cursorScale = scale;
            }
            if (!_cursorTheme) return;

            wl_cursor* cursor = wl_cursor_theme_get_cursor(_cursorTheme, "left_ptr");
            if (!cursor) cursor = wl_cursor_theme_get_cursor(_cursorTheme, "default");
            if (!cursor || cursor->image_count == 0) return;

            if (!_cursorSurface) _cursorSurface = wl_compositor_create_surface(_compositor);
            wl_cursor_image* image = cursor->images[0];
            wl_surface_set_buffer_scale(_cursorSurface, scale);
            wl_surface_attach(_cursorSurface, wl_cursor_image_get_buffer(image), 0, 0);
            wl_surface_damage_buffer(_cursorSurface, 0, 0, static_cast<int32_t>(image->width), static_cast<int32_t>(image->height));
            wl_surface_commit(_cursorSurface);
            wl_pointer_set_cursor(_pointer, _pointerEnterSerial, _cursorSurface,
                                  static_cast<int32_t>(image->hotspot_x) / scale, static_cast<int32_t>(image->hotspot_y) / scale);
        }

        void WaylandBackend::ForgetWindow(const WaylandWindow* window)
        {
            if (_kbFocus == window)
            {
                _kbFocus   = nullptr;
                _repeatKey = 0;
            }
            if (_pointerFocus == window) _pointerFocus = nullptr;
            if (_dragWindow == window) _dragWindow = nullptr;
        }

        WaylandWindow* WaylandBackend::Find(wl_surface* surface) const
        {
            if (!surface) return nullptr;
            for (LinuxWindow* w : LinuxWindow::All())
                if (auto* ww = static_cast<WaylandWindow*>(w); ww->Surface() == surface) return ww;
            return nullptr;
        }

        int32_t WaylandBackend::MaxOutputScale() const
        {
            int32_t scale = 1;
            for (const auto& o : _outputs) scale = std::max(scale, o->scale);
            return scale;
        }

        Output* WaylandBackend::FindOutput(wl_output* output) const
        {
            for (const auto& o : _outputs)
                if (o->output == output) return o.get();
            return nullptr;
        }

        void WaylandBackend::FlushDisplay()
        {
            if (_display) wl_display_flush(_display);
        }

        void WaylandBackend::CheckError()
        {
            if (_failed || wl_display_get_error(_display) == 0) return;
            _failed = true;
            LOG_ERROR("Wayland connection lost (errno {}); closing every window", wl_display_get_error(_display));
            for (LinuxWindow* w : LinuxWindow::All())
                static_cast<WaylandWindow*>(w)->ForceClose();
        }

        // ---- DisplayBackend ----

        Ref<Window> WaylandBackend::CreateWindow(const WindowDesc& desc)
        {
            auto window = MakeRef<WaylandWindow>(*this, desc);
            if (!window->IsValid()) return nullptr;
            CheckError();
            return window;
        }

        void WaylandBackend::Pump()
        {
            if (_failed) return;
            FlushDisplay();

#if defined(LUMORA_HAVE_LIBDECOR)
            if (_decor)
            {
                libdecor_dispatch(_decor, 0);
            }
            else
#endif
            {
                while (wl_display_prepare_read(_display) != 0)
                    wl_display_dispatch_pending(_display);
                wl_display_read_events(_display);   // the socket is non-blocking: returns at once when idle
                wl_display_dispatch_pending(_display);
            }

            TickRepeat();
            CheckError();
        }

        void WaylandBackend::Wait()
        {
            if (_failed) return;
            FlushDisplay();

#if defined(LUMORA_HAVE_LIBDECOR)
            const int displayFd = _decor ? libdecor_get_fd(_decor) : wl_display_get_fd(_display);
            if (!_decor)
#else
            const int displayFd = wl_display_get_fd(_display);
#endif
            {
                bool queued = false;
                while (wl_display_prepare_read(_display) != 0)
                {
                    if (wl_display_dispatch_pending(_display) > 0) queued = true;
                }
                if (queued)
                {
                    wl_display_cancel_read(_display);
                    CheckError();
                    return;
                }
            }

            int timeoutMs = -1;
            if (_repeatKey != 0 && _repeatRate > 0)
            {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(_repeatNext - Clock::now());
                timeoutMs = static_cast<int>(std::max<std::int64_t>(0, left.count() + 1));
            }

            pollfd fds[2] = {{displayFd, POLLIN, 0}, {WakeFd(), POLLIN, 0}};
            const int ready = ::poll(fds, 2, timeoutMs);
            const bool dataReady = ready > 0 && (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0;

#if defined(LUMORA_HAVE_LIBDECOR)
            if (_decor)
            {
                if (dataReady) libdecor_dispatch(_decor, 0);
            }
            else
#endif
            {
                if (dataReady) wl_display_read_events(_display);
                else wl_display_cancel_read(_display);
                wl_display_dispatch_pending(_display);
            }

            if (ready > 0 && (fds[1].revents & POLLIN)) DrainWake();
            TickRepeat();
            CheckError();
        }

        ScreenPoint WaylandBackend::CursorPosition()
        {
            // No global pointer position exists on Wayland: this is the position inside the surface under the pointer.
            if (!_pointerFocus) return {0, 0};
            const InputVec2 p = _pointerFocus->LastPointer();
            return {static_cast<int32_t>(p.x), static_cast<int32_t>(p.y)};
        }

        Window* WaylandBackend::WindowAt(const ScreenPoint point)
        {
            // Window rects are {0,0,w,h} (no global coordinates), so only the pointer-focus window can answer.
            if (_pointerFocus && _pointerFocus->GetClientRect().Contains(point)) return _pointerFocus;
            return nullptr;
        }

        bool WaylandBackend::IsOnAnyMonitor(const ScreenRect& rect)
        {
            if (_outputs.empty()) return true;
            return std::ranges::any_of(_outputs, [&](const std::unique_ptr<Output>& o)
            {
                const ScreenRect r = o->Rect();
                return rect.x < r.x + r.width && rect.x + rect.width > r.x && rect.y < r.y + r.height && rect.y + rect.height > r.y;
            });
        }

        ScreenRect WaylandBackend::PrimaryWorkArea()
        {
            // Wayland has no primary output or panel-aware work area; the first output's full mode stands in.
            if (_outputs.empty() || _outputs.front()->width <= 0) return {0, 0, 1920, 1080};
            return _outputs.front()->Rect();
        }
    }

    std::unique_ptr<DisplayBackend> MakeWaylandBackend()
    {
        auto backend = std::make_unique<WaylandBackend>();
        if (!backend->Init()) return nullptr;
        return backend;
    }
}
