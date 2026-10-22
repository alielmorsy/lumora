#pragma once

#include <lumora/platform/Window.h>
#include <lumora/platform/android/AndroidApp.h>

#include <game-activity/GameActivity.h>
#include <game-activity/native_app_glue/android_native_app_glue.h>

#include <vector>

namespace lumora::platform
{
    class AndroidWindow : public Window
    {
    public:
        explicit AndroidWindow(const WindowDesc& desc);
        ~AndroidWindow() override;

        void PumpEvents() override;
        void PumpMessages() override;
        void FlushEvents() override;

        [[nodiscard]] bool ShouldClose() const noexcept override { return _shouldClose; }
        void RequestClose() noexcept override { _shouldClose = true; }
        void ClearCloseRequest() noexcept override { _shouldClose = false; }

        [[nodiscard]] NativeWindowHandle GetNativeHandle() const noexcept override;
        [[nodiscard]] uint32_t GetWidth() const noexcept override { return _width; }
        [[nodiscard]] uint32_t GetHeight() const noexcept override { return _height; }
        [[nodiscard]] float GetDpiScale() const noexcept override { return _dpiScale; }
        [[nodiscard]] FullscreenMode GetFullscreenMode() const noexcept override { return _fullscreenMode; }

        void SetTitle(std::string_view title) override;
        void SetFullscreenMode(FullscreenMode mode) override;
        void SetCursorMode(CursorMode mode, const CursorClip& clip) override;
        [[nodiscard]] CursorMode GetCursorMode() const noexcept override { return CursorMode::Normal; }

        [[nodiscard]] ScreenRect GetClientRect() const noexcept override;
        void SetClientRect(const ScreenRect& rect) override;
        [[nodiscard]] ScreenPoint ClientToScreen(float x, float y) const noexcept override;

        void Focus() override;
        [[nodiscard]] bool IsHidden() const noexcept override;

        void SetTextInputActive(bool active) override;

        [[nodiscard]] ANativeWindow* GetNativeWindow() const noexcept { return _window; }
        [[nodiscard]] ScreenPoint GetLastTouchPoint() const noexcept { return {static_cast<int32_t>(_lastTouchPos.x), static_cast<int32_t>(_lastTouchPos.y)}; }

        void OnInitWindow(ANativeWindow* window);
        void OnTermWindow();
        void OnFocusChanged(bool focused);
        void OnConfigChanged();
        void OnPause();
        void OnResume();

        std::vector<InputEvent>& PendingInput() { return _pendingInput; }

    private:
        void UpdateDpi();
        void ProcessInputEvents();
        void ProcessTextInput();

        ANativeWindow*          _window = nullptr;
        uint32_t                _width = 0;
        uint32_t                _height = 0;
        float                   _dpiScale = 1.0f;
        FullscreenMode          _fullscreenMode = FullscreenMode::Exclusive;
        bool                    _shouldClose = false;
        bool                    _focused = true;
        bool                    _paused = false;
        bool                    _textInputActive = false;
        InputVec2               _lastTouchPos{0.f, 0.f};

        std::vector<WindowEvent> _pendingEvents;
        std::vector<InputEvent>  _pendingInput;
    };
}
