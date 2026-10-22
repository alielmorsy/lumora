#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace lumora::log
{
    enum class Level : uint8_t
    {
        Trace,
        Info,
        Warn,
        Error,
        Fatal,
    };

    /// Lines below this level are dropped before formatting.
    LUMORA_API void SetMinLevel(Level level) noexcept;
    [[nodiscard]] LUMORA_API Level MinLevel() noexcept;

    /// Formats with std::format and hands the line to the global sink.
    class LUMORA_API Logger : public RefCounted
    {
    public:
        explicit Logger(std::string name) noexcept : _name(std::move(name))
        {
        }

        template <typename... Args>
        void Trace(std::format_string<Args...> fmt, Args&&... args)
        {
            if (Level::Trace < MinLevel()) return;
            Dispatch(Level::Trace, std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        void Info(std::format_string<Args...> fmt, Args&&... args)
        {
            if (Level::Info < MinLevel()) return;
            Dispatch(Level::Info, std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        void Warn(std::format_string<Args...> fmt, Args&&... args)
        {
            if (Level::Warn < MinLevel()) return;
            Dispatch(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        void Error(std::format_string<Args...> fmt, Args&&... args)
        {
            if (Level::Error < MinLevel()) return;
            Dispatch(Level::Error, std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        void Fatal(std::format_string<Args...> fmt, Args&&... args)
        {
            Dispatch(Level::Fatal, std::format(fmt, std::forward<Args>(args)...));
        }

        void Log(Level level, std::string_view message)
        {
            if (level < MinLevel()) return;
            Dispatch(level, std::string(message));
        }

        [[nodiscard]] std::string_view Name() const noexcept { return _name; }

    private:
        void Dispatch(Level level, const std::string& message) noexcept;

        std::string _name;
    };

    /// Returns the named logger, creating it on first use.
    [[nodiscard]] LUMORA_API Ref<Logger> GetLogger(std::string_view name);

    /// Receives every rendered line. The default writes to stderr.
    using SinkFn = void (*)(Level level, const char* category, const char* message) noexcept;

    /// Passing nullptr restores the default stderr sink.
    LUMORA_API void SetSink(SinkFn sink) noexcept;

    /// Observer sinks run after the primary sink and never displace it.
    using SinkHandle = std::uint64_t;
    using SinkCallback = std::function<void(Level level, const char* category, const char* message)>;

    /// Thread-safe. The callback runs on the logging thread: keep it quick and non-throwing.
    [[nodiscard]] LUMORA_API SinkHandle AddSink(SinkCallback sink);

    /// Call before the callback's captured state dies. Blocks until an in-flight dispatch finishes.
    LUMORA_API void RemoveSink(SinkHandle handle) noexcept;
}

// Per-file category: LUMORA_LOG_CATEGORY("Renderer"); then LOG_INFO("frame {}", n);
#define LUMORA_LOG_CATEGORY(categoryName)                                              \
    namespace                                                                          \
    {                                                                                  \
        inline ::lumora::log::Logger& _LumoraLogCategory() noexcept                     \
        {                                                                              \
            static ::lumora::Ref<::lumora::log::Logger> logger =                       \
                ::lumora::log::GetLogger(categoryName);                                \
            return *logger;                                                            \
        }                                                                              \
    }
#ifndef _NOEBUG
#define LOG_TRACE(...) (_LumoraLogCategory().Trace(__VA_ARGS__))
#endif
#define LOG_INFO(...)  (_LumoraLogCategory().Info(__VA_ARGS__))
#define LOG_WARN(...)  (_LumoraLogCategory().Warn(__VA_ARGS__))
#define LOG_ERROR(...) (_LumoraLogCategory().Error(__VA_ARGS__))
#define LOG_FATAL(...) (_LumoraLogCategory().Fatal(__VA_ARGS__))

#if defined(NDEBUG)
#define LOG_DEBUG(...) ((void)0)
#else
#define LOG_DEBUG(...) (_LumoraLogCategory().Trace(__VA_ARGS__))
#endif

// Logs, breaks into an attached debugger, then abort() so the crash handler writes a dump.
#if defined(_MSC_VER)
#define LUMORA_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define LUMORA_DEBUG_BREAK() __builtin_trap()
#else
#define LUMORA_DEBUG_BREAK() ((void)0)
#endif

#define LUMORA_FATAL(...)                                                          \
    do                                                                             \
    {                                                                              \
        LOG_FATAL(__VA_ARGS__);                                                    \
        LUMORA_DEBUG_BREAK();                                                      \
        ::std::abort();                                                            \
    } while (false)

// At most one line per call site per interval, e.g. LOG_WARN_EVERY(1s, "..."). The first call always logs;
// racing threads may log twice in one interval.
#define LUMORA_LOG_EVERY_IMPL(level, interval, ...)                                    \
    do                                                                                 \
    {                                                                                  \
        static ::std::atomic<::std::chrono::steady_clock::rep> _lumoraLogEveryLast{0}; \
        const auto _lumoraLogEveryNow = ::std::chrono::steady_clock::now();            \
        const auto _lumoraLogEveryPrev =                                               \
            _lumoraLogEveryLast.load(::std::memory_order_relaxed);                     \
        const auto _lumoraLogEveryStamp = _lumoraLogEveryNow.time_since_epoch().count(); \
        if (_lumoraLogEveryPrev == 0                                                   \
            || ::std::chrono::steady_clock::duration{_lumoraLogEveryStamp              \
                                                     - _lumoraLogEveryPrev}            \
                   >= (interval))                                                      \
        {                                                                              \
            _lumoraLogEveryLast.store(_lumoraLogEveryStamp, ::std::memory_order_relaxed); \
            level(__VA_ARGS__);                                                        \
        }                                                                              \
    } while (false)

#define LOG_TRACE_EVERY(interval, ...) LUMORA_LOG_EVERY_IMPL(LOG_TRACE, interval, __VA_ARGS__)
#define LOG_INFO_EVERY(interval, ...)  LUMORA_LOG_EVERY_IMPL(LOG_INFO, interval, __VA_ARGS__)
#define LOG_WARN_EVERY(interval, ...)  LUMORA_LOG_EVERY_IMPL(LOG_WARN, interval, __VA_ARGS__)
#define LOG_ERROR_EVERY(interval, ...) LUMORA_LOG_EVERY_IMPL(LOG_ERROR, interval, __VA_ARGS__)
