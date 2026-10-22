#include "lumora/log/Log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace lumora::log
{
    namespace
    {
        const char* LevelStr(Level level) noexcept
        {
            switch (level)
            {
            case Level::Trace: return "TRACE";
            case Level::Info: return "INFO ";
            case Level::Warn: return "WARN ";
            case Level::Error: return "ERROR";
            case Level::Fatal: return "FATAL";
            }
            return "?";
        }

        std::mutex& StderrMutex() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }

        void DefaultSink(Level level, const char* category, const char* message) noexcept
        {
            const std::lock_guard lock(StderrMutex());
            std::fprintf(stderr, "[%s] [%s] %s\n", LevelStr(level), category, message);
        }

        std::atomic<SinkFn> g_sink{&DefaultSink};

        std::atomic<Level> g_minLevel{Level::Trace};

        struct ExtraSink
        {
            SinkHandle id;
            SinkCallback fn;
        };

        std::mutex& ExtraSinksMutex() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }

        std::vector<ExtraSink>& ExtraSinks() noexcept
        {
            static std::vector<ExtraSink> sinks;
            return sinks;
        }

        std::atomic<SinkHandle> g_nextSinkHandle{1};

        std::mutex& RegistryMutex() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, Ref<Logger>>& Registry() noexcept
        {
            static std::unordered_map<std::string, Ref<Logger>> registry;
            return registry;
        }
    }

    void SetMinLevel(Level level) noexcept
    {
        g_minLevel.store(level, std::memory_order_relaxed);
    }

    Level MinLevel() noexcept
    {
        return g_minLevel.load(std::memory_order_relaxed);
    }

    void Logger::Dispatch(Level level, const std::string& message) noexcept
    {
        const SinkFn sink = g_sink.load(std::memory_order_acquire);
        if (sink) sink(level, _name.c_str(), message.c_str());

        const std::lock_guard lock(ExtraSinksMutex());
        for (const ExtraSink& extra : ExtraSinks())
        {
            try
            {
                extra.fn(level, _name.c_str(), message.c_str());
            }
            catch (...)
            {
                // An observer sink must never take the logging thread down.
            }
        }
    }

    Ref<Logger> GetLogger(std::string_view name)
    {
        const std::lock_guard lock(RegistryMutex());

        auto& registry = Registry();
        const auto it = registry.find(std::string(name));
        if (it != registry.end()) return it->second;

        Ref<Logger> logger = MakeRef<Logger>(std::string(name));
        registry.emplace(std::string(name), logger);
        return logger;
    }

    void SetSink(SinkFn sink) noexcept
    {
        g_sink.store(sink ? sink : &DefaultSink, std::memory_order_release);
    }

    SinkHandle AddSink(SinkCallback sink)
    {
        const SinkHandle id = g_nextSinkHandle.fetch_add(1, std::memory_order_relaxed);
        const std::lock_guard lock(ExtraSinksMutex());
        ExtraSinks().push_back(ExtraSink{id, std::move(sink)});
        return id;
    }

    void RemoveSink(SinkHandle handle) noexcept
    {
        const std::lock_guard lock(ExtraSinksMutex());
        std::vector<ExtraSink>& sinks = ExtraSinks();
        sinks.erase(std::remove_if(sinks.begin(), sinks.end(),
                                   [handle](const ExtraSink& s) { return s.id == handle; }),
                    sinks.end());
    }
}
