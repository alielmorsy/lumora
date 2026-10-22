#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace lumora::platform
{
    struct ProgressDialogDesc
    {
        std::string title  = "Lumora";
        std::string phase  = "Starting up";
        std::string status = "Please wait...";
        /// Client area, in unscaled DIPs. The frame is grown around it for the caption.
        uint32_t    width  = 460;
        uint32_t    height = 102;
    };

    /// A small OS window with its own message pump on its own thread, so it can show progress while the main thread is
    /// blocked (e.g. at boot).
    /// Not part of normal rendering: one owner-drawn HWND repainted by a timer, on a thread no worker scheduler uses (message
    /// queues are thread-affine).
    /// Mutators are callable from any thread and never block on the UI thread.
    class LUMORA_API ProgressDialog : public RefCounted
    {
    public:
        /// Raises the window and returns once it exists. Null if the platform could not create it; treat that as "no progress
        /// reporting", not a boot failure.
        static Ref<ProgressDialog> Create(const ProgressDialogDesc& desc);

        ~ProgressDialog() override = default;

        /// Raise the denominator. The normal case, not an error: work is discovered as earlier work runs.
        virtual void AddTotal(uint64_t delta) noexcept = 0;

        virtual void AddCompleted(uint64_t delta) noexcept = 0;

        /// The stage under way, drawn above the bar. Copied.
        virtual void SetPhase(std::string_view text) = 0;

        /// The item under way, drawn below the bar. Copied.
        virtual void SetStatus(std::string_view text) = 0;

        /// Destroys the window and joins its thread. Idempotent; called by the destructor.
        /// Blocks until the UI thread leaves its pump (microseconds), so do not call it from a worker thread you cannot afford to lose.
        virtual void Close() = 0;
    };
}
