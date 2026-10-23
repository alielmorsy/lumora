#include <lumora/platform/Thread.h>

#include <Windows.h>
#include <process.h>

#include <string>
#include <utility>

namespace lumora::platform
{
    namespace
    {
        struct ThreadStartContext
        {
            std::string name;
            std::function<void()> entry;
            ThreadPriority priority;
        };

        unsigned int __stdcall WinThreadProc(void* arg)
        {
            auto* ctx = static_cast<ThreadStartContext*>(arg);
            std::string name = std::move(ctx->name);
            auto entry = std::move(ctx->entry);
            const ThreadPriority priority = ctx->priority;
            delete ctx;

            SetCurrentThreadName(name);
            SetCurrentThreadPriority(priority);

            if (entry)
            {
                entry();
            }
            return 0;
        }
    }

    void SetCurrentThreadName(std::string_view name)
    {
        if (name.empty())
        {
            return;
        }
        const int wideLen = ::MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), nullptr, 0);
        if (wideLen <= 0)
        {
            return;
        }
        std::wstring wide(static_cast<size_t>(wideLen), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, name.data(), static_cast<int>(name.size()), wide.data(), wideLen);
        ::SetThreadDescription(::GetCurrentThread(), wide.c_str());
    }

    bool SetCurrentThreadPriority(ThreadPriority priority)
    {
        int winPriority = THREAD_PRIORITY_NORMAL;
        switch (priority)
        {
        case ThreadPriority::Normal:
            winPriority = THREAD_PRIORITY_NORMAL;
            break;
        case ThreadPriority::High:
            winPriority = THREAD_PRIORITY_ABOVE_NORMAL;
            break;
        case ThreadPriority::Display:
            winPriority = THREAD_PRIORITY_HIGHEST;
            break;
        }
        return ::SetThreadPriority(::GetCurrentThread(), winPriority) != 0;
    }

    ThreadId CurrentThreadId() noexcept
    {
        return static_cast<ThreadId>(::GetCurrentThreadId());
    }

    Thread::Thread() noexcept = default;

    Thread::~Thread()
    {
        if (Joinable())
        {
            Join();
        }
    }

    Thread::Thread(Thread&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr))
        , m_id(std::exchange(other.m_id, 0))
    {
    }

    Thread& Thread::operator=(Thread&& other) noexcept
    {
        if (this != &other)
        {
            if (Joinable())
            {
                Join();
            }
            m_handle = std::exchange(other.m_handle, nullptr);
            m_id = std::exchange(other.m_id, 0);
        }
        return *this;
    }

    bool Thread::Start(std::string_view name, std::function<void()> entry, ThreadPriority priority)
    {
        if (Joinable())
        {
            return false;
        }

        auto* ctx = new ThreadStartContext{std::string(name), std::move(entry), priority};
        unsigned int threadId = 0;
        const uintptr_t handle = _beginthreadex(nullptr, 0, &WinThreadProc, ctx, 0, &threadId);
        if (handle == 0)
        {
            delete ctx;
            return false;
        }

        m_handle = reinterpret_cast<void*>(handle);
        m_id = static_cast<ThreadId>(threadId);
        return true;
    }

    void Thread::Join()
    {
        if (m_handle != nullptr)
        {
            ::WaitForSingleObject(static_cast<HANDLE>(m_handle), INFINITE);
            ::CloseHandle(static_cast<HANDLE>(m_handle));
            m_handle = nullptr;
            m_id = 0;
        }
    }

    bool Thread::Joinable() const noexcept
    {
        return m_handle != nullptr;
    }

    ThreadId Thread::Id() const noexcept
    {
        return m_id;
    }
}
