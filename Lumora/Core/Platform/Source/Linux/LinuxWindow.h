#pragma once

#include "lumora/platform/Window.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::platform
{
    /// State and event plumbing shared by the X11 and Wayland windows. A backend subclass fills the pending buffers while
    /// it dispatches server events (inside DisplayBackend::Pump); FlushEvents publishes them.
    class LinuxWindow : public Window
    {
    public:
        LinuxWindow();
        ~LinuxWindow() override;

        /// Every live window, in creation order.
        [[nodiscard]] static const std::vector<LinuxWindow*>& All();

        void WaitEvents() override;
        void Wake() override;
        void PumpEvents() override;
        void PumpMessages() override;
        void FlushEvents() override;

        [[nodiscard]] bool ShouldClose() const override { return _shouldClose; }
        void RequestClose() override { _shouldClose = true; }
        void ClearCloseRequest() override { _shouldClose = false; }

        [[nodiscard]] uint32_t       GetWidth()          const override { return _width; }
        [[nodiscard]] uint32_t       GetHeight()         const override { return _height; }
        [[nodiscard]] float          GetDpiScale()       const override { return _dpiScale; }
        [[nodiscard]] FullscreenMode GetFullscreenMode() const override { return _fullscreenMode; }
        [[nodiscard]] CursorMode     GetCursorMode()     const override { return _cursorMode; }

    protected:
        uint32_t       _width          = 0;   ///< client area, physical pixels
        uint32_t       _height         = 0;
        float          _dpiScale       = 1.0f;
        bool           _shouldClose    = false;
        FullscreenMode _fullscreenMode = FullscreenMode::Windowed;
        CursorMode     _cursorMode     = CursorMode::Normal;
        CursorClip     _cursorClip{};
        InputVec2      _lastPointerPos{};

        std::vector<WindowEvent>   _pending;
        std::vector<InputEvent>    _pendingInput;
        std::vector<FileDropEvent> _pendingDrops;
    };

    /// One display server connection plus everything that is not tied to a single window. Implemented by X11Backend.cpp and
    /// WaylandBackend.cpp. All calls come from the thread that creates windows and pumps events, except Wake (see below).
    class DisplayBackend
    {
    public:
        virtual ~DisplayBackend() = default;

        [[nodiscard]] virtual WindowBackend Kind() const = 0;

        /// Null if the window could not be created; the reason is logged.
        [[nodiscard]] virtual Ref<Window> CreateWindow(const WindowDesc& desc) = 0;

        /// Reads and dispatches everything already available from the server into the windows' pending buffers. Never blocks.
        virtual void Pump() = 0;

        /// Blocks until the server has data, a timer the backend owns is due (key repeat), or WakeFd() is readable (then
        /// drains it). Returns immediately if events are already queued client-side.
        virtual void Wait() = 0;

        [[nodiscard]] virtual ScreenPoint CursorPosition()                         = 0;
        [[nodiscard]] virtual Window*     WindowAt(ScreenPoint point)              = 0;
        [[nodiscard]] virtual bool        IsOnAnyMonitor(const ScreenRect& rect)   = 0;
        [[nodiscard]] virtual ScreenRect  PrimaryWorkArea()                        = 0;

        virtual bool        SetClipboard(std::string_view text) = 0;
        virtual std::string GetClipboard()                      = 0;
    };

    /// Null if the server is not reachable.
    [[nodiscard]] std::unique_ptr<DisplayBackend> MakeX11Backend();
    [[nodiscard]] std::unique_ptr<DisplayBackend> MakeWaylandBackend();

    /// The backend in use: Wayland when a Wayland compositor answers, else X11 (LUMORA_WINDOW_BACKEND=x11|wayland forces
    /// one). Picked on first use and kept for the life of the process. Null when neither server is reachable.
    [[nodiscard]] DisplayBackend* GetBackend();

    /// A non-blocking eventfd, readable after Wake(). Backends poll it next to their connection fd and call DrainWake().
    [[nodiscard]] int  WakeFd();
    void               DrainWake();
}
