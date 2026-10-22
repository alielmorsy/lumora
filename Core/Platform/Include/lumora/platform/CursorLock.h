#pragma once

#include <lumora/Api.h>

#include <cstdint>

namespace lumora::platform
{
    enum class CursorLock : std::uint8_t
    {
        Free,
        Locked, ///< hidden, pinned to the centre of the host's capture region, relative motion only
    };

    /// Process-wide cursor-lock request: game code states intent, the host applies it to a window and sub-rect.
    /// Last writer wins on purpose (a refcount would let one leaked token trap the cursor); the host may refuse until the
    /// request changes (see CursorLockSerial).
    LUMORA_API void RequestCursorLock(CursorLock mode) noexcept;

    [[nodiscard]] LUMORA_API CursorLock CursorLockRequest() noexcept;

    /// Bumped on every RequestCursorLock, even if the value is unchanged, so a host can tell a repeated request from one it
    /// already refused.
    [[nodiscard]] LUMORA_API std::uint32_t CursorLockSerial() noexcept;
}
