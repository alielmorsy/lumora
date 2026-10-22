#pragma once


#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace lumora::os
{
    enum class FileChange : std::uint8_t
    {
        Added,
        Modified,
        Removed
    };

    struct FileEvent
    {
        std::uint32_t root = 0;  ///< the root index AddRoot returned
        FileChange    change = FileChange::Modified;
        std::string   path;      ///< forward-slash, relative to that root
    };

    /// One completion port and thread shared by all roots (a stall on one delays the others); events are coalesced per path
    /// and delivered only via Drain.
    /// Windows only: elsewhere every Drain reports an overflow so callers rescan.
    class DirectoryWatcher
    {
    public:
        DirectoryWatcher();
        ~DirectoryWatcher();

        DirectoryWatcher(const DirectoryWatcher&)            = delete;
        DirectoryWatcher& operator=(const DirectoryWatcher&) = delete;

        /// Watch `root` and everything under it. Returns the index every FileEvent from it carries, or
        /// `kInvalidRoot` when the directory cannot be opened.
        [[nodiscard]] std::uint32_t AddRoot(const std::filesystem::path& root);
        void                        RemoveRoot(std::uint32_t root);

        /// Everything that has settled since the last call; empty is the normal, cheap answer.
        [[nodiscard]] std::vector<FileEvent> Drain();

        /// True once per overflow (the OS dropped notifications for this root); the only correct response is a full rescan of it.
        [[nodiscard]] bool ConsumeOverflow(std::uint32_t root);

        /// Quiet time a path needs before it is drained, so half-written files are not reported. Default 150 ms.
        void SetQuietWindow(std::chrono::milliseconds window);

        /// Paths containing any of these components are never reported. Defaults: `.cache`, `Intermediate`, `Binaries`, `.git`.
        void SetIgnoredComponents(std::vector<std::string> components);

        static constexpr std::uint32_t kInvalidRoot = 0xFFFFFFFFu;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}
