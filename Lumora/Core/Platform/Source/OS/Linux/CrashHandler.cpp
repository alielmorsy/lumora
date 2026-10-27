#include <lumora/os/CrashHandler.h>

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <vector>

namespace lumora::os
{
    namespace
    {
        struct State
        {
            std::atomic<bool>   installed{false};
            std::atomic<bool>   writing{false};
            CrashHandlerOptions options;
            std::string         dumpDir;
            char                altStack[64 * 1024];
        };

        State& Get()
        {
            static State state;
            return state;
        }

        const char* SignalName(const int sig)
        {
            switch (sig)
            {
            case SIGSEGV: return "SIGSEGV";
            case SIGABRT: return "SIGABRT";
            case SIGFPE:  return "SIGFPE";
            case SIGILL:  return "SIGILL";
            case SIGBUS:  return "SIGBUS";
            default:      return "UNKNOWN";
            }
        }

        void WriteAll(const int fd, const char* text)
        {
            [[maybe_unused]] const ssize_t n = ::write(fd, text, std::strlen(text));
        }

        /// Opens "<dir>/<app>-<date>-<time>-<pid>.txt"; the path is copied into `pathOut` (caller buffer). -1 on failure.
        int OpenReport(char* pathOut, const std::size_t pathSize)
        {
            State& state = Get();
            if (state.dumpDir.empty()) return -1;

            const std::time_t now = std::time(nullptr);
            std::tm           local{};
            ::localtime_r(&now, &local);

            std::snprintf(pathOut, pathSize, "%s/%s-%04d-%02d-%02d-%02d%02d%02d-%d.txt", state.dumpDir.c_str(),
                          state.options.appName.c_str(), local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                          local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>(::getpid()));
            return ::open(pathOut, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        }

        void WriteReport(const int fd, const char* reason, const char* detail)
        {
            char line[256];
            WriteAll(fd, "Reason: ");
            WriteAll(fd, reason);
            WriteAll(fd, "\n");
            if (detail)
            {
                WriteAll(fd, detail);
                WriteAll(fd, "\n");
            }
            std::snprintf(line, sizeof(line), "Process ID: %d\nThread ID: %ld\n", static_cast<int>(::getpid()),
                          static_cast<long>(::gettid()));
            WriteAll(fd, line);

            void* frames[64];
            const int count = ::backtrace(frames, 64);
            WriteAll(fd, "Backtrace:\n");
            ::backtrace_symbols_fd(frames, count, fd);
        }

        void SignalHandler(const int sig, siginfo_t* info, void* /*ucontext*/)
        {
            State& state = Get();
            if (state.writing.exchange(true)) ::_exit(128 + sig);

            char path[512];
            if (const int fd = OpenReport(path, sizeof(path)); fd >= 0)
            {
                char detail[128];
                std::snprintf(detail, sizeof(detail), "Signal: %d (%s), fault address: %p", sig, SignalName(sig),
                              info ? info->si_addr : nullptr);
                WriteReport(fd, SignalName(sig), detail);
                ::close(fd);
            }

            // SA_RESETHAND restored the default action; re-raise so the exit status and any core dump are intact.
            ::raise(sig);
        }

        void TerminateHandler()
        {
            State& state = Get();
            if (!state.writing.exchange(true))
            {
                char path[512];
                if (const int fd = OpenReport(path, sizeof(path)); fd >= 0)
                {
                    WriteReport(fd, "std::terminate", nullptr);
                    ::close(fd);
                }
            }
            std::abort();
        }

        /// Deletes the oldest "<appName>-*.txt" reports until at most `keep` remain; the timestamped names sort by age.
        void PruneReports(const CrashHandlerOptions& options)
        {
            if (options.keepReports == 0) return;

            std::error_code               ec;
            std::vector<std::filesystem::path> reports;
            for (const auto& entry : std::filesystem::directory_iterator(options.dumpDir, ec))
            {
                const std::string name = entry.path().filename().string();
                if (name.rfind(options.appName + "-", 0) == 0 && entry.path().extension() == ".txt")
                    reports.push_back(entry.path());
            }
            std::ranges::sort(reports);
            while (reports.size() > options.keepReports)
            {
                std::filesystem::remove(reports.front(), ec);
                reports.erase(reports.begin());
            }
        }
    }

    void InstallCrashHandler(CrashHandlerOptions options)
    {
        State& state = Get();
        if (state.installed.exchange(true)) return;

        state.options = std::move(options);
        if (!state.options.dumpDir.empty())
        {
            std::error_code ec;
            std::filesystem::create_directories(state.options.dumpDir, ec);
            state.dumpDir = state.options.dumpDir.string();
            PruneReports(state.options);
        }

        // Pre-load libgcc so backtrace() does not allocate inside the handler.
        void* warm[1];
        ::backtrace(warm, 1);

        stack_t stack{};
        stack.ss_sp   = state.altStack;
        stack.ss_size = sizeof(state.altStack);
        ::sigaltstack(&stack, nullptr);

        struct sigaction sa{};
        sa.sa_flags     = SA_SIGINFO | SA_ONSTACK | SA_NODEFER | SA_RESETHAND;
        sa.sa_sigaction = SignalHandler;
        ::sigemptyset(&sa.sa_mask);
        for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
            ::sigaction(sig, &sa, nullptr);

        std::set_terminate(TerminateHandler);
    }

    std::filesystem::path WriteCrashReport(const std::string_view reason)
    {
        State& state = Get();
        if (!state.installed.load() || state.dumpDir.empty()) return {};

        char path[512];
        const int fd = OpenReport(path, sizeof(path));
        if (fd < 0) return {};

        const std::string text(reason);
        WriteReport(fd, text.c_str(), nullptr);
        ::close(fd);
        return std::filesystem::path(path);
    }
}
