#include <gtest/gtest.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Context.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Reactive/Tracking.h>

#include "Reactive/ReactiveGraph.h"

#include <memory>
#include <string>
#include <vector>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

TEST(ReactiveTests, SignalWriteRerunsEffect) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto count = Signal(0);
    int runs = 0;
    int seen = -1;
    Effect([&, count] {
        seen = count();
        ++runs;
    });
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(seen, 0);

    count = 5;
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(seen, 5);

    count = 5;
    EXPECT_EQ(runs, 2);

    count += 1;
    EXPECT_EQ(runs, 3);
    EXPECT_EQ(seen, 6);

    auto flag = Signal(false);
    int flips = 0;
    Effect([&, flag] {
        (void)flag();
        ++flips;
    });
    flag.Toggle();
    EXPECT_EQ(flips, 2);
    EXPECT_TRUE(flag.Peek());

    auto items = Signal(std::vector<int>{});
    int pushes = 0;
    Effect([&, items] {
        (void)items();
        ++pushes;
    });
    items.Update([](std::vector<int>& v) { v.push_back(4); });
    EXPECT_EQ(pushes, 2);
    EXPECT_EQ(items.Peek().size(), 1u);
}

TEST(ReactiveTests, PeekDoesNotTrack) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto value = Signal(1);
    int runs = 0;
    Effect([&, value] {
        (void)value.Peek();
        ++runs;
    });
    value = 2;
    EXPECT_EQ(runs, 1);
}

TEST(ReactiveTests, ComputedLazyAndRetracks) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    auto flag = Signal(true);
    auto a = Signal(1);
    auto b = Signal(2);
    int evals = 0;
    auto pick = Computed([&evals, flag, a, b] {
        ++evals;
        return flag() ? a() : b();
    });
    EXPECT_EQ(evals, 1);
    EXPECT_EQ(pick(), 1);

    a = 10;
    EXPECT_EQ(evals, 1);
    EXPECT_EQ(pick(), 10);
    EXPECT_EQ(evals, 2);

    b = 20;
    EXPECT_EQ(pick(), 10);
    EXPECT_EQ(evals, 2);

    flag = false;
    EXPECT_EQ(pick(), 20);
    EXPECT_EQ(evals, 3);

    a = 100;
    EXPECT_EQ(pick(), 20);
    EXPECT_EQ(evals, 3);
}

TEST(ReactiveTests, EffectTracksComputed) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto count = Signal(1);
    auto doubled = Computed([count] { return count() * 2; });
    int runs = 0;
    int seen = 0;
    Effect([&, doubled] {
        seen = doubled();
        ++runs;
    });
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(seen, 2);

    count = 3;
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(seen, 6);
}

TEST(ReactiveTests, OwnerDisposalOrder) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    auto value = Signal(0);
    int runs = 0;
    std::vector<int> order;
    {
        Owner owner;
        Effect([&, value] {
            (void)value();
            ++runs;
        });
        OnCleanup([&] { order.push_back(1); });
        OnCleanup([&] { order.push_back(2); });
    }
    EXPECT_EQ(order, (std::vector<int>{2, 1}));

    value = 7;
    EXPECT_EQ(runs, 1);
}

TEST(ReactiveTests, EffectWithoutOwnerDoesNotPersist) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    auto value = Signal(0);
    int runs = 0;
    Effect([&, value] {
        (void)value();
        ++runs;
    });
    EXPECT_EQ(runs, 1);

    value = 1;
    EXPECT_EQ(runs, 1);
}

TEST(ReactiveTests, MapSingleSource) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    auto count = Signal(1);
    auto label = count.Map([](int v) { return v * 10; });
    EXPECT_EQ(label(), 10);
    count = 2;
    EXPECT_EQ(label(), 20);

    auto other = Signal(100);
    auto sneaky = count.Map([other](int v) { return v + other(); });
    EXPECT_EQ(sneaky(), 102);
    other = 200;
    EXPECT_EQ(sneaky(), 102);
    count = 3;
    EXPECT_EQ(sneaky(), 203);

    auto chained = label.Map([](int v) { return v + 1; });
    count = 4;
    EXPECT_EQ(chained(), 41);
}

TEST(ReactiveTests, ContextShadowing) {
    static Context<int> DepthCtx;

    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    Owner outer;
    Provide(DepthCtx, 1);
    EXPECT_EQ(Use(DepthCtx), 1);
    {
        Owner inner;
        Provide(DepthCtx, 2);
        EXPECT_EQ(Use(DepthCtx), 2);
    }
    EXPECT_EQ(Use(DepthCtx), 1);
}

TEST(ReactiveTests, TwoGraphsSharedSignal) {
    auto graphA = MakeRef<ReactiveGraph>();
    auto graphB = MakeRef<ReactiveGraph>();

    auto shared = Signal(0);
    int runsA = 0;
    int runsB = 0;

    auto ownerA = std::make_unique<Owner>(Dormant);
    auto ownerB = std::make_unique<Owner>(Dormant);
    {
        GraphScope scope(*graphA);
        Owner::Scope active(*ownerA);
        Effect([&, shared] {
            (void)shared();
            ++runsA;
        });
    }
    {
        GraphScope scope(*graphB);
        Owner::Scope active(*ownerB);
        Effect([&, shared] {
            (void)shared();
            ++runsB;
        });
    }
    EXPECT_EQ(runsA, 1);
    EXPECT_EQ(runsB, 1);

    shared = 1;
    EXPECT_EQ(runsA, 2);
    EXPECT_EQ(runsB, 2);

    {
        GraphScope scope(*graphB);
        shared = 2;
    }
    EXPECT_EQ(runsA, 3);
    EXPECT_EQ(runsB, 3);

    ownerA->Dispose();
    shared = 3;
    EXPECT_EQ(runsA, 3);
    EXPECT_EQ(runsB, 4);
}

TEST(ReactiveTests, LifecycleHooks) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    bool mounted = false;
    float accumulated = 0.0f;
    constexpr float kStep = 1.f / 60.f;
    {
        Owner owner;
        OnMount([&] { mounted = true; });
        OnFrame([&](float dt) { accumulated += dt; });
        EXPECT_TRUE(mounted);

        graph->Tick(kStep);
        EXPECT_FLOAT_EQ(accumulated, kStep);
    }
    graph->Tick(kStep);
    EXPECT_FLOAT_EQ(accumulated, kStep);
}

TEST(ReactiveTests, OnMountShieldsTracking) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto value = Signal(0);
    int runs = 0;
    Effect([&, value] {
        OnMount([&, value] { (void)value(); });
        ++runs;
    });
    EXPECT_EQ(runs, 1);

    value = 1;
    EXPECT_EQ(runs, 1);
}

TEST(ReactiveTests, ReactivePropAbsorbs) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    Reactive<int> constant = 5;
    EXPECT_TRUE(constant.IsStatic());
    EXPECT_EQ(constant.Get(), 5);

    auto value = Signal(7);
    Reactive<int> fromSignal = value;
    EXPECT_FALSE(fromSignal.IsStatic());
    EXPECT_EQ(fromSignal.Get(), 7);

    Reactive<int> fromLambda = [value] { return value() + 1; };
    EXPECT_FALSE(fromLambda.IsStatic());
    EXPECT_EQ(fromLambda.Get(), 8);

    auto doubled = Computed([value] { return value() * 2; });
    Reactive<int> fromComputed = doubled;
    EXPECT_FALSE(fromComputed.IsStatic());
    EXPECT_EQ(fromComputed.Get(), 14);

    int patches = 0;
    Effect([&, fromSignal] {
        (void)fromSignal.Get();
        ++patches;
    });
    value = 9;
    EXPECT_EQ(patches, 2);
}

TEST(ReactiveTests, BindTwoWay) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto on = Signal(false);
    Bind<bool> bind = on;
    EXPECT_FALSE(bind());
    bind.Set(true);
    EXPECT_TRUE(on.Peek());

    int model = 3;
    Bind<int> pair([&model] { return model; }, [&model](int v) { model = v; });
    EXPECT_EQ(pair.Get(), 3);
    pair.Set(4);
    EXPECT_EQ(model, 4);
}

TEST(ReactiveTests, StrAbsorbs) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);

    Str literal = "hello";
    EXPECT_TRUE(literal.IsStatic());
    EXPECT_EQ(literal.Get(), "hello");

    auto name = Signal(std::string("world"));
    Str fromSignal = name;
    EXPECT_FALSE(fromSignal.IsStatic());
    EXPECT_EQ(fromSignal.Get(), "world");

    Str fromLambda = [name] { return "hi " + name(); };
    EXPECT_EQ(fromLambda.Get(), "hi world");

    Str fromMap = name.Map([](const std::string& s) { return s + "!"; });
    EXPECT_EQ(fromMap.Get(), "world!");
}

TEST(ReactiveTests, GlitchFreeDiamond) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto root = Signal(1);
    auto left  = Computed([root] { return root() + 1; });
    auto right = Computed([root] { return root() * 2; });
    int joinEvals = 0;
    auto join = Computed([&joinEvals, left, right] {
        ++joinEvals;
        return left() + right();
    });

    int runs = 0;
    int seen = 0;
    Effect([&, join] {
        seen = join();
        ++runs;
    });
    EXPECT_EQ(joinEvals, 1);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(seen, 4);

    root = 5;
    EXPECT_EQ(seen, 16);
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(joinEvals, 2);
}

TEST(ReactiveTests, DuplicateReadFiresOnce) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto value = Signal(0);
    int runs = 0;
    Effect([&, value] {
        (void)value();
        (void)value();
        ++runs;
    });
    EXPECT_EQ(runs, 1);

    value = 1;
    EXPECT_EQ(runs, 2);
}

TEST(ReactiveTests, BatchCoalesces) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto x = Signal(0);
    auto y = Signal(0);
    int runs = 0;
    int sum = 0;
    Effect([&, x, y] {
        sum = x() + y();
        ++runs;
    });
    EXPECT_EQ(runs, 1);

    Batch([&] {
        x = 1;
        y = 2;
        EXPECT_EQ(runs, 1);
    });
    EXPECT_EQ(runs, 2);
    EXPECT_EQ(sum, 3);
}

TEST(ReactiveTests, ReentrantWriteIsDropped) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto value = Signal(0);
    int runs = 0;
    Effect([&, value] {
        const int current = value();
        ++runs;
        if (current < 1) {
            value = current + 1;
        }
    });
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(value.Peek(), 1);

    value = 5;
    EXPECT_EQ(runs, 2);
}
