#include <gtest/gtest.h>
#include <lumora/os/DirectoryWatcher.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace lumora::os;

namespace
{
    std::atomic<std::uint64_t> g_rootSeq{0};

    std::filesystem::path MakeTempRoot(const char* tag)
    {
        const auto pid   = GetCurrentProcessId();
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto seq   = g_rootSeq.fetch_add(1, std::memory_order_relaxed);
        auto       root  = std::filesystem::temp_directory_path() /
                     ("dirwatch-" + std::string(tag) + "-" + std::to_string(pid) + "-" +
                      std::to_string(stamp) + "-" + std::to_string(seq));
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        std::filesystem::create_directories(root, ec);
        return root;
    }

    void WriteTestFile(const std::filesystem::path& p, std::string_view text)
    {
        std::filesystem::create_directories(p.parent_path());
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    template <typename Pred>
    std::vector<FileEvent> PollUntil(
        DirectoryWatcher& watcher,
        Pred&&            pred,
        std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        std::vector<FileEvent> all;
        const auto             deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            auto batch = watcher.Drain();
            if (!batch.empty())
            {
                all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
                if (pred(all)) return all;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        auto finalBatch = watcher.Drain();
        all.insert(all.end(), std::make_move_iterator(finalBatch.begin()), std::make_move_iterator(finalBatch.end()));
        return all;
    }

    std::vector<FileEvent> PollForEvents(DirectoryWatcher& watcher, std::size_t atLeast = 1)
    {
        return PollUntil(watcher, [atLeast](const std::vector<FileEvent>& events)
        {
            return events.size() >= atLeast;
        });
    }

    bool NothingArrivesWithin(DirectoryWatcher& watcher, std::chrono::milliseconds window)
    {
        const auto deadline = std::chrono::steady_clock::now() + window;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!watcher.Drain().empty()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return watcher.Drain().empty();
    }

    void DrainQuietly(DirectoryWatcher& watcher, std::chrono::milliseconds budget = std::chrono::milliseconds(500))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (std::chrono::steady_clock::now() < deadline)
        {
            (void)watcher.Drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

TEST(DirectoryWatcherTests, Created)
{
    const auto root = MakeTempRoot("created");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);

        WriteTestFile(root / "a.txt", "hello");

        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            return std::any_of(evs.begin(), evs.end(), [](const FileEvent& e)
            {
                return e.path == "a.txt" && e.change == FileChange::Added;
            });
        });
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty())
        {
            EXPECT_EQ(events[0].root, r);
            EXPECT_EQ(events[0].change, FileChange::Added);
            EXPECT_EQ(events[0].path, "a.txt");
        }
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, Modified)
{
    const auto root = MakeTempRoot("modified");
    WriteTestFile(root / "a.txt", "hello");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);
        (void)watcher.Drain();

        WriteTestFile(root / "a.txt", "goodbye");

        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            return std::any_of(evs.begin(), evs.end(), [](const FileEvent& e)
            {
                return e.path == "a.txt" && e.change == FileChange::Modified;
            });
        });
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty())
        {
            EXPECT_EQ(events[0].change, FileChange::Modified);
            EXPECT_EQ(events[0].path, "a.txt");
        }
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, Removed)
{
    const auto root = MakeTempRoot("removed");
    WriteTestFile(root / "a.txt", "hello");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);
        (void)watcher.Drain();

        std::filesystem::remove(root / "a.txt");

        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            return std::any_of(evs.begin(), evs.end(), [](const FileEvent& e)
            {
                return e.path == "a.txt" && e.change == FileChange::Removed;
            });
        });
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty())
        {
            EXPECT_EQ(events[0].change, FileChange::Removed);
            EXPECT_EQ(events[0].path, "a.txt");
        }
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, Renamed)
{
    const auto root = MakeTempRoot("renamed");
    WriteTestFile(root / "old.txt", "hello");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);
        (void)watcher.Drain();

        std::error_code ec;
        std::filesystem::rename(root / "old.txt", root / "new.txt", ec);
        EXPECT_FALSE(ec);

        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            bool hasOld = false, hasNew = false;
            for (const auto& e : evs)
            {
                if (e.path == "old.txt" && e.change == FileChange::Removed) hasOld = true;
                if (e.path == "new.txt" && e.change == FileChange::Added) hasNew = true;
            }
            return hasOld && hasNew;
        });
        EXPECT_EQ(events.size(), 2u);

        bool sawRemovedOld = false;
        bool sawAddedNew   = false;
        for (const auto& e : events)
        {
            if (e.path == "old.txt" && e.change == FileChange::Removed) sawRemovedOld = true;
            if (e.path == "new.txt" && e.change == FileChange::Added) sawAddedNew = true;
        }
        EXPECT_TRUE(sawRemovedOld);
        EXPECT_TRUE(sawAddedNew);
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, BurstCoalesces)
{
    const auto root = MakeTempRoot("burst");
    WriteTestFile(root / "a.txt", "hello");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);
        watcher.SetQuietWindow(std::chrono::milliseconds(150));
        (void)watcher.Drain();

        for (int i = 0; i < 5; ++i)
        {
            WriteTestFile(root / "a.txt", "rev " + std::to_string(i));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const auto events = PollForEvents(watcher);
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty())
        {
            EXPECT_EQ(events[0].change, FileChange::Modified);
            EXPECT_EQ(events[0].path, "a.txt");
        }
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, Nested)
{
    const auto root = MakeTempRoot("nested");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);

        WriteTestFile(root / "sub" / "deep" / "x.txt", "hello");

        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            return std::any_of(evs.begin(), evs.end(), [](const FileEvent& e)
            {
                return e.path == "sub/deep/x.txt";
            });
        });
        EXPECT_FALSE(events.empty());
        bool found = false;
        for (const auto& e : events)
            if (e.path == "sub/deep/x.txt") found = true;
        EXPECT_TRUE(found);
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, IgnoresComponents)
{
    const auto root = MakeTempRoot("ignored");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);

        WriteTestFile(root / ".cache" / "x.txt", "hello");

        EXPECT_TRUE(NothingArrivesWithin(watcher, std::chrono::milliseconds(500)));
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, QuietWindowHolds)
{
    const auto root = MakeTempRoot("quiet");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);
        watcher.SetQuietWindow(std::chrono::milliseconds(300));

        WriteTestFile(root / "a.txt", "hello");

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        EXPECT_TRUE(watcher.Drain().empty());

        const auto events = PollForEvents(watcher);
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty()) EXPECT_EQ(events[0].change, FileChange::Added);
    }
    std::filesystem::remove_all(root);
}

TEST(DirectoryWatcherTests, AddRootFailsForNonexistentDirectory)
{
    const auto root = MakeTempRoot("nonexistent");
    std::filesystem::remove_all(root);

    DirectoryWatcher watcher;
    EXPECT_EQ(watcher.AddRoot(root), DirectoryWatcher::kInvalidRoot);
}

TEST(DirectoryWatcherTests, RemoveRootDuringBurstStopsCleanlyAndFreesItsHandle)
{
    HANDLE self          = GetCurrentProcess();
    DWORD  handlesBefore = 0;
    EXPECT_NE(GetProcessHandleCount(self, &handlesBefore), 0);

    const auto root       = MakeTempRoot("remove-burst");
    const auto secondRoot = MakeTempRoot("remove-burst-second");
    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);

        std::atomic<bool> stopWriting{false};
        std::thread       writer([&]
        {
            int i = 0;
            while (!stopWriting.load(std::memory_order_relaxed))
            {
                WriteTestFile(root / ("churn" + std::to_string(i++) + ".txt"), "x");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        EXPECT_FALSE(PollForEvents(watcher).empty());

        watcher.RemoveRoot(r);

        stopWriting.store(true, std::memory_order_relaxed);
        writer.join();

        DrainQuietly(watcher);

        WriteTestFile(root / "after-remove.txt", "must not be reported");
        EXPECT_TRUE(NothingArrivesWithin(watcher, std::chrono::milliseconds(500)));

        const std::uint32_t r2 = watcher.AddRoot(secondRoot);
        EXPECT_NE(r2, DirectoryWatcher::kInvalidRoot);
        EXPECT_NE(r2, r);

        WriteTestFile(secondRoot / "b.txt", "hello");
        const auto events = PollUntil(watcher, [](const std::vector<FileEvent>& evs)
        {
            return std::any_of(evs.begin(), evs.end(), [](const FileEvent& e)
            {
                return e.path == "b.txt" && e.change == FileChange::Added;
            });
        });
        EXPECT_EQ(events.size(), 1u);
        if (!events.empty())
        {
            EXPECT_EQ(events[0].root, r2);
            EXPECT_EQ(events[0].change, FileChange::Added);
        }
    }
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(secondRoot);

    DWORD handlesAfter = 0;
    EXPECT_NE(GetProcessHandleCount(self, &handlesAfter), 0);
    EXPECT_LE(handlesAfter, handlesBefore + 8);
}

TEST(DirectoryWatcherTests, DestroyingWatcherDuringActivityDoesNotCorruptMemory)
{
    HANDLE self          = GetCurrentProcess();
    DWORD  handlesBefore = 0;
    EXPECT_NE(GetProcessHandleCount(self, &handlesBefore), 0);

    const auto        root = MakeTempRoot("destroy-live");
    std::atomic<bool> stopWriting{false};
    std::thread       writer([&]
    {
        int i = 0;
        while (!stopWriting.load(std::memory_order_relaxed))
        {
            WriteTestFile(root / ("churn" + std::to_string(i++) + ".txt"), "x");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    {
        DirectoryWatcher     watcher;
        const std::uint32_t  r = watcher.AddRoot(root);
        EXPECT_NE(r, DirectoryWatcher::kInvalidRoot);

        EXPECT_FALSE(PollForEvents(watcher).empty());
    }

    stopWriting.store(true, std::memory_order_relaxed);
    writer.join();
    std::filesystem::remove_all(root);

    DWORD handlesAfter = 0;
    EXPECT_NE(GetProcessHandleCount(self, &handlesAfter), 0);
    EXPECT_LE(handlesAfter, handlesBefore + 8);
}
