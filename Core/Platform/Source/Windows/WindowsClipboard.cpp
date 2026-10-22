#include <lumora/platform/Clipboard.h>

#include "WindowsHeaders.h"

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace lumora::platform
{
    namespace
    {
        std::wstring Widen(std::string_view s)
        {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            if (n <= 0) return {};
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        std::string Narrow(const wchar_t* s)
        {
            if (!s || !*s) return {};
            const int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
            if (n <= 1) return {}; // n counts the terminator
            std::string out(static_cast<std::size_t>(n - 1), '\0');
            ::WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
            return out;
        }

        /// The clipboard is one system-wide lock another process may hold at that instant, so a single OpenClipboard attempt
        /// is flaky; retry briefly.
        class ClipboardLock
        {
        public:
            ClipboardLock()
            {
                for (int attempt = 0; attempt < kAttempts; ++attempt)
                {
                    if (::OpenClipboard(nullptr))
                    {
                        m_open = true;
                        return;
                    }
                    ::Sleep(kRetryMs);
                }
            }

            ~ClipboardLock()
            {
                if (m_open) ::CloseClipboard();
            }

            ClipboardLock(const ClipboardLock&)            = delete;
            ClipboardLock& operator=(const ClipboardLock&) = delete;

            [[nodiscard]] explicit operator bool() const noexcept { return m_open; }

        private:
            static constexpr int kAttempts = 5;
            static constexpr DWORD kRetryMs = 2;

            bool m_open = false;
        };
    } // namespace

    bool SetClipboardText(std::string_view text)
    {
        const std::wstring wide = Widen(text);

        const ClipboardLock lock;
        if (!lock) return false;

        if (!::EmptyClipboard()) return false;

        // The clipboard owns the global after a successful SetClipboardData; free the handle only on failure (otherwise a double free).
        const std::size_t bytes  = (wide.size() + 1) * sizeof(wchar_t);
        const HGLOBAL     handle = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (!handle) return false;

        if (auto* dst = static_cast<wchar_t*>(::GlobalLock(handle)))
        {
            std::memcpy(dst, wide.c_str(), bytes);
            ::GlobalUnlock(handle);
        }
        else
        {
            ::GlobalFree(handle);
            return false;
        }

        if (!::SetClipboardData(CF_UNICODETEXT, handle))
        {
            ::GlobalFree(handle);
            return false;
        }
        return true;
    }

    std::string GetClipboardText()
    {
        if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) return {};

        const ClipboardLock lock;
        if (!lock) return {};

        const HANDLE handle = ::GetClipboardData(CF_UNICODETEXT);
        if (!handle) return {};

        std::string out;
        if (const auto* src = static_cast<const wchar_t*>(::GlobalLock(handle)))
        {
            out = Narrow(src);
            ::GlobalUnlock(handle);
        }
        return out;
    }
}
