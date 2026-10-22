#include <lumora/os/CrashHandler.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <dbghelp.h>

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <cstring>
#include <exception>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

namespace lumora::os
{
    namespace
    {
        // Deliberately no std::string/filesystem/logging at fault time: the faulting thread may hold the CRT heap or logger
        // lock. Uses fixed buffers and raw Win32.
        struct State
        {
            std::atomic<bool> installed{false};
            std::atomic<bool> writing{false};   // one report per process; a fault inside the handler gives up
            wchar_t           dir[MAX_PATH]{};
            wchar_t           name[64]{};
            std::size_t       keep = 20;

            /// Resolved at install time, never at fault time (reading the environment could block on the CRT heap lock).
            MINIDUMP_TYPE dumpType = MiniDumpNormal;
        };

        State& Get()
        {
            static State state;
            return state;
        }

        void FormatStamp(wchar_t* out, std::size_t cap)
        {
            SYSTEMTIME now{};
            ::GetLocalTime(&now);
            ::swprintf(out, cap, L"%04u-%02u-%02u-%02u%02u%02u-%lu", now.wYear, now.wMonth, now.wDay, now.wHour,
                       now.wMinute, now.wSecond, ::GetCurrentProcessId());
        }

        /// Oldest-first prune, so a machine that crashes in a loop does not fill the disk with dumps.
        void PruneOldReports()
        {
            const State& state = Get();
            if (state.keep == 0) return;

            wchar_t pattern[MAX_PATH]{};
            ::swprintf(pattern, MAX_PATH, L"%s\\%s-*.dmp", state.dir, state.name);

            struct Entry
            {
                FILETIME time{};
                wchar_t  name[MAX_PATH]{};
            };
            std::vector<Entry> found;

            WIN32_FIND_DATAW data{};
            const HANDLE     search = ::FindFirstFileW(pattern, &data);
            if (search == INVALID_HANDLE_VALUE) return;
            do
            {
                Entry entry;
                entry.time = data.ftLastWriteTime;
                ::wcsncpy_s(entry.name, data.cFileName, MAX_PATH - 1);
                found.push_back(entry);
            } while (::FindNextFileW(search, &data));
            ::FindClose(search);

            if (found.size() <= state.keep) return;

            std::sort(found.begin(), found.end(), [](const Entry& a, const Entry& b)
                      { return ::CompareFileTime(&a.time, &b.time) < 0; });

            for (std::size_t i = 0; i + state.keep < found.size(); ++i)
            {
                wchar_t path[MAX_PATH]{};
                ::swprintf(path, MAX_PATH, L"%s\\%s", state.dir, found[i].name);
                ::DeleteFileW(path);

                wchar_t sidecar[MAX_PATH]{};
                ::wcsncpy_s(sidecar, path, MAX_PATH - 1);
                if (wchar_t* dot = ::wcsrchr(sidecar, L'.'); dot != nullptr)
                {
                    ::wcsncpy_s(dot, MAX_PATH - (dot - sidecar), L".txt", 4);
                    ::DeleteFileW(sidecar);
                }
            }
        }

        void Append(HANDLE file, const char* text, int length)
        {
            if (file == INVALID_HANDLE_VALUE || text == nullptr || length <= 0) return;
            DWORD written = 0;
            ::WriteFile(file, text, static_cast<DWORD>(length), &written, nullptr);
        }

        /// Faulting stack as `module!function + offset  file:line`, written into the report so the .txt is self-sufficient.
        /// Runs AFTER the dump is written (dbghelp allocates and takes the loader lock, so it may hang); walks a COPY of the
        /// context because StackWalk64 mutates it.
        void WriteSymbolizedStack(HANDLE file, const EXCEPTION_POINTERS* pointers)
        {
            if (pointers == nullptr || pointers->ContextRecord == nullptr) return;

            const HANDLE process = ::GetCurrentProcess();

            // Search next to the executable: that is where this build's PDBs are.
            wchar_t exe[MAX_PATH]{};
            ::GetModuleFileNameW(nullptr, exe, MAX_PATH);
            if (wchar_t* slash = ::wcsrchr(exe, L'\\'); slash != nullptr) *slash = L'\0';

            char search[MAX_PATH]{};
            ::WideCharToMultiByte(CP_UTF8, 0, exe, -1, search, MAX_PATH, nullptr, nullptr);

            ::SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
            if (!::SymInitialize(process, search, TRUE)) return;

            CONTEXT context = *pointers->ContextRecord;

            STACKFRAME64 frame{};
            frame.AddrPC.Offset    = context.Rip;
            frame.AddrPC.Mode      = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrFrame.Mode   = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrStack.Mode   = AddrModeFlat;

            Append(file, "\r\nstack (faulting thread):\r\n", 28);

            alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto* symbol       = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen   = MAX_SYM_NAME;

            for (int depth = 0; depth < 64; ++depth)
            {
                if (!::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, ::GetCurrentThread(), &frame, &context,
                                   nullptr, &::SymFunctionTableAccess64, &::SymGetModuleBase64, nullptr))
                    break;
                if (frame.AddrPC.Offset == 0) break;

                char line[1024]{};
                int  at = ::_snprintf_s(line, sizeof(line), _TRUNCATE, "  #%02d 0x%016llX", depth,
                                        static_cast<unsigned long long>(frame.AddrPC.Offset));

                DWORD64 displacement = 0;
                if (::SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
                    at += ::_snprintf_s(line + at, sizeof(line) - at, _TRUNCATE, "  %s + 0x%llX", symbol->Name,
                                        static_cast<unsigned long long>(displacement));

                IMAGEHLP_LINE64 source{};
                source.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                DWORD column        = 0;
                if (::SymGetLineFromAddr64(process, frame.AddrPC.Offset, &column, &source)
                    && source.FileName != nullptr)
                    at += ::_snprintf_s(line + at, sizeof(line) - at, _TRUNCATE, "\r\n       %s:%lu",
                                        source.FileName, source.LineNumber);

                at += ::_snprintf_s(line + at, sizeof(line) - at, _TRUNCATE, "\r\n");
                Append(file, line, at);

                // A fiber stack (boost.context) has no unwind info past its entry, so the walk stops here.
                if (frame.AddrReturn.Offset == 0) break;
            }

            ::SymCleanup(process);
        }

        void WriteSidecar(const wchar_t* path, const char* reason, const EXCEPTION_POINTERS* pointers)
        {
            const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return;

            char  text[2048]{};
            int   at = ::_snprintf_s(text, sizeof(text), _TRUNCATE,
                                     "reason: %s\r\nprocess: %lu\r\nthread: %lu\r\n", reason,
                                     ::GetCurrentProcessId(), ::GetCurrentThreadId());

            if (pointers != nullptr && pointers->ExceptionRecord != nullptr)
            {
                const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
                at += ::_snprintf_s(text + at, sizeof(text) - at, _TRUNCATE,
                                    "code: 0x%08lX\r\naddress: 0x%p\r\n",
                                    record.ExceptionCode, record.ExceptionAddress);

                // The faulting module is the most useful line: it says which binary crashed.
                HMODULE module = nullptr;
                if (::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                             | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                         static_cast<LPCWSTR>(record.ExceptionAddress), &module)
                    && module != nullptr)
                {
                    wchar_t wide[MAX_PATH]{};
                    ::GetModuleFileNameW(module, wide, MAX_PATH);

                    char narrow[MAX_PATH]{};
                    ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, MAX_PATH, nullptr, nullptr);
                    at += ::_snprintf_s(text + at, sizeof(text) - at, _TRUNCATE, "module: %s\r\n", narrow);
                }

                if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2)
                    at += ::_snprintf_s(text + at, sizeof(text) - at, _TRUNCATE,
                                        "access violation: %s at 0x%llX\r\n",
                                        record.ExceptionInformation[0] == 0 ? "read" : "write",
                                        static_cast<unsigned long long>(record.ExceptionInformation[1]));
            }

            Append(file, text, at);
            WriteSymbolizedStack(file, pointers);
            ::CloseHandle(file);
        }

        /// What the dump captures, decided once at install. The narrow set covers only memory one hop from the faulting
        /// stack, which often misses the object that explains the crash.
        /// MiniDumpWithPrivateReadWriteMemory adds the whole private heap at the cost of file size, so it is a debug default
        /// and opt-in elsewhere.
        [[nodiscard]] MINIDUMP_TYPE ResolveDumpType()
        {
            const MINIDUMP_TYPE narrow = static_cast<MINIDUMP_TYPE>(
                MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo
                | MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithDataSegs);

            // IgnoreInaccessibleMemory too: with the heap in scope one unreadable page would otherwise fail the whole write.
            const MINIDUMP_TYPE wide = static_cast<MINIDUMP_TYPE>(
                narrow | MiniDumpWithPrivateReadWriteMemory | MiniDumpIgnoreInaccessibleMemory);

#if defined(NDEBUG)
            bool heap = false;
#else
            bool heap = true;
#endif

            // Fixed buffer rather than os::GetEnv, to keep the whole file to one rule (no allocation).
            wchar_t        value[8]{};
            const DWORD    got = ::GetEnvironmentVariableW(L"LUMORA_CRASH_HEAP", value, 8);
            if (got > 0 && got < 8) heap = value[0] != L'0';

            return heap ? wide : narrow;
        }

        /// The dump itself. Type comes from ResolveDumpType, cached at install.
        bool WriteDump(const wchar_t* path, EXCEPTION_POINTERS* pointers)
        {
            const HANDLE file = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                              FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) return false;

            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId          = ::GetCurrentThreadId();
            info.ExceptionPointers = pointers;
            info.ClientPointers    = FALSE;

            const MINIDUMP_TYPE type = Get().dumpType;

            const BOOL ok = ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
                                                pointers != nullptr ? &info : nullptr, nullptr, nullptr);
            ::CloseHandle(file);
            return ok == TRUE;
        }

        /// Returns the .dmp path in `dumpOut` (may be null).
        bool Report(const char* reason, EXCEPTION_POINTERS* pointers, wchar_t* dumpOut, std::size_t dumpCap)
        {
            State& state = Get();
            if (!state.installed.load(std::memory_order_acquire)) return false;

            // One report only. A second fault (commonly inside the unwind of the first) must not recurse.
            bool expected = false;
            if (!state.writing.compare_exchange_strong(expected, true)) return false;

            wchar_t stamp[64]{};
            FormatStamp(stamp, 64);

            wchar_t dump[MAX_PATH]{};
            ::swprintf(dump, MAX_PATH, L"%s\\%s-%s.dmp", state.dir, state.name, stamp);
            wchar_t sidecar[MAX_PATH]{};
            ::swprintf(sidecar, MAX_PATH, L"%s\\%s-%s.txt", state.dir, state.name, stamp);

            const bool ok = WriteDump(dump, pointers);
            WriteSidecar(sidecar, reason, pointers);
            PruneOldReports();

            if (dumpOut != nullptr && dumpCap > 0) ::wcsncpy_s(dumpOut, dumpCap, dump, dumpCap - 1);

            // stderr as well as the files: a console run then shows the path without hunting for it.
            char narrow[MAX_PATH]{};
            ::WideCharToMultiByte(CP_UTF8, 0, dump, -1, narrow, MAX_PATH, nullptr, nullptr);
            ::fprintf(stderr, "\n[crash] %s\n[crash] report: %s\n", reason, narrow);
            ::fflush(stderr);

            // Latch stays set: one report per process; the vectored handler and the unhandled filter both see the same fault.
            return ok;
        }

        LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* pointers)
        {
            Report("unhandled exception", pointers, nullptr, 0);
            return EXCEPTION_EXECUTE_HANDLER;   // let the process die; the report is already on disk
        }

        /// Only codes that mean this thread is already broken; C++ throws, breakpoints and guard-page hits are normal traffic.
        bool IsFatalCode(DWORD code) noexcept
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION:
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
                case EXCEPTION_DATATYPE_MISALIGNMENT:
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:
                case EXCEPTION_ILLEGAL_INSTRUCTION:
                case EXCEPTION_IN_PAGE_ERROR:
                case EXCEPTION_INT_DIVIDE_BY_ZERO:
                case EXCEPTION_PRIV_INSTRUCTION:
                case EXCEPTION_STACK_OVERFLOW:
                case STATUS_HEAP_CORRUPTION:
                    return true;
                default:
                    return false;
            }
        }

        /// First-chance, before SEH dispatch. Fires for faults on boost.context fiber stacks, which have no SEH chain and
        /// never reach the unhandled-exception filter.
        /// Always returns CONTINUE_SEARCH: it only observes.
        LONG WINAPI OnVectoredException(EXCEPTION_POINTERS* pointers)
        {
            if (pointers == nullptr || pointers->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
            if (!IsFatalCode(pointers->ExceptionRecord->ExceptionCode)) return EXCEPTION_CONTINUE_SEARCH;

            Report("hardware fault (first chance)", pointers, nullptr, 0);
            return EXCEPTION_CONTINUE_SEARCH;
        }

        /// Builds a synthetic EXCEPTION_POINTERS from the calling thread's context for non-SEH paths (abort, terminate,
        /// purecall, invalid-parameter, CRT assert), so the report still has a stack.
        struct CapturedContext
        {
            CONTEXT           context{};
            EXCEPTION_RECORD  record{};
            EXCEPTION_POINTERS pointers{};
        };

        void CaptureCurrentContext(CapturedContext& out)
        {
            ::RtlCaptureContext(&out.context);
            out.record.ExceptionAddress = reinterpret_cast<void*>(out.context.Rip);
            out.pointers.ExceptionRecord = &out.record;
            out.pointers.ContextRecord   = &out.context;
        }

        /// abort() -- which is where a failed assert and a debug-CRT bounds check end up.
        void OnAbort(int)
        {
            CapturedContext captured;
            CaptureCurrentContext(captured);
            Report("abort (a failed assert, or a CRT check)", &captured.pointers, nullptr, 0);
            ::_exit(3);
        }

        void OnTerminate()
        {
            CapturedContext captured;
            CaptureCurrentContext(captured);
            Report("std::terminate (an exception escaped, or a noexcept function threw)", &captured.pointers,
                   nullptr, 0);
            ::_exit(3);
        }

        void OnPureCall()
        {
            CapturedContext captured;
            CaptureCurrentContext(captured);
            Report("pure virtual call (an object used during its own construction or destruction)",
                   &captured.pointers, nullptr, 0);
            ::_exit(3);
        }

        /// The release CRT's route for "vector subscript out of range" and friends: _STL_VERIFY calls
        /// _invalid_parameter_noinfo_noreturn, which lands here.
        void OnInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, uintptr_t)
        {
            CapturedContext captured;
            CaptureCurrentContext(captured);
            Report("invalid CRT parameter (an STL bounds/iterator check -- e.g. vector subscript out of range)",
                   &captured.pointers, nullptr, 0);
            ::_exit(3);
        }

        /// Debug-CRT asserts arrive here BEFORE the dialog, so a run with no debugger still leaves a dump.
        int OnCrtReport(int reportType, char* message, int* returnValue)
        {
            if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR)
            {
                char reason[1024]{};
                ::_snprintf_s(reason, sizeof(reason), _TRUNCATE, "CRT assert: %s",
                              message != nullptr ? message : "<no message>");
                CapturedContext captured;
                CaptureCurrentContext(captured);
                Report(reason, &captured.pointers, nullptr, 0);
            }

            if (returnValue != nullptr) *returnValue = 0;
            return FALSE;   // FALSE: keep the normal reporting (dialog / debugger break) as well
        }
    }

    void InstallCrashHandler(CrashHandlerOptions options)
    {
        State& state = Get();
        bool   expected = false;
        if (!state.installed.compare_exchange_strong(expected, true)) return;

        std::error_code ec;
        const bool      created = std::filesystem::create_directories(options.dumpDir, ec);
        const bool      usable  = std::filesystem::is_directory(options.dumpDir, ec);

        const std::wstring dir = options.dumpDir.wstring();
        ::wcsncpy_s(state.dir, dir.c_str(), MAX_PATH - 1);

        // Printed at install: an empty crash folder is otherwise indistinguishable from a handler that never ran.
        std::printf("[crash] reports -> %s%s\n", options.dumpDir.string().c_str(),
                    usable ? "" : "  (NOT WRITABLE -- no reports will be produced)");
        (void)created;
        std::fflush(stdout);

        std::wstring name(options.appName.begin(), options.appName.end());
        if (name.empty()) name = L"App";
        ::wcsncpy_s(state.name, name.c_str(), 63);

        state.keep     = options.keepReports;
        state.dumpType = ResolveDumpType();

        // A heap dump of a debug build runs to gigabytes, so keepReports is clamped here, where the memory mode is known.
        const bool        heap    = (state.dumpType & MiniDumpWithPrivateReadWriteMemory) != 0;
        const std::size_t clamped = 3;
        const bool        capped  = heap && state.keep > clamped;
        if (capped) state.keep = clamped;

        // Said at install so a dump missing the heap is not discovered only after the process is gone.
        std::printf("[crash] dump memory: %s (LUMORA_CRASH_HEAP=%s), keeping %zu%s\n",
                    heap ? "stacks + private heap -- gigabytes, every heap object readable"
                         : "stacks + one hop -- tens of MB, heap objects usually absent",
                    heap ? "1" : "0", state.keep,
                    capped ? " (capped from the requested count for heap dumps)" : "");
        std::fflush(stdout);

        // Vectored first: the unhandled filter never fires for a fault on a fiber stack. The filter remains the backup for the main thread.
        ::AddVectoredExceptionHandler(1 /*first*/, &OnVectoredException);
        ::SetUnhandledExceptionFilter(&OnUnhandledException);

        // A stack overflow needs a reserve to run the handler on at all.
        ULONG guarantee = 64 * 1024;
        ::SetThreadStackGuarantee(&guarantee);

        (void)std::signal(SIGABRT, &OnAbort);
        ::_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);   // no "this application has requested..." box
        std::set_terminate(&OnTerminate);
        ::_set_purecall_handler(&OnPureCall);
        ::_set_invalid_parameter_handler(&OnInvalidParameter);
#ifdef _DEBUG
        ::_CrtSetReportHook(&OnCrtReport);
#endif
    }

    std::filesystem::path WriteCrashReport(std::string_view reason)
    {
        char text[512]{};
        ::_snprintf_s(text, sizeof(text), _TRUNCATE, "%.*s", static_cast<int>(reason.size()), reason.data());

        wchar_t dump[MAX_PATH]{};
        if (!Report(text, nullptr, dump, MAX_PATH)) return {};
        return std::filesystem::path{dump};
    }
}
