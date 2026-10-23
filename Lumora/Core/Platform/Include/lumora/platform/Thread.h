#pragma once

#include <lumora/Api.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace lumora::platform
{
    enum class ThreadPriority : std::uint8_t
    {
        Normal,
        High,
        Display
    };

    using ThreadId = std::uint64_t;

    class LUMORA_API Thread
    {
    public:
        Thread() noexcept;
        ~Thread();

        Thread(Thread&& other) noexcept;
        Thread& operator=(Thread&& other) noexcept;

        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        /// Runs `entry` on a new thread named `name` at `priority` (best effort). False if already started or creation failed.
        bool Start(std::string_view name, std::function<void()> entry, ThreadPriority priority = ThreadPriority::Normal);

        void Join();

        [[nodiscard]] bool Joinable() const noexcept;
        [[nodiscard]] ThreadId Id() const noexcept;

    private:
        void* m_handle = nullptr;
        ThreadId m_id = 0;
    };

    LUMORA_API void SetCurrentThreadName(std::string_view name);
    LUMORA_API bool SetCurrentThreadPriority(ThreadPriority priority);
    [[nodiscard]] LUMORA_API ThreadId CurrentThreadId() noexcept;
}
