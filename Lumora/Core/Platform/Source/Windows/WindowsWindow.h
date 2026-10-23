#pragma once

#include "lumora/platform/Window.h"

#include "WindowsHeaders.h"

#include <string>
#include <vector>

namespace lumora::platform
{
    class WindowsWindow final : public Window
    {
    public:
        explicit WindowsWindow(const WindowDesc& desc);

        ~WindowsWindow() override;

        void WaitEvents() override;
        void Wake() override;

        void PumpEvents() override;

        void PumpMessages() override;

        void FlushEvents() override;

        [[nodiscard]] bool ShouldClose() const override { return _shouldClose; }

        void RequestClose() override { _shouldClose = true; }
        void ClearCloseRequest() override { _shouldClose = false; }

        [[nodiscard]] NativeWindowHandle GetNativeHandle()   const override;

        [[nodiscard]] uint32_t           GetWidth()          const override { return _width; }

        [[nodiscard]] uint32_t           GetHeight()         const override { return _height; }

        [[nodiscard]] float              GetDpiScale()       const override { return _dpiScale; }

        [[nodiscard]] FullscreenMode     GetFullscreenMode() const override { return _fullscreenMode; }

        void SetTitle(std::string_view title)  override;

        void SetFullscreenMode(FullscreenMode mode) override;

        void SetCursorMode(CursorMode mode, const CursorClip& clip) override;

        using Window::SetCursorMode;

        [[nodiscard]] CursorMode GetCursorMode() const override { return _cursorMode; }

        [[nodiscard]] ScreenRect  GetClientRect() const override;
        void                      SetClientRect(const ScreenRect& rect) override;
        [[nodiscard]] ScreenPoint ClientToScreen(float x, float y) const override;
        void                      Focus() override;
        [[nodiscard]] bool        IsHidden() const override;

    private:
        /// Re-derives the screen-space clip rect and anchor from `_cursorClip` and re-pins the pointer. Called on capture and
        /// whenever a captured window moves or resizes (ClipCursor takes screen coordinates).
        void ApplyCapture();

        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        HWND           _hwnd           = nullptr;
        HINSTANCE      _hinstance      = nullptr;
        uint32_t       _width          = 0;
        uint32_t       _height         = 0;
        float          _dpiScale       = 1.0f;
        bool           _shouldClose    = false;
        FullscreenMode _fullscreenMode = FullscreenMode::Windowed;
        CursorMode     _cursorMode     = CursorMode::Normal;
        CursorClip     _cursorClip{};
        POINT          _cursorAnchor{};
        InputVec2      _lastPointerPos{};

        LONG            _savedStyle    = 0;
        LONG            _savedExStyle  = 0;
        WINDOWPLACEMENT _savedPlacement{};
        std::wstring    _exclusiveDevice;

        std::vector<WindowEvent>   _pending;
        std::vector<InputEvent>    _pendingInput;
        std::vector<FileDropEvent> _pendingDrops;
        wchar_t                    _pendingSurrogate = 0;
    };
}
