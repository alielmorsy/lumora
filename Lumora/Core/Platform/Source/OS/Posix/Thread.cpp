#include <lumora/platform/Thread.h>

#include <pthread.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__linux__) || defined(__ANDROID__)
#include <sys/syscall.h>
#endif

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>

namespace lumora::platform
{
    namespace
    {
        pid_t GetNativeTid() noexcept
        {
#if defined(__BIONIC__) || (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30)))
            return gettid();
#elif defined(SYS_gettid)
            return static_cast<pid_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
            uint64_t tid = 0;
            pthread_threadid_np(nullptr, &tid);
            return static_cast<pid_t>(tid);
#else
            return 0;
#endif
        }

        struct PosixThreadStartContext
        {
            std::string name;
            std::function<void()> entry;
            ThreadPriority priority = ThreadPriority::Normal;
            std::mutex mtx;
            std::condition_variable cv;
            bool ready = false;
            bool mainDone = false;
            ThreadId tid = 0;
        };

        void* PosixThreadProc(void* arg)
        {
            auto* ctx = static_cast<PosixThreadStartContext*>(arg);
            std::function<void()> entry;

            SetCurrentThreadName(ctx->name);
            SetCurrentThreadPriority(ctx->priority);

            {
                std::unique_lock<std::mutex> lock(ctx->mtx);
                ctx->tid = CurrentThreadId();
                ctx->ready = true;
                ctx->cv.notify_one();
                ctx->cv.wait(lock, [&] { return ctx->mainDone; });
                entry = std::move(ctx->entry);
            }
            delete ctx;

            if (entry)
            {
                entry();
            }
            return nullptr;
        }
    }

    void SetCurrentThreadName(std::string_view name)
    {
        if (name.empty())
        {
            return;
        }
        char buf[16];
        const size_t copyLen = std::min(name.size(), static_cast<size_t>(15));
        std::memcpy(buf, name.data(), copyLen);
        buf[copyLen] = '\0';

#if defined(__APPLE__)
        pthread_setname_np(buf);
#elif defined(__linux__) || defined(__ANDROID__)
        pthread_setname_np(pthread_self(), buf);
#endif
    }

    bool SetCurrentThreadPriority(ThreadPriority priority)
    {
        int nice = 0;
        switch (priority)
        {
        case ThreadPriority::Normal:
            nice = 0;
            break;
        case ThreadPriority::High:
            nice = -2;
            break;
        case ThreadPriority::Display:
            nice = -4;
            break;
        }

#if defined(__linux__) || defined(__ANDROID__)
        const pid_t tid = GetNativeTid();
        if (tid > 0)
        {
            return setpriority(PRIO_PROCESS, static_cast<id_t>(tid), nice) == 0;
        }
#endif
        return false;
    }

    ThreadId CurrentThreadId() noexcept
    {
        const pid_t tid = GetNativeTid();
        if (tid > 0)
        {
            return static_cast<ThreadId>(tid);
        }
        return static_cast<ThreadId>(reinterpret_cast<uintptr_t>(pthread_self()));
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

        auto* ctx = new PosixThreadStartContext();
        ctx->name = std::string(name);
        ctx->entry = std::move(entry);
        ctx->priority = priority;

        pthread_t pt{};
        const int res = pthread_create(&pt, nullptr, &PosixThreadProc, ctx);
        if (res != 0)
        {
            delete ctx;
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(ctx->mtx);
            ctx->cv.wait(lock, [&] { return ctx->ready; });
            m_id = ctx->tid;
            ctx->mainDone = true;
        }
        ctx->cv.notify_one();

        m_handle = reinterpret_cast<void*>(static_cast<uintptr_t>(pt));
        return true;
    }

    void Thread::Join()
    {
        if (m_handle != nullptr)
        {
            const pthread_t pt = static_cast<pthread_t>(reinterpret_cast<uintptr_t>(m_handle));
            pthread_join(pt, nullptr);
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
