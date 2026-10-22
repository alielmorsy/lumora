#include <lumora/os/Process.h>
#include <lumora/log/Log.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <vector>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    ProcessMemory CurrentProcessMemory()
    {
        ProcessMemory mem{};
#if defined(__linux__) || defined(__ANDROID__)
        std::ifstream statm("/proc/self/statm");
        if (statm)
        {
            uint64_t sizePages = 0;
            uint64_t residentPages = 0;
            if (statm >> sizePages >> residentPages)
            {
                const long pageSize = ::sysconf(_SC_PAGESIZE);
                if (pageSize > 0)
                {
                    mem.workingSetBytes = residentPages * static_cast<uint64_t>(pageSize);
                    mem.privateBytes = sizePages * static_cast<uint64_t>(pageSize);
                }
            }
        }

        struct rusage usage{};
        if (::getrusage(RUSAGE_SELF, &usage) == 0)
        {
            mem.peakWorkingSetBytes = static_cast<uint64_t>(usage.ru_maxrss) * 1024ULL;
        }
#endif
        return mem;
    }

    std::string QuoteArg(std::string_view arg)
    {
        if (arg.empty()) return "''";
        bool needsQuotes = false;
        for (const char c : arg)
        {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\'' || c == '\"' || c == '\\' ||
                c == '$' || c == '`' || c == '&' || c == '|' || c == ';' || c == '<' || c == '>' ||
                c == '(' || c == ')' || c == '*' || c == '?' || c == '[' || c == ']' || c == '#' ||
                c == '~' || c == '=' || c == '!')
            {
                needsQuotes = true;
                break;
            }
        }
        if (!needsQuotes) return std::string(arg);

        std::string result = "'";
        for (const char c : arg)
        {
            if (c == '\'')
            {
                result += "'\\''";
            }
            else
            {
                result.push_back(c);
            }
        }
        result.push_back('\'');
        return result;
    }

    ProcessResult Run(const std::filesystem::path& exe, const std::vector<std::string>& args,
                      const ProcessOptions& opts)
    {
        ProcessResult result{};

        int outPipe[2]{-1, -1};
        int errPipe[2]{-1, -1};

        if (::pipe(outPipe) != 0)
        {
            LOG_ERROR("Process::Run: pipe() failed");
            return result;
        }

        if (!opts.mergeStreams)
        {
            if (::pipe(errPipe) != 0)
            {
                LOG_ERROR("Process::Run: pipe() for stderr failed");
                ::close(outPipe[0]);
                ::close(outPipe[1]);
                return result;
            }
        }

        const pid_t pid = ::fork();
        if (pid < 0)
        {
            LOG_ERROR("Process::Run: fork() failed");
            ::close(outPipe[0]);
            ::close(outPipe[1]);
            if (!opts.mergeStreams)
            {
                ::close(errPipe[0]);
                ::close(errPipe[1]);
            }
            return result;
        }

        if (pid == 0)
        {
            ::setpgid(0, 0);

            if (!opts.workingDir.empty())
            {
                if (::chdir(opts.workingDir.c_str()) != 0)
                {
                    ::_exit(127);
                }
            }

            ::close(outPipe[0]);
            ::dup2(outPipe[1], STDOUT_FILENO);
            ::close(outPipe[1]);

            if (opts.mergeStreams)
            {
                ::dup2(STDOUT_FILENO, STDERR_FILENO);
            }
            else
            {
                ::close(errPipe[0]);
                ::dup2(errPipe[1], STDERR_FILENO);
                ::close(errPipe[1]);
            }

            std::vector<char*> argvList;
            std::string exeStr = exe.string();
            argvList.push_back(const_cast<char*>(exeStr.c_str()));
            for (const auto& a : args)
            {
                argvList.push_back(const_cast<char*>(a.c_str()));
            }
            argvList.push_back(nullptr);

            if (!opts.env.empty())
            {
                std::vector<char*> envpList;
                for (const auto& e : opts.env)
                {
                    envpList.push_back(const_cast<char*>(e.c_str()));
                }
                envpList.push_back(nullptr);
                ::execve(exe.c_str(), argvList.data(), envpList.data());
            }
            else
            {
                ::execv(exe.c_str(), argvList.data());
            }

            ::_exit(127);
        }

        ::close(outPipe[1]);
        if (!opts.mergeStreams)
        {
            ::close(errPipe[1]);
        }

        std::stop_callback stopCb(opts.stop, [pid] {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
        });

        if (opts.stop.stop_requested())
        {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
        }

        std::string lineBuf;
        auto feedLine = [&](std::string_view chunk) {
            if (!opts.onLine) return;
            lineBuf.append(chunk);
            for (;;)
            {
                const auto nl = lineBuf.find('\n');
                if (nl == std::string::npos) break;
                std::string_view line(lineBuf.data(), nl);
                if (!line.empty() && line.back() == '\r')
                {
                    line.remove_suffix(1);
                }
                opts.onLine(line);
                lineBuf.erase(0, nl + 1);
            }
        };

        std::array<char, 4096> buffer{};
        std::vector<pollfd> fds;
        if (outPipe[0] >= 0) fds.push_back({.fd = outPipe[0], .events = POLLIN, .revents = 0});
        if (!opts.mergeStreams && errPipe[0] >= 0) fds.push_back({.fd = errPipe[0], .events = POLLIN, .revents = 0});

        while (!fds.empty())
        {
            if (opts.stop.stop_requested())
            {
                ::kill(-pid, SIGKILL);
                ::kill(pid, SIGKILL);
            }

            const int pr = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), 100);
            if (pr < 0)
            {
                if (errno == EINTR) continue;
                break;
            }

            for (auto it = fds.begin(); it != fds.end();)
            {
                if (it->revents & (POLLIN | POLLHUP | POLLERR))
                {
                    const ssize_t bytes = ::read(it->fd, buffer.data(), buffer.size());
                    if (bytes > 0)
                    {
                        const std::string_view chunk(buffer.data(), static_cast<size_t>(bytes));
                        if (it->fd == outPipe[0])
                        {
                            result.out.append(chunk);
                            feedLine(chunk);
                        }
                        else
                        {
                            result.err.append(chunk);
                        }
                        ++it;
                    }
                    else
                    {
                        ::close(it->fd);
                        it = fds.erase(it);
                    }
                }
                else
                {
                    ++it;
                }
            }
        }

        if (!lineBuf.empty() && opts.onLine)
        {
            std::string_view line(lineBuf);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            opts.onLine(line);
            lineBuf.clear();
        }

        int status = 0;
        ::waitpid(pid, &status, 0);

        result.launched = true;
        if (opts.stop.stop_requested())
        {
            result.exitCode = 1;
        }
        else if (WIFEXITED(status))
        {
            result.exitCode = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status))
        {
            result.exitCode = 128 + WTERMSIG(status);
        }
        else
        {
            result.exitCode = -1;
        }

        return result;
    }

    ProcessResult RunCommandLine(std::string_view commandLine, const ProcessOptions& opts)
    {
        ProcessOptions copyOpts = opts;
        copyOpts.mergeStreams = true;
        const char* sh = (::access("/bin/sh", X_OK) == 0) ? "/bin/sh" : "/system/bin/sh";
        return Run(sh, {"-c", std::string(commandLine)}, copyOpts);
    }
}
