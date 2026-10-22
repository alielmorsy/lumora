#pragma once


#include <cstdint>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::os
{
    // This process's memory: `workingSet` is resident physical memory (the task manager number); `privateBytes` is commit
    // charge, which does not fall when pages are merely trimmed.
    struct ProcessMemory
    {
        std::uint64_t workingSetBytes     = 0;
        std::uint64_t peakWorkingSetBytes = 0;
        std::uint64_t privateBytes        = 0;
    };

    [[nodiscard]] ProcessMemory CurrentProcessMemory();

    // An environment block is a list of "NAME=VALUE" entries; empty => inherit the parent's environment.
    using EnvBlock = std::vector<std::string>;

    // Result of a synchronous child-process run. `launched` distinguishes "could not start" (launched
    // == false, exitCode meaningless) from "ran and returned exitCode".
    struct ProcessResult
    {
        bool        launched = false;
        int         exitCode = -1;
        std::string out; // captured stdout (or merged stdout+stderr when mergeStreams is set)
        std::string err; // captured stderr (empty when merged)
    };

    struct ProcessOptions
    {
        std::filesystem::path                 workingDir;          // empty => inherit
        EnvBlock                              env;                 // empty => inherit
        bool                                  mergeStreams = false; // fold stderr into `out`
        std::function<void(std::string_view)> onLine;             // set => deliver output lines as they arrive

        // A stop request terminates the child and every process it started, and the run returns with
        // exitCode 1. Default-constructed (no stop source) => the run cannot be cancelled.
        std::stop_token stop;
    };

    // Spawns `exe` with `args` (no shell; each argument quoted), waits, and captures output. mergeStreams folds stderr into
    // `out`; completed lines also go to `onLine`.
    [[nodiscard]] ProcessResult Run(const std::filesystem::path& exe, const std::vector<std::string>& args,
                                    const ProcessOptions& opts = {});

    // Spawns a pre-formed command line (caller already quoted every argument), e.g. `cmd.exe /c "vcvars64.bat && set"`.
    // Output is always merged into `out`.
    [[nodiscard]] ProcessResult RunCommandLine(std::string_view commandLine, const ProcessOptions& opts = {});

    // Quotes a single argument for a command line if it contains spaces or quotes.
    [[nodiscard]] std::string QuoteArg(std::string_view arg);
}
