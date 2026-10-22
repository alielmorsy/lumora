#include <lumora/platform/CursorLock.h>

#include <atomic>

namespace lumora::platform
{
    namespace
    {
        /// Relaxed throughout: a single byte of intent read once per frame. The serial is bumped after the value so a host
        /// seeing a new serial cannot read a stale mode.
        std::atomic<CursorLock>    g_request{CursorLock::Free};
        std::atomic<std::uint32_t> g_serial{0};
    }

    void RequestCursorLock(const CursorLock mode) noexcept
    {
        g_request.store(mode, std::memory_order_release);
        g_serial.fetch_add(1, std::memory_order_release);
    }

    CursorLock CursorLockRequest() noexcept
    {
        return g_request.load(std::memory_order_acquire);
    }

    std::uint32_t CursorLockSerial() noexcept
    {
        return g_serial.load(std::memory_order_acquire);
    }
}
