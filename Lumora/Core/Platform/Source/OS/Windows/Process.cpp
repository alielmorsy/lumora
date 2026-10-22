#include <lumora/os/Process.h>

#include <lumora/log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    ProcessMemory CurrentProcessMemory()
    {
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (!::GetProcessMemoryInfo(::GetCurrentProcess(),
                                    reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                    sizeof(counters)))
            return {};

        return ProcessMemory{
            .workingSetBytes     = static_cast<std::uint64_t>(counters.WorkingSetSize),
            .peakWorkingSetBytes = static_cast<std::uint64_t>(counters.PeakWorkingSetSize),
            .privateBytes        = static_cast<std::uint64_t>(counters.PrivateUsage),
        };
    }

    namespace
    {
        std::wstring Widen(std::string_view s)
        {
            if (s.empty()) return {};
            const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<std::size_t>(n), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        // MSVCRT argv quoting: leave clean tokens bare, else wrap in quotes escaping backslash runs that
        // precede a quote (and the closing quote) per the documented parsing rules.
        void AppendQuotedW(std::wstring& cmd, const std::wstring& arg)
        {
            if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
            {
                cmd += arg;
                return;
            }
            cmd += L'"';
            for (auto it = arg.begin();; ++it)
            {
                std::size_t backslashes = 0;
                while (it != arg.end() && *it == L'\\') { ++it; ++backslashes; }
                if (it == arg.end())
                {
                    cmd.append(backslashes * 2, L'\\');
                    break;
                }
                if (*it == L'"')
                {
                    cmd.append(backslashes * 2 + 1, L'\\');
                    cmd += L'"';
                }
                else
                {
                    cmd.append(backslashes, L'\\');
                    cmd += *it;
                }
            }
            cmd += L'"';
        }

        // Builds an ANSI double-null-terminated environment block, or empty to inherit.
        std::vector<char> MakeEnvBuffer(const EnvBlock& env)
        {
            std::vector<char> buf;
            if (env.empty()) return buf;
            for (const std::string& e : env)
            {
                buf.insert(buf.end(), e.begin(), e.end());
                buf.push_back('\0');
            }
            buf.push_back('\0');
            return buf;
        }

        struct Pipe
        {
            HANDLE read  = nullptr;
            HANDLE write = nullptr;

            bool Open()
            {
                SECURITY_ATTRIBUTES sa{};
                sa.nLength        = sizeof(sa);
                sa.bInheritHandle = TRUE;
                if (!::CreatePipe(&read, &write, &sa, 0)) return false;
                ::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0); // the read end must not leak to the child
                return true;
            }
            void CloseWrite() { if (write) { ::CloseHandle(write); write = nullptr; } }
            void CloseRead()  { if (read)  { ::CloseHandle(read);  read  = nullptr; } }
        };

        // Reads whatever is currently buffered on a pipe without blocking. Returns false once the write
        // end is closed so the caller can stop polling it.
        bool DrainAvailable(HANDLE h, std::string& sink)
        {
            for (;;)
            {
                DWORD avail = 0;
                if (!::PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) return false;
                if (avail == 0) return true;
                char  buf[4096];
                DWORD toRead = avail < sizeof(buf) ? avail : static_cast<DWORD>(sizeof(buf));
                DWORD read   = 0;
                if (!::ReadFile(h, buf, toRead, &read, nullptr) || read == 0) return false;
                sink.append(buf, read);
            }
        }

        // Blocking read of a single pipe carrying merged stdout+stderr: capture into `out` and/or deliver
        // completed lines to `onLine`.
        void PumpMerged(HANDLE readPipe, std::string& out, const std::function<void(std::string_view)>& onLine)
        {
            std::string pending;
            char        chunk[4096];
            DWORD       read = 0;
            while (::ReadFile(readPipe, chunk, sizeof(chunk), &read, nullptr) && read > 0)
            {
                out.append(chunk, read);
                if (onLine)
                {
                    pending.append(chunk, read);
                    std::size_t nl;
                    while ((nl = pending.find('\n')) != std::string::npos)
                    {
                        std::string line = pending.substr(0, nl);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        onLine(line);
                        pending.erase(0, nl + 1);
                    }
                }
            }
            if (onLine && !pending.empty())
            {
                if (pending.back() == '\r') pending.pop_back();
                onLine(pending);
            }
        }

        [[nodiscard]] DWORD CreationFlags(const ProcessOptions& opts, const DWORD base)
        {
            return opts.stop.stop_possible() ? base | CREATE_SUSPENDED : base;
        }

        // Created suspended so the child is in the job before it starts anything: killing only the direct child would leave
        // its descendants holding the output pipe open.
        // Must be destroyed before the process handle is closed.
        class Cancellation
        {
        public:
            Cancellation(const std::stop_token& stop, const PROCESS_INFORMATION& pi)
            {
                if (!stop.stop_possible()) return;

                m_job = ::CreateJobObjectW(nullptr, nullptr);
                if (m_job != nullptr && !::AssignProcessToJobObject(m_job, pi.hProcess))
                {
                    ::CloseHandle(m_job);
                    m_job = nullptr;
                }

                m_callback.emplace(stop, [job = m_job, process = pi.hProcess]
                {
                    if (job != nullptr) ::TerminateJobObject(job, 1);
                    else ::TerminateProcess(process, 1);
                });
                ::ResumeThread(pi.hThread);
            }

            ~Cancellation()
            {
                m_callback.reset();
                if (m_job != nullptr) ::CloseHandle(m_job);
            }

            Cancellation(const Cancellation&)            = delete;
            Cancellation& operator=(const Cancellation&) = delete;

        private:
            HANDLE                                                m_job = nullptr;
            std::optional<std::stop_callback<std::function<void()>>> m_callback;
        };

        int WaitAndClose(PROCESS_INFORMATION& pi)
        {
            ::WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            ::GetExitCodeProcess(pi.hProcess, &code);
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
            return static_cast<int>(code);
        }
    }

    ProcessResult Run(const std::filesystem::path& exe, const std::vector<std::string>& args, const ProcessOptions& opts)
    {
        ProcessResult result;

        std::wstring cmd;
        AppendQuotedW(cmd, exe.wstring());
        for (const std::string& a : args)
        {
            cmd += L' ';
            AppendQuotedW(cmd, Widen(a));
        }

        const std::wstring exeW   = exe.wstring();
        const std::wstring wdW    = opts.workingDir.empty() ? std::wstring{} : opts.workingDir.wstring();
        std::vector<char>  envBuf = MakeEnvBuffer(opts.env);

        STARTUPINFOW si{};
        si.cb        = sizeof(si);
        si.dwFlags   = STARTF_USESTDHANDLES;
        si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

        if (opts.mergeStreams)
        {
            Pipe pipe;
            if (!pipe.Open()) { LOG_ERROR("OSCore::Run: CreatePipe failed for {}", exe.string()); return result; }
            si.hStdOutput = pipe.write;
            si.hStdError  = pipe.write;

            std::wstring        cmdMutable = cmd;
            PROCESS_INFORMATION pi{};
            const BOOL          ok = ::CreateProcessW(exeW.c_str(), cmdMutable.data(), nullptr, nullptr, TRUE,
                                                      CreationFlags(opts, CREATE_NO_WINDOW),
                                                      envBuf.empty() ? nullptr : envBuf.data(),
                                                      wdW.empty() ? nullptr : wdW.c_str(), &si, &pi);
            const DWORD lastErr = ok ? 0 : ::GetLastError();
            pipe.CloseWrite();
            if (!ok)
            {
                LOG_ERROR("OSCore::Run: CreateProcess failed ({}) for {}", lastErr, exe.string());
                pipe.CloseRead();
                return result;
            }
            result.launched = true;
            {
                const Cancellation cancel(opts.stop, pi);
                PumpMerged(pipe.read, result.out, opts.onLine);
                ::WaitForSingleObject(pi.hProcess, INFINITE);
            }
            result.exitCode = WaitAndClose(pi);
            pipe.CloseRead();
            return result;
        }

        Pipe outPipe, errPipe;
        if (!outPipe.Open()) { LOG_ERROR("OSCore::Run: CreatePipe failed for {}", exe.string()); return result; }
        if (!errPipe.Open()) { outPipe.CloseRead(); outPipe.CloseWrite(); return result; }
        si.hStdOutput = outPipe.write;
        si.hStdError  = errPipe.write;

        std::wstring        cmdMutable = cmd;
        PROCESS_INFORMATION pi{};
        const BOOL          ok = ::CreateProcessW(exeW.c_str(), cmdMutable.data(), nullptr, nullptr, TRUE,
                                                  CreationFlags(opts, CREATE_NO_WINDOW),
                                                  envBuf.empty() ? nullptr : envBuf.data(),
                                                  wdW.empty() ? nullptr : wdW.c_str(), &si, &pi);
        const DWORD lastErr = ok ? 0 : ::GetLastError();
        outPipe.CloseWrite();
        errPipe.CloseWrite();
        if (!ok)
        {
            LOG_ERROR("OSCore::Run: CreateProcess failed ({}) for {}", lastErr, exe.string());
            outPipe.CloseRead();
            errPipe.CloseRead();
            return result;
        }
        result.launched = true;

        {
            const Cancellation cancel(opts.stop, pi);
            bool               outOpen = true, errOpen = true;
            for (;;)
            {
                if (outOpen && !DrainAvailable(outPipe.read, result.out)) outOpen = false;
                if (errOpen && !DrainAvailable(errPipe.read, result.err)) errOpen = false;
                if (::WaitForSingleObject(pi.hProcess, 5) == WAIT_OBJECT_0)
                {
                    DrainAvailable(outPipe.read, result.out);
                    DrainAvailable(errPipe.read, result.err);
                    break;
                }
            }
        }
        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);
        result.exitCode = static_cast<int>(code);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);
        outPipe.CloseRead();
        errPipe.CloseRead();
        return result;
    }

    ProcessResult RunCommandLine(std::string_view commandLine, const ProcessOptions& opts)
    {
        ProcessResult result;

        Pipe pipe;
        if (!pipe.Open()) { LOG_ERROR("OSCore::RunCommandLine: CreatePipe failed"); return result; }

        STARTUPINFOA si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = pipe.write;
        si.hStdError  = pipe.write;
        si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

        std::vector<char> cmd(commandLine.begin(), commandLine.end());
        cmd.push_back('\0');
        std::vector<char> envBuf = MakeEnvBuffer(opts.env);
        const std::string wd     = opts.workingDir.empty() ? std::string{} : opts.workingDir.string();

        PROCESS_INFORMATION pi{};
        const BOOL          ok = ::CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CreationFlags(opts, 0),
                                                  envBuf.empty() ? nullptr : envBuf.data(),
                                                  wd.empty() ? nullptr : wd.c_str(), &si, &pi);
        const DWORD lastErr = ok ? 0 : ::GetLastError();
        pipe.CloseWrite();
        if (!ok)
        {
            LOG_ERROR("OSCore::RunCommandLine: CreateProcess failed ({})", lastErr);
            pipe.CloseRead();
            return result;
        }
        result.launched = true;
        {
            const Cancellation cancel(opts.stop, pi);
            PumpMerged(pipe.read, result.out, opts.onLine);
            ::WaitForSingleObject(pi.hProcess, INFINITE);
        }
        result.exitCode = WaitAndClose(pi);
        pipe.CloseRead();
        return result;
    }

    std::string QuoteArg(std::string_view arg)
    {
        if (!arg.empty() && arg.find_first_of(" \t\"") == std::string_view::npos) return std::string(arg);
        std::string out = "\"";
        std::size_t backslashes = 0;
        for (const char c : arg)
        {
            if (c == '\\') { ++backslashes; continue; }
            if (c == '"')
            {
                out.append(backslashes * 2 + 1, '\\');
                out += '"';
                backslashes = 0;
                continue;
            }
            out.append(backslashes, '\\');
            backslashes = 0;
            out += c;
        }
        out.append(backslashes * 2, '\\');
        out += '"';
        return out;
    }
}
