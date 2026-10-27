#include "LinuxWindow.h"

#include "lumora/os/Environment.h"
#include "lumora/log/Log.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

LUMORA_LOG_CATEGORY("Platform.Linux")

namespace lumora::platform
{
    namespace
    {
        std::vector<LinuxWindow*> g_windows;

        int g_wakeFd = -1;

        std::unique_ptr<DisplayBackend> PickBackend()
        {
            const auto forced = os::GetEnv("LUMORA_WINDOW_BACKEND");
            const bool wantX11     = forced && *forced == "x11";
            const bool wantWayland = forced && *forced == "wayland";

            if (!wantX11)
            {
                const auto waylandDisplay = os::GetEnv("WAYLAND_DISPLAY");
                if (wantWayland || (waylandDisplay && !waylandDisplay->empty()))
                {
                    if (auto backend = MakeWaylandBackend()) return backend;
                    LOG_WARN("Wayland compositor unreachable, trying X11");
                }
            }
            if (!wantWayland)
            {
                if (auto backend = MakeX11Backend()) return backend;
            }
            LOG_FATAL("no display server reachable (WAYLAND_DISPLAY / DISPLAY)");
            return nullptr;
        }
    }

    int WakeFd()
    {
        if (g_wakeFd < 0) g_wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        return g_wakeFd;
    }

    void DrainWake()
    {
        std::uint64_t value = 0;
        while (::read(WakeFd(), &value, sizeof(value)) > 0) {}
    }

    DisplayBackend* GetBackend()
    {
        static const std::unique_ptr<DisplayBackend> backend = PickBackend();
        return backend.get();
    }

    LinuxWindow::LinuxWindow()
    {
        g_windows.push_back(this);
    }

    LinuxWindow::~LinuxWindow()
    {
        std::erase(g_windows, this);
    }

    const std::vector<LinuxWindow*>& LinuxWindow::All()
    {
        return g_windows;
    }

    void LinuxWindow::WaitEvents()
    {
        if (DisplayBackend* backend = GetBackend()) backend->Wait();
    }

    void LinuxWindow::Wake()
    {
        const std::uint64_t one = 1;
        [[maybe_unused]] const ssize_t n = ::write(WakeFd(), &one, sizeof(one));
    }

    void LinuxWindow::PumpEvents()
    {
        PumpMessages();
        FlushEvents();
    }

    void LinuxWindow::PumpMessages()
    {
        if (DisplayBackend* backend = GetBackend()) backend->Pump();
    }

    void LinuxWindow::FlushEvents()
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

    Ref<Window> Window::Create(const WindowDesc& desc)
    {
        DisplayBackend* backend = GetBackend();
        return backend ? backend->CreateWindow(desc) : nullptr;
    }

    void Window::PumpAll()
    {
        if (DisplayBackend* backend = GetBackend()) backend->Pump();

        const std::vector<LinuxWindow*> live = g_windows;
        for (LinuxWindow* w : live)
        {
            // A prior flush may have destroyed this window (handler dropped its Ref); skip stale entries.
            if (std::find(g_windows.begin(), g_windows.end(), w) != g_windows.end())
                w->FlushEvents();
        }
    }

    bool Window::AnyOpen()
    {
        return std::ranges::any_of(g_windows, [](const LinuxWindow* w) { return !w->ShouldClose(); });
    }

    ScreenPoint Window::CursorScreenPosition()
    {
        DisplayBackend* backend = GetBackend();
        return backend ? backend->CursorPosition() : ScreenPoint{};
    }

    Window* Window::WindowAt(const ScreenPoint point)
    {
        DisplayBackend* backend = GetBackend();
        return backend ? backend->WindowAt(point) : nullptr;
    }

    bool Window::IsOnAnyMonitor(const ScreenRect& rect)
    {
        DisplayBackend* backend = GetBackend();
        return backend && backend->IsOnAnyMonitor(rect);
    }

    ScreenRect Window::PrimaryWorkArea()
    {
        DisplayBackend* backend = GetBackend();
        return backend ? backend->PrimaryWorkArea() : ScreenRect{};
    }
}
