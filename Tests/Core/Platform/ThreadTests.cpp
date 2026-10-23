#include <gtest/gtest.h>
#include <lumora/platform/Thread.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace platform = lumora::platform;

TEST(ThreadTests, StartRunsOnAnotherThread)
{
    const platform::ThreadId mainId = platform::CurrentThreadId();
    platform::ThreadId workerId = 0;
    std::atomic<bool> done = false;

    platform::Thread thread;
    EXPECT_FALSE(thread.Joinable());
    EXPECT_EQ(thread.Id(), 0u);

    const bool started = thread.Start("Worker", [&] {
        workerId = platform::CurrentThreadId();
        done = true;
    });

    ASSERT_TRUE(started);
    EXPECT_TRUE(thread.Joinable());
    EXPECT_NE(thread.Id(), 0u);

    thread.Join();

    EXPECT_FALSE(thread.Joinable());
    EXPECT_EQ(thread.Id(), 0u);
    EXPECT_TRUE(done);
    EXPECT_NE(workerId, 0u);
    EXPECT_NE(workerId, mainId);
}

TEST(ThreadTests, DoubleStartFails)
{
    std::atomic<bool> stop = false;
    platform::Thread thread;

    const bool started = thread.Start("DoubleStart", [&] {
        while (!stop.load())
        {
            std::this_thread::yield();
        }
    });
    ASSERT_TRUE(started);

    const bool secondStart = thread.Start("Second", [] {});
    EXPECT_FALSE(secondStart);

    stop = true;
    thread.Join();
}

TEST(ThreadTests, DestructorJoins)
{
    std::atomic<bool> executed = false;
    {
        platform::Thread thread;
        thread.Start("DtorJoin", [&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            executed = true;
        });
    }
    EXPECT_TRUE(executed);
}

TEST(ThreadTests, MoveConstructAndAssign)
{
    std::atomic<bool> executed1 = false;
    std::atomic<bool> executed2 = false;

    platform::Thread t1;
    t1.Start("T1", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        executed1 = true;
    });

    const platform::ThreadId t1Id = t1.Id();
    platform::Thread t2(std::move(t1));

    EXPECT_FALSE(t1.Joinable());
    EXPECT_EQ(t1.Id(), 0u);
    EXPECT_TRUE(t2.Joinable());
    EXPECT_EQ(t2.Id(), t1Id);

    platform::Thread t3;
    t3.Start("T3", [&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        executed2 = true;
    });

    // Move-assign t3 onto joinable t2 should join t2 first.
    t2 = std::move(t3);
    EXPECT_TRUE(executed1);
    EXPECT_TRUE(t2.Joinable());

    t2.Join();
    EXPECT_TRUE(executed2);
}

TEST(ThreadTests, NameAndPriorityDoNotCrash)
{
    platform::SetCurrentThreadName("MainTestThread");
    platform::SetCurrentThreadPriority(platform::ThreadPriority::Normal);
    platform::SetCurrentThreadPriority(platform::ThreadPriority::High);
    platform::SetCurrentThreadPriority(platform::ThreadPriority::Display);

    platform::Thread thread;
    thread.Start(
        "PrioTest",
        [] {
            platform::SetCurrentThreadName("PrioWorker");
            platform::SetCurrentThreadPriority(platform::ThreadPriority::Normal);
            platform::SetCurrentThreadPriority(platform::ThreadPriority::High);
            platform::SetCurrentThreadPriority(platform::ThreadPriority::Display);
        },
        platform::ThreadPriority::Display);

    thread.Join();
}
