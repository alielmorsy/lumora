#pragma once

#include "WindowsHeaders.h"

#include <lumora/platform/ProgressDialog.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace lumora::platform
{
    /// Win32 owner-drawn progress window: two filled rectangles, avoiding a comctl32 dependency and a v6 manifest.
    /// Thread ownership: `_hwnd`, the pump, fonts and all GDI calls live on `_thread` only; public mutators write `_total`,
    /// `_done` and the strings, which the timer reads on its next tick.
    class WindowsProgressDialog final : public ProgressDialog
    {
    public:
        explicit WindowsProgressDialog(const ProgressDialogDesc& desc);
        ~WindowsProgressDialog() override;

        void AddTotal(uint64_t delta) noexcept override;
        void AddCompleted(uint64_t delta) noexcept override;
        void SetPhase(std::string_view text) override;
        void SetStatus(std::string_view text) override;
        void Close() override;

        [[nodiscard]] bool Alive() const noexcept { return _hwnd.load(std::memory_order_acquire) != nullptr; }

        /// Public only because the window class is registered from file scope. Never call it directly.
        static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    private:
        void ThreadMain(ProgressDialogDesc desc);
        void Paint(HWND hwnd);

        std::thread       _thread;
        std::atomic<HWND> _hwnd{nullptr};

        /// Created, used and destroyed on `_thread`; derived from the shell's message font.
        HFONT _fontBody = nullptr;
        HFONT _fontDim  = nullptr;
        int   _dpi      = 96;

        std::atomic<uint64_t> _total{0};
        std::atomic<uint64_t> _done{0};

        /// Advanced by the timer, so the indeterminate block sweeps while nothing is known yet.
        std::atomic<uint32_t> _ticks{0};

        /// Held only for a std::wstring assign, so a descheduled holder cannot stall the paint.
        mutable std::mutex _textMutex;
        std::wstring       _phase;
        std::wstring       _status;

        /// Set once window creation has resolved either way, so Create never returns an undecided HWND and the first SetStatus is not lost.
        std::atomic<bool> _started{false};
    };
}
