#pragma once

#include "FileDropEvents.h"
#include "InputEvents.h"
#include "NativeWindow.h"
#include "WindowEvents.h"

#include <lumora/Api.h>
#include <lumora/EventBus.h>
#include <lumora/Ref.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lumora::platform
{
    enum class FullscreenMode : uint8_t
    {
        Windowed,
        Borderless,
        Exclusive,
    };

    enum class CursorMode : uint8_t
    {
        Normal,
        Captured,   ///< hidden, clipped to the capture region, reported as relative motion only
    };

    /// Where a capture clips and re-centres, in client-area PHYSICAL pixels, top-left origin.
    /// Empty means the whole client area; an editor sets it to a panel so the pointer is not re-centred over other UI.
    struct CursorClip
    {
        int32_t x      = 0;
        int32_t y      = 0;
        int32_t width  = 0;
        int32_t height = 0;

        [[nodiscard]] constexpr bool Empty() const noexcept { return width <= 0 || height <= 0; }

        [[nodiscard]] constexpr bool operator==(const CursorClip&) const noexcept = default;
    };

    /// Physical screen pixels, virtual-desktop origin.
    struct ScreenPoint
    {
        int32_t x = 0;
        int32_t y = 0;

        [[nodiscard]] constexpr bool operator==(const ScreenPoint&) const noexcept = default;
    };

    struct ScreenRect
    {
        int32_t x      = 0;
        int32_t y      = 0;
        int32_t width  = 0;
        int32_t height = 0;

        [[nodiscard]] constexpr bool Contains(const ScreenPoint p) const noexcept
        {
            return p.x >= x && p.y >= y && p.x < x + width && p.y < y + height;
        }

        [[nodiscard]] constexpr bool operator==(const ScreenRect&) const noexcept = default;
    };

    class Window;

    struct WindowDesc
    {
        std::string                title          = "Lumora";
        uint32_t                   width          = 1280;
        uint32_t                   height         = 720;
        bool                       resizable      = true;
        FullscreenMode             fullscreenMode = FullscreenMode::Windowed;
        std::optional<ScreenPoint> position;
        Window*                    owner          = nullptr;
        bool                       showInTaskbar  = true;
    };

    class LUMORA_API Window : public RefCounted
    {
    public:
        /// Creates a platform-specific window.
        static Ref<Window> Create(const WindowDesc& desc);


        ~Window() override = default;

        /// Drains the OS message queue once, then publishes pending events for every live window.
        static void PumpAll();

        /// True if any live window has not yet requested close.
        [[nodiscard]] static bool AnyOpen();

        /// PumpMessages() then FlushEvents(); only legal where subscriber callbacks are legal (see FlushEvents).
        virtual void PumpEvents() = 0;

        /// Drains the OS queue into pending buffers WITHOUT publishing, so it runs no subscriber callback; the only half
        /// legal while a frame graph is running.
        virtual void PumpMessages() = 0;

        /// Publishes what PumpMessages queued, then clears the buffers. Subscribers run inline and can mutate the world, so
        /// call it only when no frame graph is running.
        virtual void FlushEvents() = 0;

        /// True if close was requested.
        [[nodiscard]] virtual bool ShouldClose() const = 0;

        /// Requests close (sets the close flag), e.g. after a save prompt resolves.
        virtual void RequestClose() = 0;

        /// Clears a pending close request so a frame that intercepted ShouldClose() can veto it.
        virtual void ClearCloseRequest() = 0;

        /// Event bus for window events.
        [[nodiscard]] EventBus<WindowEvent>& Events() { return _events; }

        /// Event bus for raw input events (mouse/keyboard/text).
        [[nodiscard]] EventBus<InputEvent>& InputEvents() { return _input; }

        /// Event bus for OS file-drop events.
        [[nodiscard]] EventBus<FileDropEvent>& FileDrops() { return _fileDrops; }

        [[nodiscard]] virtual NativeWindowHandle GetNativeHandle()    const = 0;

        /// Client-area width in pixels.
        [[nodiscard]] virtual uint32_t           GetWidth()           const = 0;

        /// Client-area height in pixels.
        [[nodiscard]] virtual uint32_t           GetHeight()          const = 0;

        /// High-DPI scale factor (1.0 = 100%).
        [[nodiscard]] virtual float              GetDpiScale()        const = 0;

        [[nodiscard]] virtual FullscreenMode     GetFullscreenMode()  const = 0;

        virtual void SetTitle(std::string_view title)   = 0;

        virtual void SetFullscreenMode(FullscreenMode mode)  = 0;

        /// Capture hides the cursor, clips it to `clip` (empty = whole client area) and switches to relative motion:
        /// `InputEvent::delta` moves while `position` freezes.
        /// Safe to call every frame with a moving rect: a re-clip while captured re-anchors without touching the show-cursor counter.
        virtual void SetCursorMode(CursorMode mode, const CursorClip& clip) = 0;

        void SetCursorMode(const CursorMode mode) { SetCursorMode(mode, CursorClip{}); }

        [[nodiscard]] virtual CursorMode GetCursorMode() const = 0;

        /// The client area in physical screen pixels.
        [[nodiscard]] virtual ScreenRect GetClientRect() const = 0;

        /// Moves and sizes the window so its client area is `rect`.
        virtual void SetClientRect(const ScreenRect& rect) = 0;

        [[nodiscard]] virtual ScreenPoint ClientToScreen(float x, float y) const = 0;

        virtual void Focus() = 0;

        /// Minimized, or hidden along with a minimized owner: the window has nothing on screen to present to.
        [[nodiscard]] virtual bool IsHidden() const = 0;

        [[nodiscard]] static ScreenPoint CursorScreenPosition();

        /// The window whose client area is topmost at `point`; null over anything else.
        [[nodiscard]] static Window* WindowAt(ScreenPoint point);

        [[nodiscard]] static bool       IsOnAnyMonitor(const ScreenRect& rect);
        [[nodiscard]] static ScreenRect PrimaryWorkArea();

    protected:
        EventBus<WindowEvent>   _events;
        EventBus<InputEvent>    _input;
        EventBus<FileDropEvent> _fileDrops;
    };
}
