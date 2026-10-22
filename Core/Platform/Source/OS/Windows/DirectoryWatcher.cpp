#include <lumora/os/DirectoryWatcher.h>

#include <lumora/log/Log.h>

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    struct DirectoryWatcher::Impl
    {
        struct RootWatch
        {
            OVERLAPPED             overlapped{};
            HANDLE                 handle = INVALID_HANDLE_VALUE;
            std::vector<DWORD>     buffer;
            std::vector<DWORD>     backBuffer;
            std::filesystem::path  root;
            std::uint32_t          index = 0;
            std::atomic<bool>      active{false};
            std::atomic<bool>      overflow{false};
            std::atomic<bool>      ioPending{false};
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

        std::uint32_t           AddRoot(const std::filesystem::path& root);
        void                     RemoveRoot(std::uint32_t rootIndex);
        std::vector<FileEvent>  Drain();
        bool                     ConsumeOverflow(std::uint32_t rootIndex);
        void                     SetQuietWindow(std::chrono::milliseconds window);
        void                     SetIgnoredComponents(std::vector<std::string> components);

        void ThreadMain();
        void HandleCompletion(RootWatch* rw, DWORD bytesTransferred, DWORD errorCode);
        void HandleRetireRequest(std::uint32_t rootIndex);
        void RequestRetire(std::uint32_t rootIndex);
        void ArmRead(RootWatch* rw);
        void ProcessBuffer(RootWatch* rw, DWORD bytesTransferred);
        void RecordEvent(std::uint32_t rootIndex, FileChange change, std::string path);
        void FinishRetire(RootWatch* rw);
        [[nodiscard]] bool AllRootsRetired();
        [[nodiscard]] bool IsIgnored(const std::string& relPath) const;
        [[nodiscard]] std::chrono::milliseconds GetQuietWindow() const;

        HANDLE            port = nullptr;
        std::thread       thread;
        std::atomic<bool> stopping{false};

        // Tag for a retire-request completion packet's lpOverlapped, distinct from the shutdown wakeup (nullptr) and from
        // real per-root completions. Only its address is compared.
        OVERLAPPED retireSentinel{};

        std::mutex                              rootsMutex;
        std::vector<std::unique_ptr<RootWatch>> roots;

        std::mutex                               pendingMutex;
        std::unordered_map<std::string, Pending> pending;

        mutable std::mutex        configMutex;
        std::chrono::milliseconds quietWindow{150};
        std::vector<std::string>  ignoredComponents{".cache", "Intermediate", "Binaries", ".git"};
    };

    DirectoryWatcher::Impl::Impl()
    {
        port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!port) LOG_ERROR("DirectoryWatcher: CreateIoCompletionPort failed ({})", GetLastError());
        thread = std::thread(&Impl::ThreadMain, this);
    }

    DirectoryWatcher::Impl::~Impl()
    {
        stopping.store(true, std::memory_order_release);

        // Routes every active root through the RemoveRoot retire path: only the watch thread touches a root's OVERLAPPED
        // after its first arm, so cancellation is requested here but performed there.
        {
            std::lock_guard lock(rootsMutex);
            for (auto& r : roots)
                if (r && r->active.exchange(false, std::memory_order_acq_rel))
                    RequestRetire(r->index);
        }

        // Wakes the thread if it has nothing to retire; ThreadMain exits only after every root confirms closed (see the
        // stopping check at the bottom of its loop).
        // The CloseHandle/clear() loop below is a no-op safety net.
        if (port) PostQueuedCompletionStatus(port, 0, 0, nullptr);

        if (thread.joinable()) thread.join();

        for (auto& r : roots)
            if (r) CloseHandle(r->handle);
        roots.clear();

        if (port) CloseHandle(port);
    }

    void DirectoryWatcher::Impl::ThreadMain()
    {
        for (;;)
        {
            DWORD        bytesTransferred = 0;
            ULONG_PTR    key              = 0;
            LPOVERLAPPED ov               = nullptr;
            const BOOL   ok = GetQueuedCompletionStatus(port, &bytesTransferred, &key, &ov, INFINITE);

            if (ov == &retireSentinel)
                HandleRetireRequest(static_cast<std::uint32_t>(key));
            else if (ov != nullptr)
            {
                const DWORD errorCode = ok ? ERROR_SUCCESS : GetLastError();
                HandleCompletion(reinterpret_cast<RootWatch*>(ov), bytesTransferred, errorCode);
            }
            // else: bare shutdown wakeup; whether to stop is decided below, after every packet kind.

            // Exit only once every retiring root has closed (FinishRetire freed its slot); freeing the buffer/OVERLAPPED
            // earlier would race the OS still writing into it.
            if (stopping.load(std::memory_order_acquire) && AllRootsRetired()) break;
        }
    }

    void DirectoryWatcher::Impl::HandleCompletion(RootWatch* rw, DWORD bytesTransferred, DWORD errorCode)
    {
        if (!rw->active.load(std::memory_order_acquire))
        {
            FinishRetire(rw);
            return;
        }

        const bool overflowed = bytesTransferred == 0 || errorCode == ERROR_NOTIFY_ENUM_DIR;
        if (overflowed) rw->overflow.store(true, std::memory_order_release);

        std::swap(rw->buffer, rw->backBuffer);
        ArmRead(rw);

        if (!overflowed) ProcessBuffer(rw, bytesTransferred);
    }

    void DirectoryWatcher::Impl::ArmRead(RootWatch* rw)
    {
        ZeroMemory(&rw->overlapped, sizeof(OVERLAPPED));
        DWORD      bytesReturned = 0;
        const BOOL ok = ReadDirectoryChangesW(
            rw->handle,
            rw->buffer.data(),
            static_cast<DWORD>(rw->buffer.size() * sizeof(DWORD)),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE,
            &bytesReturned,
            &rw->overlapped,
            nullptr);

        if (!ok && GetLastError() != ERROR_IO_PENDING)
        {
            LOG_ERROR("DirectoryWatcher: ReadDirectoryChangesW failed ({}) for {}", GetLastError(), rw->root.string());
            rw->overflow.store(true, std::memory_order_release);
            rw->ioPending.store(false, std::memory_order_release);
            return;
        }
        rw->ioPending.store(true, std::memory_order_release);
    }

    void DirectoryWatcher::Impl::ProcessBuffer(RootWatch* rw, DWORD bytesTransferred)
    {
        const auto* base   = reinterpret_cast<const std::byte*>(rw->backBuffer.data());
        DWORD       offset = 0;
        while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= bytesTransferred)
        {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(base + offset);

            FileChange change     = FileChange::Modified;
            bool       recognized = true;
            switch (info->Action)
            {
                case FILE_ACTION_ADDED:            change = FileChange::Added;    break;
                case FILE_ACTION_REMOVED:          change = FileChange::Removed;  break;
                case FILE_ACTION_MODIFIED:         change = FileChange::Modified; break;
                case FILE_ACTION_RENAMED_OLD_NAME: change = FileChange::Removed;  break;
                case FILE_ACTION_RENAMED_NEW_NAME: change = FileChange::Added;    break;
                default:                           recognized = false;           break;
            }

            if (recognized)
            {
                const int wlen = static_cast<int>(info->FileNameLength / sizeof(WCHAR));
                if (wlen > 0)
                {
                    const int needed =
                        WideCharToMultiByte(CP_UTF8, 0, info->FileName, wlen, nullptr, 0, nullptr, nullptr);
                    if (needed > 0)
                    {
                        std::string utf8(static_cast<std::size_t>(needed), '\0');
                        WideCharToMultiByte(CP_UTF8, 0, info->FileName, wlen, utf8.data(), needed, nullptr, nullptr);
                        std::replace(utf8.begin(), utf8.end(), '\\', '/');

                        if (!IsIgnored(utf8)) RecordEvent(rw->index, change, std::move(utf8));
                    }
                }
            }

            if (info->NextEntryOffset == 0) break;
            offset += info->NextEntryOffset;
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

    void DirectoryWatcher::Impl::HandleRetireRequest(std::uint32_t rootIndex)
    {
        RootWatch* rw = nullptr;
        {
            std::lock_guard lock(rootsMutex);
            if (rootIndex >= roots.size() || !roots[rootIndex]) return;
            rw = roots[rootIndex].get();
        }

        if (rw->ioPending.load(std::memory_order_acquire))
        {
            // The only CancelIoEx call: on the thread that owns every ArmRead, so it cannot land between ZeroMemory and the reissued read.
            // Its completion arrives via HandleCompletion, which finishes the retirement.
            CancelIoEx(rw->handle, &rw->overlapped);
        }
        else
        {
            // Nothing outstanding (the last ArmRead failed synchronously), so no completion will confirm retirement; finish directly.
            FinishRetire(rw);
        }
    }

    void DirectoryWatcher::Impl::RequestRetire(std::uint32_t rootIndex)
    {
        if (port) PostQueuedCompletionStatus(port, 0, static_cast<ULONG_PTR>(rootIndex), &retireSentinel);
    }

    void DirectoryWatcher::Impl::FinishRetire(RootWatch* rw)
    {
        std::lock_guard lock(rootsMutex);
        if (rw->index < roots.size() && roots[rw->index].get() == rw)
        {
            CloseHandle(rw->handle);
            roots[rw->index].reset();
        }
    }

    bool DirectoryWatcher::Impl::AllRootsRetired()
    {
        std::lock_guard lock(rootsMutex);
        return std::all_of(roots.begin(), roots.end(), [](const auto& r) { return r == nullptr; });
    }

    bool DirectoryWatcher::Impl::IsIgnored(const std::string& relPath) const
    {
        std::lock_guard lock(configMutex);
        std::size_t     start = 0;
        while (start <= relPath.size())
        {
            const std::size_t     slash = relPath.find('/', start);
            const std::size_t     end   = slash == std::string::npos ? relPath.size() : slash;
            const std::string_view component(relPath.data() + start, end - start);

            for (const auto& ignored : ignoredComponents)
                if (component == ignored) return true;

            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return false;
    }

    std::chrono::milliseconds DirectoryWatcher::Impl::GetQuietWindow() const
    {
        std::lock_guard lock(configMutex);
        return quietWindow;
    }

    std::uint32_t DirectoryWatcher::Impl::AddRoot(const std::filesystem::path& root)
    {
        const HANDLE handle = CreateFileW(
            root.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr);

        if (handle == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR("DirectoryWatcher: CreateFileW failed ({}) for {}", GetLastError(), root.string());
            return DirectoryWatcher::kInvalidRoot;
        }

        auto watch = std::make_unique<RootWatch>();
        watch->handle = handle;
        watch->buffer.resize(16384);
        watch->backBuffer.resize(16384);
        watch->root = root;
        watch->active.store(true, std::memory_order_relaxed);

        // Indices are never reused: a retire request in flight for a freed slot must not land on a different root later published into it.
        // Cost: one pointer-sized slot per root ever watched.
        RootWatch*    rw    = nullptr;
        std::uint32_t index = 0;
        {
            std::lock_guard lock(rootsMutex);
            index        = static_cast<std::uint32_t>(roots.size());
            watch->index = index;
            rw           = watch.get();
            roots.push_back(std::move(watch));
        }

        if (!CreateIoCompletionPort(handle, port, static_cast<ULONG_PTR>(index), 0))
        {
            LOG_ERROR("DirectoryWatcher: CreateIoCompletionPort failed ({}) for {}", GetLastError(), root.string());
            std::lock_guard lock(rootsMutex);
            CloseHandle(handle);
            roots[index].reset();
            return DirectoryWatcher::kInvalidRoot;
        }

        // Runs on the owner thread: the one exception to "only the watch thread touches an OVERLAPPED", safe because no
        // completion can exist before the read is issued and the index is not yet visible.
        ArmRead(rw);
        if (!rw->ioPending.load(std::memory_order_acquire))
        {
            // The initial arm failed synchronously: no read was queued, so no completion will come. Clean up directly (never
            // via RequestRetire) so the caller is not handed an index that never delivers.
            std::lock_guard lock(rootsMutex);
            CloseHandle(rw->handle);
            roots[index].reset();
            return DirectoryWatcher::kInvalidRoot;
        }

        return index;
    }

    void DirectoryWatcher::Impl::RemoveRoot(std::uint32_t rootIndex)
    {
        {
            std::lock_guard lock(rootsMutex);
            if (rootIndex >= roots.size() || !roots[rootIndex]) return;
            if (!roots[rootIndex]->active.exchange(false, std::memory_order_acq_rel)) return;
        }
        // Cancellation happens on the watch thread (HandleRetireRequest), never here: past a root's first arm the owner
        // thread must not touch its OVERLAPPED, which ArmRead mutates from the watch thread.
        RequestRetire(rootIndex);
    }

    std::vector<FileEvent> DirectoryWatcher::Impl::Drain()
    {
        const auto quiet = GetQuietWindow();
        const auto now   = std::chrono::steady_clock::now();

        std::vector<FileEvent> result;
        std::lock_guard        lock(pendingMutex);
        for (auto it = pending.begin(); it != pending.end();)
        {
            if (now - it->second.timestamp >= quiet)
            {
                FileEvent event;
                event.root   = it->second.root;
                event.change = it->second.change;
                event.path   = std::move(it->second.path);
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
