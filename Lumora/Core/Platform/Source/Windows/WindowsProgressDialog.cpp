#include "WindowsProgressDialog.h"

#include "lumora/log/Log.h"

#include <algorithm>
#include <utility>

LUMORA_LOG_CATEGORY("Platform.Progress")

namespace lumora::platform
{
    namespace
    {
        const wchar_t* const kClassName = L"LumoraProgressDialogClass";

        std::once_flag g_classOnce;
        bool           g_classOk = false;

        /// Repaint cadence: fast enough to read as motion, slow enough that a fast-reporting load costs one paint per few updates.
        constexpr UINT kTimerId = 1;
        constexpr UINT kTimerMs = 33;

        // Theme colours, hardcoded: this window must come up before any stylesheet has loaded.
        constexpr COLORREF kBg          = RGB(0x17, 0x17, 0x17);
        constexpr COLORREF kTrack       = RGB(0x0a, 0x0a, 0x0a);
        constexpr COLORREF kTrackBorder = RGB(0x2a, 0x2a, 0x2a);
        constexpr COLORREF kFill        = RGB(0xed, 0xed, 0xed);
        constexpr COLORREF kText        = RGB(0xe6, 0xe6, 0xe6);
        constexpr COLORREF kTextDim     = RGB(0x8a, 0x8a, 0x8a);

        std::wstring Widen(std::string_view s)
        {
            if (s.empty()) return {};
            const int len =
                ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (len <= 0) return {};
            std::wstring out(static_cast<std::size_t>(len), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
            return out;
        }

        /// The shell's message font at `dpi`, shrunk by `deltaPt` where a dimmer, smaller line is wanted.
        HFONT MakeUiFont(int dpi, int deltaPt)
        {
            NONCLIENTMETRICSW ncm{};
            ncm.cbSize = sizeof(ncm);
            if (!::SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0,
                                              static_cast<UINT>(dpi)))
                return static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

            LOGFONTW lf = ncm.lfMessageFont;
            if (deltaPt != 0)
            {
                // lfHeight is negative for character height, so shrinking means moving toward zero.
                const LONG scaled = ::MulDiv(deltaPt, dpi, 96);
                lf.lfHeight       = lf.lfHeight < 0 ? std::min<LONG>(lf.lfHeight + scaled, -1)
                                                    : std::max<LONG>(lf.lfHeight - scaled, 1);
            }
            HFONT font = ::CreateFontIndirectW(&lf);
            return font != nullptr ? font : static_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        }

        void EnsureClass()
        {
            std::call_once(g_classOnce,
                           []
                           {
                               WNDCLASSEXW wc{};
                               wc.cbSize        = sizeof(WNDCLASSEXW);
                               wc.lpfnWndProc   = &WindowsProgressDialog::WndProc;
                               wc.hInstance     = ::GetModuleHandleW(nullptr);
                               wc.lpszClassName = kClassName;
                               wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
                               // No background brush: the memory-DC blit covers every pixel, and an OS erase first causes flicker.
                               wc.hbrBackground = nullptr;

                               g_classOk = ::RegisterClassExW(&wc) != 0;
                               if (!g_classOk)
                                   LOG_ERROR("RegisterClassExW for the progress dialog failed ({})",
                                             ::GetLastError());
                           });
        }
    }

    Ref<ProgressDialog> ProgressDialog::Create(const ProgressDialogDesc& desc)
    {
        auto dialog = MakeRef<WindowsProgressDialog>(desc);
        if (!dialog->Alive())
        {
            // Not an error for the caller: a boot without a progress window is fine, and the cause is already logged.
            dialog->Close();
            return {};
        }
        return dialog;
    }

    WindowsProgressDialog::WindowsProgressDialog(const ProgressDialogDesc& desc)
    {
        _phase  = Widen(desc.phase);
        _status = Widen(desc.status);

        _thread = std::thread(&WindowsProgressDialog::ThreadMain, this, desc);

        // Atomic wait rather than polling: the window exists or has failed by the time Create returns, so a status set on the
        // next line is not dropped.
        _started.wait(false, std::memory_order_acquire);
    }

    WindowsProgressDialog::~WindowsProgressDialog() { Close(); }

    void WindowsProgressDialog::AddTotal(const uint64_t delta) noexcept
    {
        _total.fetch_add(delta, std::memory_order_relaxed);
    }

    void WindowsProgressDialog::AddCompleted(const uint64_t delta) noexcept
    {
        _done.fetch_add(delta, std::memory_order_relaxed);
    }

    void WindowsProgressDialog::SetPhase(const std::string_view text)
    {
        std::wstring widened = Widen(text);
        std::lock_guard lock(_textMutex);
        _phase = std::move(widened);
    }

    void WindowsProgressDialog::SetStatus(const std::string_view text)
    {
        // Widened outside the lock: the conversion is the expensive half.
        std::wstring widened = Widen(text);
        std::lock_guard lock(_textMutex);
        _status = std::move(widened);
    }

    void WindowsProgressDialog::Close()
    {
        if (const HWND hwnd = _hwnd.exchange(nullptr, std::memory_order_acq_rel); hwnd != nullptr)
            ::PostMessageW(hwnd, WM_CLOSE, 0, 0);

        if (_thread.joinable()) _thread.join();
    }

    void WindowsProgressDialog::ThreadMain(ProgressDialogDesc desc)
    {
        // Announced on every exit path, including failures: the constructor's wait has no other way to learn there is no window.
        struct Announce
        {
            std::atomic<bool>& flag;
            ~Announce()
            {
                flag.store(true, std::memory_order_release);
                flag.notify_all();
            }
        } announce{_started};

        EnsureClass();
        if (!g_classOk) return;

        _dpi = static_cast<int>(::GetDpiForSystem());

        RECT frame{0, 0, ::MulDiv(static_cast<int>(desc.width), _dpi, 96),
                   ::MulDiv(static_cast<int>(desc.height), _dpi, 96)};

        // WS_CAPTION without WS_SYSMENU: draggable but no close box, since a boot has no cancel path.
        constexpr DWORD kStyle   = WS_POPUP | WS_CAPTION | WS_BORDER;
        constexpr DWORD kStyleEx = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
        ::AdjustWindowRectExForDpi(&frame, kStyle, FALSE, kStyleEx, static_cast<UINT>(_dpi));

        const int frameW = frame.right - frame.left;
        const int frameH = frame.bottom - frame.top;

        RECT work{};
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const int x = work.left + (work.right - work.left - frameW) / 2;
        const int y = work.top + (work.bottom - work.top - frameH) / 2;

        const std::wstring title = Widen(desc.title);
        const HWND         hwnd  = ::CreateWindowExW(kStyleEx, kClassName, title.c_str(), kStyle, x, y,
                                                     frameW, frameH, nullptr, nullptr,
                                                     ::GetModuleHandleW(nullptr), this);
        if (hwnd == nullptr)
        {
            LOG_ERROR("CreateWindowExW for the progress dialog failed ({})", ::GetLastError());
            return;
        }

        _fontBody = MakeUiFont(_dpi, 0);
        _fontDim  = MakeUiFont(_dpi, 1);

        _hwnd.store(hwnd, std::memory_order_release);
        ::SetTimer(hwnd, kTimerId, kTimerMs, nullptr);
        ::ShowWindow(hwnd, SW_SHOWNORMAL);
        ::UpdateWindow(hwnd);

        // Published before the pump starts so early reports land in a window that is already drawing.
        announce.flag.store(true, std::memory_order_release);
        announce.flag.notify_all();

        MSG msg{};
        while (::GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }

        // Stock objects are shared and must not be deleted; MakeUiFont returns one when it cannot build its own.
        const HGDIOBJ stock = ::GetStockObject(DEFAULT_GUI_FONT);
        if (_fontBody != nullptr && static_cast<HGDIOBJ>(_fontBody) != stock) ::DeleteObject(_fontBody);
        if (_fontDim != nullptr && static_cast<HGDIOBJ>(_fontDim) != stock) ::DeleteObject(_fontDim);
        _fontBody = nullptr;
        _fontDim  = nullptr;
    }

    void WindowsProgressDialog::Paint(const HWND hwnd)
    {
        PAINTSTRUCT ps{};
        const HDC   dc = ::BeginPaint(hwnd, &ps);
        if (dc == nullptr) return;

        RECT client{};
        ::GetClientRect(hwnd, &client);
        const int w = client.right - client.left;
        const int h = client.bottom - client.top;

        // A minimized window reports a zero client area; a zero width would make the sweep modulus zero and the fill width negative.
        if (w <= 0 || h <= 0)
        {
            ::EndPaint(hwnd, &ps);
            return;
        }

        // One blit at the end rather than draws onto the visible surface, which tears.
        const HDC     mem = ::CreateCompatibleDC(dc);
        const HBITMAP bmp = ::CreateCompatibleBitmap(dc, w, h);
        const auto    oldBmp = static_cast<HBITMAP>(::SelectObject(mem, bmp));

        const auto scale = [this](const int dip) { return ::MulDiv(dip, _dpi, 96); };

        const HBRUSH bgBrush = ::CreateSolidBrush(kBg);
        ::FillRect(mem, &client, bgBrush);
        ::DeleteObject(bgBrush);

        const uint64_t total = _total.load(std::memory_order_relaxed);
        const uint64_t done  = std::min(_done.load(std::memory_order_relaxed), total);

        std::wstring phase;
        std::wstring status;
        {
            std::lock_guard lock(_textMutex);
            phase  = _phase;
            status = _status;
        }

        const int pad     = scale(16);
        const int lineH   = scale(18);
        const int barH    = scale(8);
        const int textW   = w - pad * 2;

        ::SetBkMode(mem, TRANSPARENT);

        // Phase line
        const auto oldFont = static_cast<HFONT>(::SelectObject(mem, _fontBody));
        ::SetTextColor(mem, kText);
        RECT phaseRect{pad, pad, pad + textW, pad + lineH};
        ::DrawTextW(mem, phase.c_str(), -1, &phaseRect,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (total > 0)
        {
            // `done / total`, not a percentage: the total grows as dependencies are discovered, so a percentage can fall.
            const std::wstring counts =
                std::to_wstring(done) + L" / " + std::to_wstring(total);
            ::SelectObject(mem, _fontDim);
            ::SetTextColor(mem, kTextDim);
            RECT countRect{pad, pad, pad + textW, pad + lineH};
            ::DrawTextW(mem, counts.c_str(), -1, &countRect,
                        DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
        }

        // Track
        const int barY = pad + lineH + scale(12);
        RECT      track{pad, barY, pad + textW, barY + barH};

        const HBRUSH trackBrush = ::CreateSolidBrush(kTrack);
        ::FillRect(mem, &track, trackBrush);
        ::DeleteObject(trackBrush);

        const HBRUSH borderBrush = ::CreateSolidBrush(kTrackBorder);
        ::FrameRect(mem, &track, borderBrush);
        ::DeleteObject(borderBrush);

        // Fill
        const HBRUSH fillBrush = ::CreateSolidBrush(kFill);
        if (total == 0)
        {
            // Indeterminate "waiting" state: nothing discovered yet, so show a sweeping block.
            const int blockW = std::max(textW / 4, scale(24));
            const int span   = std::max(textW + blockW, 1);
            const int offset = static_cast<int>((_ticks.load(std::memory_order_relaxed) * 7u)
                                                % static_cast<uint32_t>(span));

            RECT block{pad + offset - blockW, barY + 1, pad + offset, barY + barH - 1};
            block.left  = std::max(block.left, static_cast<LONG>(pad + 1));
            block.right = std::min(block.right, static_cast<LONG>(pad + textW - 1));
            if (block.right > block.left) ::FillRect(mem, &block, fillBrush);
        }
        else if (textW > 2)
        {
            const int fillW =
                static_cast<int>(static_cast<uint64_t>(textW - 2) * done / std::max<uint64_t>(total, 1));
            if (fillW > 0)
            {
                RECT fill{pad + 1, barY + 1, pad + 1 + fillW, barY + barH - 1};
                ::FillRect(mem, &fill, fillBrush);
            }
        }
        ::DeleteObject(fillBrush);

        // Status line
        ::SelectObject(mem, _fontDim);
        ::SetTextColor(mem, kTextDim);
        const int statusY = barY + barH + scale(12);
        RECT      statusRect{pad, statusY, pad + textW, statusY + lineH};
        // PATH_ELLIPSIS, not END_ELLIPSIS: the file name at the end of the path identifies an asset.
        ::DrawTextW(mem, status.c_str(), -1, &statusRect,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(mem, oldFont);
        ::BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);

        ::SelectObject(mem, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(mem);
        ::EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK WindowsProgressDialog::WndProc(const HWND hwnd, const UINT msg, const WPARAM wparam,
                                                    const LPARAM lparam)
    {
        if (msg == WM_NCCREATE)
        {
            const auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lparam);
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }

        auto* self = reinterpret_cast<WindowsProgressDialog*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self == nullptr) return ::DefWindowProcW(hwnd, msg, wparam, lparam);

        switch (msg)
        {
            case WM_ERASEBKGND:
                // Claimed because Paint covers every pixel; otherwise the OS flat-fills the window first, which flickers.
                return 1;

            case WM_TIMER:
                if (wparam == kTimerId)
                {
                    self->_ticks.fetch_add(1, std::memory_order_relaxed);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;

            case WM_PAINT:
                self->Paint(hwnd);
                return 0;

            case WM_CLOSE:
                ::DestroyWindow(hwnd);
                return 0;

            case WM_DESTROY:
                ::KillTimer(hwnd, kTimerId);
                ::PostQuitMessage(0);
                return 0;

            default:
                return ::DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }
}
