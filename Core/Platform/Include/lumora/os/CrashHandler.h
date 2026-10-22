#pragma once


#include <filesystem>
#include <string>

namespace lumora::os
{
    /// Postmortem capture for a crash with no debugger attached: writes a .dmp plus a .txt (reason, faulting module, thread).
    /// Covers access violations, abort, std::terminate, STL bounds checks and pure-virtual calls.
    struct CrashHandlerOptions
    {
        /// Where the .dmp/.txt pair goes. Created if missing.
        std::filesystem::path dumpDir;

        /// Prefix for the report files, e.g. "App" -> "App-2026-07-30-141233-4812.dmp".
        std::string appName = "App";

        /// Keep at most this many report pairs, oldest deleted first. 0 keeps everything.
        std::size_t keepReports = 20;
    };

    /// Idempotent; the second call is ignored. Install early so a crash during boot is captured too.
    void InstallCrashHandler(CrashHandlerOptions options);

    /// Writes a report for the current thread WITHOUT crashing. Returns the .dmp path, or empty if no handler is installed or
    /// the write failed.
    [[nodiscard]] std::filesystem::path WriteCrashReport(std::string_view reason);
}
