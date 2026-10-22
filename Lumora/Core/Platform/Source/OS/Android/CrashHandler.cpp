#include <lumora/os/CrashHandler.h>

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <unistd.h>

namespace lumora::os
{
    namespace
    {
        struct State
        {
            std::atomic<bool>   installed{false};
            std::atomic<bool>   writing{false};
            CrashHandlerOptions options;
            struct sigaction    oldSegv{};
            struct sigaction    oldAbrt{};
            struct sigaction    oldFpe{};
            struct sigaction    oldIll{};
            struct sigaction    oldBus{};
        };

        State& Get()
        {
            static State state;
            return state;
        }

        void SignalHandler(int sig, siginfo_t* info, void* /*ucontext*/)
        {
            State& state = Get();
            if (state.writing.exchange(true))
            {
                ::_exit(128 + sig);
            }

            const char* sigName = "UNKNOWN";
            switch (sig)
            {
                case SIGSEGV: sigName = "SIGSEGV"; break;
                case SIGABRT: sigName = "SIGABRT"; break;
                case SIGFPE:  sigName = "SIGFPE";  break;
                case SIGILL:  sigName = "SIGILL";  break;
                case SIGBUS:  sigName = "SIGBUS";  break;
                default: break;
            }

            __android_log_print(ANDROID_LOG_FATAL, "Lumora",
                                "FATAL: Process crashed with signal %d (%s), fault addr: %p",
                                sig, sigName, info ? info->si_addr : nullptr);

            struct sigaction sa{};
            sa.sa_handler = SIG_DFL;
            ::sigemptyset(&sa.sa_mask);
            ::sigaction(sig, &sa, nullptr);
            ::raise(sig);
        }
    }

    void InstallCrashHandler(CrashHandlerOptions options)
    {
        State& state = Get();
        if (state.installed.exchange(true)) return;
        state.options = std::move(options);

        struct sigaction sa{};
        sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_RESETHAND;
        sa.sa_sigaction = SignalHandler;
        ::sigemptyset(&sa.sa_mask);

        ::sigaction(SIGSEGV, &sa, &state.oldSegv);
        ::sigaction(SIGABRT, &sa, &state.oldAbrt);
        ::sigaction(SIGFPE,  &sa, &state.oldFpe);
        ::sigaction(SIGILL,  &sa, &state.oldIll);
        ::sigaction(SIGBUS,  &sa, &state.oldBus);
    }

    std::filesystem::path WriteCrashReport(std::string_view reason)
    {
        State& state = Get();
        __android_log_print(ANDROID_LOG_ERROR, "Lumora", "CrashReport: %.*s",
                            static_cast<int>(reason.size()), reason.data());

        if (state.options.dumpDir.empty())
        {
            return {};
        }

        std::error_code ec;
        std::filesystem::create_directories(state.options.dumpDir, ec);

        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);
        std::tm tmVal{};
        ::localtime_r(&timeT, &tmVal);

        char filename[128];
        std::snprintf(filename, sizeof(filename), "%s-%04d-%02d-%02d-%02d%02d%02d-%d.txt",
                      state.options.appName.c_str(),
                      tmVal.tm_year + 1900, tmVal.tm_mon + 1, tmVal.tm_mday,
                      tmVal.tm_hour, tmVal.tm_min, tmVal.tm_sec,
                      static_cast<int>(::getpid()));

        std::filesystem::path reportPath = state.options.dumpDir / filename;
        std::ofstream out(reportPath);
        if (out)
        {
            out << "Reason: " << reason << "\n";
            out << "Process ID: " << ::getpid() << "\n";
            return reportPath;
        }
        return {};
    }
}
