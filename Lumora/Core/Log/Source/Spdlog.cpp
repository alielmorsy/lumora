#include "lumora/log/Spdlog.h"
#include "lumora/log/Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#if defined(NDEBUG)
#    include <spdlog/sinks/basic_file_sink.h>
#    include <cstdio>
#    include <cstdlib>
#    include <filesystem>
#    include <vector>

#    if defined(_WIN32)
#        ifndef WIN32_LEAN_AND_MEAN
#            define WIN32_LEAN_AND_MEAN
#        endif
#        ifndef NOMINMAX
#            define NOMINMAX
#        endif
#        include <windows.h>
#        if defined(ERROR)
#            undef ERROR
#        endif
#    elif defined(__linux__)
#        include <unistd.h>
#    elif defined(__APPLE__)
#        include <mach-o/dyld.h>
#    endif
#endif

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace lumora::log
{
    namespace
    {
        spdlog::level::level_enum ToSpdlog(Level level) noexcept
        {
            switch (level)
            {
                case Level::Trace: return spdlog::level::trace;
                case Level::Info:  return spdlog::level::info;
                case Level::Warn:  return spdlog::level::warn;
                case Level::Error: return spdlog::level::err;
                case Level::Fatal: return spdlog::level::critical;
            }
            return spdlog::level::info;
        }

        std::shared_ptr<spdlog::sinks::sink>& ConsoleSink()
        {
            static std::shared_ptr<spdlog::sinks::sink> sink = []
            {
                auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
                console->set_pattern("[%H:%M:%S.%e] [%^%l%$] [%n] %v");
                return console;
            }();
            return sink;
        }

#if defined(NDEBUG)
        spdlog::filename_t GetLogFilename()
        {
            std::filesystem::path exePath;
#    if defined(_WIN32)
            std::wstring buf(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
                if (n == 0) break;
                if (n < buf.size())
                {
                    buf.resize(n);
                    exePath = buf;
                    break;
                }
                buf.resize(buf.size() * 2);
            }
#    elif defined(__linux__)
            char buf[1024];
            const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
            if (n > 0)
            {
                buf[n] = '\0';
                exePath = buf;
            }
#    elif defined(__APPLE__)
            char buf[1024];
            uint32_t size = sizeof(buf);
            if (::_NSGetExecutablePath(buf, &size) == 0)
            {
                exePath = buf;
            }
#    endif

            auto stem = exePath.stem();
            if (stem.empty())
            {
                stem = "Lumora";
            }
#    if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
            return stem.wstring() + L".log";
#    else
            return stem.string() + ".log";
#    endif
        }

        std::shared_ptr<spdlog::sinks::sink> CreateFileSink() noexcept
        {
            try
            {
                const spdlog::filename_t filename = GetLogFilename();
                auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
                fileSink->set_pattern("[%H:%M:%S.%e] [%l] [%n] %v");
                return fileSink;
            }
            catch (const std::exception& e)
            {
                std::fprintf(stderr, "[WARN ] [Log] Failed to open release log file: %s\n", e.what());
                return nullptr;
            }
            catch (...)
            {
                std::fprintf(stderr, "[WARN ] [Log] Failed to open release log file: unknown error\n");
                return nullptr;
            }
        }

        std::shared_ptr<spdlog::sinks::sink>& FileSink() noexcept
        {
            static std::shared_ptr<spdlog::sinks::sink> sink = CreateFileSink();
            return sink;
        }

        const std::vector<std::shared_ptr<spdlog::sinks::sink>>& ReleaseSinks()
        {
            static const std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks = []
            {
                std::vector<std::shared_ptr<spdlog::sinks::sink>> list;
                list.push_back(ConsoleSink());
                if (auto file = FileSink())
                {
                    list.push_back(std::move(file));
                }
                return list;
            }();
            return sinks;
        }
#endif

        std::mutex& LoggersMutex() noexcept
        {
            static std::mutex mutex;
            return mutex;
        }

        std::unordered_map<std::string, std::shared_ptr<spdlog::logger>>& Loggers() noexcept
        {
            static std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers;
            return loggers;
        }

        spdlog::logger& LoggerFor(const char* category)
        {
            const std::lock_guard lock(LoggersMutex());

            auto&             loggers = Loggers();
            const std::string key     = category ? category : "";
            const auto        it      = loggers.find(key);
            if (it != loggers.end()) return *it->second;

#if defined(NDEBUG)
            const auto& sinks  = ReleaseSinks();
            auto        logger = std::make_shared<spdlog::logger>(key, sinks.begin(), sinks.end());
            logger->set_level(spdlog::level::trace);
            logger->flush_on(spdlog::level::trace);
#else
            auto logger = std::make_shared<spdlog::logger>(key, ConsoleSink());
            logger->set_level(spdlog::level::trace);
#endif
            loggers.emplace(key, logger);
            return *logger;
        }

        void SpdlogDispatch(Level level, const char* category, const char* message) noexcept
        {
            // The message is already fully rendered by lumora::log::Logger, so log it literally
            // ("{}") to avoid spdlog re-parsing braces that came from user data.
            LoggerFor(category).log(ToSpdlog(level), "{}", message ? message : "");
        }
    }

    void InstallSpdlog()
    {
        SetSink(&SpdlogDispatch);
    }
}
