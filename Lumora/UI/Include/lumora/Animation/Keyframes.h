#pragma once

#include <lumora/Animation/Easing.h>
#include <lumora/Animation/Motion.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace lumora {

/// A keyframe stop; `at` is in [0,1] along the track.
template<typename T>
struct Keyframe {
    float at;
    T     value;
};

/// `yoyo` (with loop) reverses every other pass.
struct KeyframesConfig {
    float    duration = 1.f;
    EasingFn easing   = ease::Linear;
    float    delay    = 0.f;
    bool     loop     = false;
    bool     yoyo     = false;
};

/// Run `action` once, `at` seconds after the timeline starts.
struct TimelineStep {
    float                 at;
    std::function<void()> action;
};

namespace detail {

template<typename T>
struct KeyframeTrack final : RefCounted {
    std::vector<Keyframe<T>> frames;
    float                    elapsed = 0.f;
};

struct TimelineRunner final : RefCounted {
    std::vector<TimelineStep> steps;
    float                     elapsed = 0.f;
    std::size_t               next    = 0;
};

template<typename T>
[[nodiscard]] inline T SampleKeyframes(const std::vector<Keyframe<T>>& frames, EasingFn easing, float p) {
    if (p <= frames.front().at) {
        return frames.front().value;
    }
    if (p >= frames.back().at) {
        return frames.back().value;
    }
    for (std::size_t i = 1; i < frames.size(); ++i) {
        if (p <= frames[i].at) {
            const float span = frames[i].at - frames[i - 1].at;
            const float localT = span > 0.f ? (p - frames[i - 1].at) / span : 0.f;
            const float eased  = easing ? easing(localT) : localT;
            return Lerp(frames[i - 1].value, frames[i].value, eased);
        }
    }
    return frames.back().value;
}

}

/// Plays `frames` (ordered by `at`) over `duration`, optionally looping/ping-ponging.
template<Animatable T>
[[nodiscard]] Motion<T> Keyframes(std::vector<Keyframe<T>> frames, KeyframesConfig cfg = {}) {
    using A = AnimTraits<T>;

    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("Keyframes");
    }

    if (frames.empty()) {
        return Motion<T>{};
    }

    const T   seed  = frames.front().value;
    auto      state = MakeRef<detail::MotionState<T>>();
    state->pos      = A::ToVec(seed);
    state->idle     = false;

    auto      track = MakeRef<detail::KeyframeTrack<T>>();
    track->frames   = std::move(frames);
    Signal<T> out{seed};

    OnFrame([state, track, out, cfg](float dt) {
        if (state->idle) {
            return;
        }
        if (ReducedMotion()) {
            const T end = track->frames.back().value;
            state->pos  = A::ToVec(end);
            state->idle = true;
            out         = end;
            return;
        }
        track->elapsed += detail::FrameDelta(dt);
        const float local = track->elapsed - cfg.delay;
        if (local < 0.f) {
            return;
        }

        const float raw = cfg.duration > 0.f ? local / cfg.duration : 1.f;
        float       p   = raw;
        if (cfg.loop) {
            float cycle = std::floor(raw);
            p           = raw - cycle;
            if (cfg.yoyo && (static_cast<int>(cycle) & 1)) {
                p = 1.f - p;
            }
        } else if (raw >= 1.f) {
            const T end = track->frames.back().value;
            state->pos  = A::ToVec(end);
            state->idle = true;
            out         = end;
            return;
        }

        const T value = detail::SampleKeyframes(track->frames, cfg.easing, p);
        state->pos    = A::ToVec(value);
        out           = value;
    });

    return Motion<T>{std::move(out), std::move(state)};
}

/// Fire each step's action once at its time. Must run under an Owner; torn down when it disposes.
inline void Timeline(std::vector<TimelineStep> steps) {
    if (!Owner::Current()) {
        detail::WarnAnimationNoOwner("Timeline");
        return;
    }
    auto runner = MakeRef<detail::TimelineRunner>();
    runner->steps = std::move(steps);
    std::sort(runner->steps.begin(), runner->steps.end(),
              [](const TimelineStep& a, const TimelineStep& b) { return a.at < b.at; });

    OnFrame([runner](float dt) {
        if (runner->next >= runner->steps.size()) {
            return;
        }
        runner->elapsed += detail::FrameDelta(dt);
        while (runner->next < runner->steps.size() && runner->elapsed >= runner->steps[runner->next].at) {
            if (runner->steps[runner->next].action) {
                runner->steps[runner->next].action();
            }
            ++runner->next;
        }
    });
}

inline void Delay(float seconds, std::function<void()> action) {
    Timeline({TimelineStep{seconds, std::move(action)}});
}

/// Per-index stagger delay in seconds: `index * gap`.
[[nodiscard]] inline float Stagger(int index, float gap) noexcept {
    return static_cast<float>(index) * gap;
}

}
