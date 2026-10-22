#include <lumora/os/DirectoryWatcher.h>
#include <lumora/log/Log.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    struct DirectoryWatcher::Impl
    {
        struct RootWatch
        {
            std::uint32_t         index = 0;
            std::filesystem::path root;
            std::atomic<bool>     active{false};
            std::atomic<bool>     overflow{false};
        };

        struct Pending
        {
            std::uint32_t                         root = 0;
            FileChange                            change = FileChange::Modified;
            std::string                           path;
            std::chrono::steady_clock::time_point timestamp;
        };

        Impl();
        ~Impl();

        std::uint32_t          AddRoot(const std::filesystem::path& root);
        void                   RemoveRoot(std::uint32_t rootIndex);
        std::vector<FileEvent> Drain();
        bool                   ConsumeOverflow(std::uint32_t rootIndex);
        void                   SetQuietWindow(std::chrono::milliseconds window);
        void                   SetIgnoredComponents(std::vector<std::string> components);

        void ThreadMain();
        void AddWatchRecursive(std::uint32_t rootIndex, const std::filesystem::path& dir);
        void RecordEvent(std::uint32_t rootIndex, FileChange change, std::string path);
        [[nodiscard]] bool IsIgnored(std::string_view relPath) const;
        [[nodiscard]] std::chrono::milliseconds GetQuietWindow() const;

        int               inotifyFd = -1;
        int               wakePipe[2]{-1, -1};
        std::thread       thread;
        std::atomic<bool> stopping{false};

        std::mutex                              rootsMutex;
        std::vector<std::unique_ptr<RootWatch>> roots;

        std::mutex                                                         watchMutex;
        std::unordered_map<int, std::pair<std::uint32_t, std::filesystem::path>> wdToDir;

        std::mutex                               pendingMutex;
        std::unordered_map<std::string, Pending> pending;

        mutable std::mutex        configMutex;
        std::chrono::milliseconds quietWindow{150};
        std::vector<std::string>  ignoredComponents{".cache", "Intermediate", "Binaries", ".git"};
    };

    DirectoryWatcher::Impl::Impl()
    {
        inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (inotifyFd < 0)
        {
            LOG_ERROR("DirectoryWatcher: inotify_init1 failed");
        }

        if (::pipe(wakePipe) != 0)
        {
            LOG_ERROR("DirectoryWatcher: pipe failed");
        }
        else
        {
            ::fcntl(wakePipe[0], F_SETFL, ::fcntl(wakePipe[0], F_GETFL) | O_NONBLOCK);
            ::fcntl(wakePipe[1], F_SETFL, ::fcntl(wakePipe[1], F_GETFL) | O_NONBLOCK);
        }

        thread = std::thread(&Impl::ThreadMain, this);
    }

    DirectoryWatcher::Impl::~Impl()
    {
        stopping.store(true, std::memory_order_release);
        if (wakePipe[1] >= 0)
        {
            const char byte = 'q';
            [[maybe_unused]] const auto res = ::write(wakePipe[1], &byte, 1);
        }

        if (thread.joinable())
        {
            thread.join();
        }

        if (wakePipe[0] >= 0) ::close(wakePipe[0]);
        if (wakePipe[1] >= 0) ::close(wakePipe[1]);
        if (inotifyFd >= 0) ::close(inotifyFd);
    }

    void DirectoryWatcher::Impl::AddWatchRecursive(std::uint32_t rootIndex, const std::filesystem::path& dir)
    {
        if (inotifyFd < 0) return;

        const uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;
        const int wd = ::inotify_add_watch(inotifyFd, dir.c_str(), mask);
        if (wd >= 0)
        {
            std::lock_guard lock(watchMutex);
            wdToDir[wd] = {rootIndex, dir};
        }

        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec))
        {
            if (ec) break;
            if (entry.is_directory(ec) && !ec)
            {
                std::error_code relEc;
                const auto rel = std::filesystem::relative(entry.path(), dir, relEc);
                if (!relEc && IsIgnored(rel.generic_string()))
                {
                    continue;
                }

                const int subWd = ::inotify_add_watch(inotifyFd, entry.path().c_str(), mask);
                if (subWd >= 0)
                {
                    std::lock_guard lock(watchMutex);
                    wdToDir[subWd] = {rootIndex, entry.path()};
                }
            }
        }
    }

    std::uint32_t DirectoryWatcher::Impl::AddRoot(const std::filesystem::path& root)
    {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec) || ec)
        {
            return DirectoryWatcher::kInvalidRoot;
        }

        auto watch = std::make_unique<RootWatch>();
        watch->root = root;
        watch->active.store(true, std::memory_order_relaxed);

        std::uint32_t index = 0;
        {
            std::lock_guard lock(rootsMutex);
            index = static_cast<std::uint32_t>(roots.size());
            watch->index = index;
            roots.push_back(std::move(watch));
        }

        AddWatchRecursive(index, root);
        return index;
    }

    void DirectoryWatcher::Impl::RemoveRoot(std::uint32_t rootIndex)
    {
        {
            std::lock_guard lock(rootsMutex);
            if (rootIndex >= roots.size() || !roots[rootIndex]) return;
            if (!roots[rootIndex]->active.exchange(false, std::memory_order_acq_rel)) return;
        }

        {
            std::lock_guard lock(watchMutex);
            for (auto it = wdToDir.begin(); it != wdToDir.end();)
            {
                if (it->second.first == rootIndex)
                {
                    if (inotifyFd >= 0)
                    {
                        ::inotify_rm_watch(inotifyFd, it->first);
                    }
                    it = wdToDir.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        if (wakePipe[1] >= 0)
        {
            const char byte = 'r';
            [[maybe_unused]] const auto res = ::write(wakePipe[1], &byte, 1);
        }
    }

    void DirectoryWatcher::Impl::ThreadMain()
    {
        std::array<pollfd, 2> pfds{};
        pfds[0].fd = inotifyFd;
        pfds[0].events = POLLIN;
        pfds[1].fd = wakePipe[0];
        pfds[1].events = POLLIN;

        alignas(struct inotify_event) char buffer[16384];

        while (!stopping.load(std::memory_order_acquire))
        {
            const int ret = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), 200);
            if (ret <= 0)
            {
                if (ret < 0 && errno == EINTR) continue;
                continue;
            }

            if (pfds[1].revents & POLLIN)
            {
                char drainBuf[64];
                while (::read(wakePipe[0], drainBuf, sizeof(drainBuf)) > 0) {}
                if (stopping.load(std::memory_order_acquire)) break;
            }

            if (pfds[0].revents & POLLIN)
            {
                for (;;)
                {
                    const ssize_t len = ::read(inotifyFd, buffer, sizeof(buffer));
                    if (len <= 0) break;

                    ssize_t offset = 0;
                    while (offset < len)
                    {
                        const auto* event = reinterpret_cast<const struct inotify_event*>(&buffer[offset]);
                        offset += static_cast<ssize_t>(sizeof(struct inotify_event) + event->len);

                        if (event->mask & IN_Q_OVERFLOW)
                        {
                            std::lock_guard lock(rootsMutex);
                            for (auto& r : roots)
                            {
                                if (r && r->active.load(std::memory_order_relaxed))
                                {
                                    r->overflow.store(true, std::memory_order_release);
                                }
                            }
                            continue;
                        }

                        std::uint32_t rootIndex = 0;
                        std::filesystem::path dirPath;
                        {
                            std::lock_guard lock(watchMutex);
                            const auto it = wdToDir.find(event->wd);
                            if (it == wdToDir.end()) continue;
                            rootIndex = it->second.first;
                            dirPath = it->second.second;
                        }

                        std::filesystem::path rootPath;
                        {
                            std::lock_guard lock(rootsMutex);
                            if (rootIndex >= roots.size() || !roots[rootIndex]) continue;
                            if (!roots[rootIndex]->active.load(std::memory_order_acquire)) continue;
                            rootPath = roots[rootIndex]->root;
                        }

                        if (event->len == 0) continue;

                        const std::filesystem::path fullPath = dirPath / event->name;
                        std::error_code relEc;
                        auto rel = std::filesystem::relative(fullPath, rootPath, relEc);
                        if (relEc) continue;

                        std::string relStr = rel.generic_string();
                        if (IsIgnored(relStr)) continue;

                        if ((event->mask & IN_ISDIR) && (event->mask & (IN_CREATE | IN_MOVED_TO)))
                        {
                            const uint32_t mask = IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;
                            const int newWd = ::inotify_add_watch(inotifyFd, fullPath.c_str(), mask);
                            if (newWd >= 0)
                            {
                                std::lock_guard lock(watchMutex);
                                wdToDir[newWd] = {rootIndex, fullPath};
                            }
                        }

                        FileChange change = FileChange::Modified;
                        if (event->mask & (IN_CREATE | IN_MOVED_TO))
                        {
                            change = FileChange::Added;
                        }
                        else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                        {
                            change = FileChange::Removed;
                        }
                        else if (event->mask & (IN_MODIFY | IN_ATTRIB))
                        {
                            change = FileChange::Modified;
                        }

                        RecordEvent(rootIndex, change, std::move(relStr));
                    }
                }
            }
        }
    }

    void DirectoryWatcher::Impl::RecordEvent(std::uint32_t rootIndex, FileChange change, std::string path)
    {
        std::string key = std::to_string(rootIndex);
        key += ':';
        key += path;

        const auto now = std::chrono::steady_clock::now();

        std::lock_guard lock(pendingMutex);
        auto [it, inserted] = pending.try_emplace(std::move(key), Pending{rootIndex, change, std::move(path), now});
        if (inserted) return;

        Pending& existing = it->second;
        if (change == FileChange::Removed)
            existing.change = FileChange::Removed;
        else if (existing.change == FileChange::Removed && change == FileChange::Added)
            existing.change = FileChange::Modified;
        else if (existing.change == FileChange::Added && change == FileChange::Modified)
            existing.change = FileChange::Added;
        else if (existing.change == FileChange::Added && change == FileChange::Added)
            existing.change = FileChange::Added;
        else if (existing.change == FileChange::Modified && change == FileChange::Added)
            existing.change = FileChange::Added;
        else
            existing.change = change;
        existing.timestamp = now;
    }

    std::vector<FileEvent> DirectoryWatcher::Impl::Drain()
    {
        const auto quiet = GetQuietWindow();
        const auto now = std::chrono::steady_clock::now();

        std::vector<FileEvent> result;
        std::lock_guard lock(pendingMutex);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (now - it->second.timestamp >= quiet)
            {
                FileEvent event;
                event.root = it->second.root;
                event.change = it->second.change;
                event.path = std::move(it->second.path);
                result.push_back(std::move(event));
                it = pending.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return result;
    }

    bool DirectoryWatcher::Impl::ConsumeOverflow(std::uint32_t rootIndex)
    {
        std::lock_guard lock(rootsMutex);
        if (rootIndex >= roots.size() || !roots[rootIndex]) return false;
        return roots[rootIndex]->overflow.exchange(false, std::memory_order_acq_rel);
    }

    void DirectoryWatcher::Impl::SetQuietWindow(std::chrono::milliseconds window)
    {
        std::lock_guard lock(configMutex);
        quietWindow = window;
    }

    void DirectoryWatcher::Impl::SetIgnoredComponents(std::vector<std::string> components)
    {
        std::lock_guard lock(configMutex);
        ignoredComponents = std::move(components);
    }

    std::chrono::milliseconds DirectoryWatcher::Impl::GetQuietWindow() const
    {
        std::lock_guard lock(configMutex);
        return quietWindow;
    }

    bool DirectoryWatcher::Impl::IsIgnored(std::string_view relPath) const
    {
        std::lock_guard lock(configMutex);
        std::size_t start = 0;
        while (start <= relPath.size())
        {
            const std::size_t slash = relPath.find('/', start);
            const std::size_t end = slash == std::string_view::npos ? relPath.size() : slash;
            const std::string_view component = relPath.substr(start, end - start);

            for (const auto& ignored : ignoredComponents)
            {
                if (component == ignored) return true;
            }

            if (slash == std::string_view::npos) break;
            start = slash + 1;
        }
        return false;
    }

    DirectoryWatcher::DirectoryWatcher() : _impl(std::make_unique<Impl>()) {}
    DirectoryWatcher::~DirectoryWatcher() = default;

    std::uint32_t DirectoryWatcher::AddRoot(const std::filesystem::path& root) { return _impl->AddRoot(root); }
    void          DirectoryWatcher::RemoveRoot(std::uint32_t root) { _impl->RemoveRoot(root); }
    std::vector<FileEvent> DirectoryWatcher::Drain() { return _impl->Drain(); }
    bool DirectoryWatcher::ConsumeOverflow(std::uint32_t root) { return _impl->ConsumeOverflow(root); }
    void DirectoryWatcher::SetQuietWindow(std::chrono::milliseconds window) { _impl->SetQuietWindow(window); }

    void DirectoryWatcher::SetIgnoredComponents(std::vector<std::string> components)
    {
        _impl->SetIgnoredComponents(std::move(components));
    }
}
