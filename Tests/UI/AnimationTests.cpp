#include <gtest/gtest.h>

#include <lumora/Animation/Animation.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Signal.h>

#include "Reactive/ReactiveGraph.h"

#include <cmath>

using namespace lumora;
using lumora::MakeRef;
using lumora::Ref;

namespace {

TEST(AnimationTests, LerpEndpointsAndColorRounding) {
    EXPECT_EQ(Lerp(0.f, 10.f, 0.f), 0.f);
    EXPECT_EQ(Lerp(0.f, 10.f, 1.f), 10.f);
    EXPECT_EQ(Lerp(0.f, 10.f, 0.5f), 5.f);

    EXPECT_EQ(Lerp(Color::Black, Color::White, 0.f), Color::Black);
    EXPECT_EQ(Lerp(Color::Black, Color::White, 1.f), Color::White);

    const Color mid = Lerp(Color::Black, Color::White, 0.5f);
    EXPECT_TRUE(mid.r == 128 && mid.g == 128 && mid.b == 128 && mid.a == 255);
}

TEST(AnimationTests, SpringSettlesAndDrivesEffect) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    Motion<float> m      = Spring<float>(target, spring::Stiff);

    int   runs = 0;
    float seen = -1.f;
    Effect([&, m] {
        seen = m();
        ++runs;
    });
    EXPECT_TRUE(runs == 1 && seen == 0.f);

    target = 100.f;
    for (int i = 0; i < 1000 && m.IsAnimating(); ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_FALSE(m.IsAnimating());
    EXPECT_NEAR(m.Peek(), 100.f, 0.5f);
    EXPECT_NEAR(seen, m.Peek(), 1e-4f);
    EXPECT_GT(runs, 2);

    const int runsAtRest = runs;
    graph->Tick(1.f / 60.f);
    graph->Tick(1.f / 60.f);
    EXPECT_EQ(runs, runsAtRest);
}

TEST(AnimationTests, SpringIsInterruptible) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    Motion<float> m      = Spring<float>(target, spring::Gentle);

    target = 100.f;
    for (int i = 0; i < 8; ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_TRUE(m.IsAnimating());
    EXPECT_TRUE(m.Peek() > 0.f && m.Peek() < 100.f);

    target = 0.f;
    for (int i = 0; i < 1000 && m.IsAnimating(); ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_FALSE(m.IsAnimating());
    EXPECT_NEAR(m.Peek(), 0.f, 0.5f);
}

TEST(AnimationTests, SpringFrameRateIndependent) {
    const auto run = [](float step, int steps) {
        auto graph = MakeRef<ReactiveGraph>();
        GraphScope scope(*graph);
        Owner owner;
        auto          t = Signal(0.f);
        Motion<float> m = Spring<float>(t, spring::Gentle);
        t = 1.f;
        for (int i = 0; i < steps; ++i) {
            graph->Tick(step);
        }
        return m.Peek();
    };

    const float a = run(1.f / 60.f, 12);
    const float b = run(1.f / 240.f, 48);
    EXPECT_TRUE(a > 0.3f && a < 1.5f);
    EXPECT_NEAR(a, b, 0.02f);
}

TEST(AnimationTests, MotionAdvancesOnEveryHighRateFrameAtTheSameSpeed) {
    const auto run = [](float step, int steps, int* moves) {
        auto graph = MakeRef<ReactiveGraph>();
        GraphScope scope(*graph);
        Owner owner;
        auto          t = Signal(0.f);
        Motion<float> m = Tween<float>(t, {.duration = 0.5f});
        t = 1.f;
        float last = m.Peek();
        for (int i = 0; i < steps; ++i) {
            graph->Tick(step);
            if (moves && m.Peek() != last) {
                ++*moves;
            }
            last = m.Peek();
        }
        return m.Peek();
    };

    int moves = 0;
    const float fast = run(1.f / 165.f, 33, &moves); // 0.2 s at 165 Hz
    const float slow = run(1.f / 60.f, 12, nullptr); // 0.2 s at 60 Hz
    EXPECT_EQ(moves, 33);
    EXPECT_NEAR(fast, slow, 0.01f);
}

TEST(AnimationTests, SpringSurvivesHugeDelta) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    Motion<float> m      = Spring<float>(target, spring::Stiff);
    target               = 1.f;

    graph->Tick(1000.f);
    EXPECT_TRUE(std::isfinite(m.Peek()));
    EXPECT_TRUE(m.Peek() >= 0.f && m.Peek() <= 1.5f);

    for (int i = 0; i < 1000 && m.IsAnimating(); ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_FALSE(m.IsAnimating());
    EXPECT_NEAR(m.Peek(), 1.f, 0.5f);
}

TEST(AnimationTests, TweenCompletesAndFires) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    bool          done   = false;
    Motion<float> m =
        Tween<float>(target, {.duration = 0.1f, .easing = ease::Linear, .onComplete = [&] { done = true; }});

    target = 1.f;
    for (int i = 0; i < 100 && m.IsAnimating(); ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_FALSE(m.IsAnimating());
    EXPECT_NEAR(m.Peek(), 1.f, 1e-3f);
    EXPECT_TRUE(done);
}

TEST(AnimationTests, TweenFromAnimatesStaticTargetOnMount) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    Motion<float> still = Tween<float>(1.f, {.duration = 0.1f});
    Motion<float> m     = Tween<float>(1.f, 0.f, {.duration = 0.1f, .easing = ease::Linear});
    EXPECT_EQ(still.Peek(), 1.f);
    EXPECT_EQ(m.Peek(), 0.f);
    EXPECT_TRUE(m.IsAnimating());

    for (int i = 0; i < 3; ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_NEAR(m.Peek(), 0.5f, 1e-3f);
    for (int i = 0; i < 100 && m.IsAnimating(); ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_NEAR(m.Peek(), 1.f, 1e-3f);
    EXPECT_FALSE(still.IsAnimating());
}

TEST(AnimationTests, AnimateToCancelsPrior) {
    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto x = Signal(0.f);
    AnimateTo(x, 10.f, {.duration = 1.f, .easing = ease::Linear});
    for (int i = 0; i < 10; ++i) {
        graph->Tick(1.f / 60.f);
    }
    const float mid = x.Peek();
    EXPECT_TRUE(mid > 0.f && mid < 10.f);

    AnimateTo(x, 0.f, {.duration = 0.1f, .easing = ease::Linear});
    for (int i = 0; i < 40; ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_NEAR(x.Peek(), 0.f, 1e-2f);
}

TEST(AnimationTests, ReducedMotionSnaps) {
    SetReducedMotion(true);

    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    Motion<float> m      = Spring<float>(target, spring::Stiff);
    target               = 50.f;
    graph->Tick(1.f / 60.f);
    EXPECT_FALSE(m.IsAnimating());
    EXPECT_EQ(m.Peek(), 50.f);

    SetReducedMotion(false);
}

TEST(AnimationTests, TimeScalePauses) {
    SetTimeScale(0.f);

    auto graph = MakeRef<ReactiveGraph>();
    GraphScope scope(*graph);
    Owner owner;

    auto          target = Signal(0.f);
    Motion<float> m      = Spring<float>(target, spring::Stiff);
    target               = 100.f;
    for (int i = 0; i < 30; ++i) {
        graph->Tick(1.f / 60.f);
    }
    EXPECT_EQ(m.Peek(), 0.f);

    SetTimeScale(1.f);
}

} // namespace
