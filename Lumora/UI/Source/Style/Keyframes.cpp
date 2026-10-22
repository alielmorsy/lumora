#include "Style/Keyframes.h"

#include "Node/Payloads.h" // StyleState, TransformState
#include "Style/Convert.h" // ToTransform

#include <lumora/Animation/Interpolate.h> // Lerp
#include <lumora/Animation/Motion.h>      // ReducedMotion

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace lumora::style
{
    void ParseAnimation(const CssValue& animation, AnimationRuntime& rt)
    {
        AnimationSpec spec;
        auto group = [&](const CssValue& g) {
            int timeCount = 0;
            const auto handle = [&](const CssValue& c) {
                if (c.kind == ValueKind::Time)
                {
                    if (timeCount == 0) spec.duration = c.AsSeconds();
                    else if (timeCount == 1) spec.delay = c.AsSeconds();
                    ++timeCount;
                }
                else if (c.kind == ValueKind::Number) spec.iterations = c.num;
                else if (c.kind == ValueKind::Function && c.keyword == "cubic-bezier"_sid && c.list.size() >= 4)
                {
                    spec.timing.kind = TimingFn::Kind::CubicBezier;
                    for (int i = 0; i < 4; ++i) spec.timing.bez[i] = c.list[static_cast<size_t>(i)].num;
                }
                else if (c.kind == ValueKind::Function && c.keyword == "steps"_sid && !c.list.empty())
                {
                    spec.timing.kind = TimingFn::Kind::Steps;
                    spec.timing.steps = static_cast<int>(c.list[0].num);
                    spec.timing.stepPos = TimingFn::StepPos::End;
                    if (c.list.size() >= 2 && c.list[1].kind == ValueKind::Keyword &&
                        (c.list[1].keyword == "jump-start"_sid || c.list[1].keyword == "start"_sid))
                        spec.timing.stepPos = TimingFn::StepPos::Start;
                }
                else if (c.kind == ValueKind::Keyword)
                {
                    const StringId k = c.keyword;
                    if (k == "infinite"_sid) spec.iterations = -1.f;
                    else if (k == "linear"_sid) { spec.timing.kind = TimingFn::Kind::Fn; spec.timing.fn = ease::Linear; }
                    else if (k == "ease"_sid) { spec.timing.kind = TimingFn::Kind::CubicBezier; spec.timing.bez[0] = 0.25f; spec.timing.bez[1] = 0.1f; spec.timing.bez[2] = 0.25f; spec.timing.bez[3] = 1.f; }
                    else if (k == "ease-in"_sid) { spec.timing.kind = TimingFn::Kind::CubicBezier; spec.timing.bez[0] = 0.42f; spec.timing.bez[1] = 0.f; spec.timing.bez[2] = 1.f; spec.timing.bez[3] = 1.f; }
                    else if (k == "ease-out"_sid) { spec.timing.kind = TimingFn::Kind::CubicBezier; spec.timing.bez[0] = 0.f; spec.timing.bez[1] = 0.f; spec.timing.bez[2] = 0.58f; spec.timing.bez[3] = 1.f; }
                    else if (k == "ease-in-out"_sid) { spec.timing.kind = TimingFn::Kind::CubicBezier; spec.timing.bez[0] = 0.42f; spec.timing.bez[1] = 0.f; spec.timing.bez[2] = 0.58f; spec.timing.bez[3] = 1.f; }
                    else if (k == "normal"_sid) spec.direction = AnimationSpec::Direction::Normal;
                    else if (k == "reverse"_sid) spec.direction = AnimationSpec::Direction::Reverse;
                    else if (k == "alternate"_sid) spec.direction = AnimationSpec::Direction::Alternate;
                    else if (k == "alternate-reverse"_sid) spec.direction = AnimationSpec::Direction::AlternateReverse;
                    else if (k == "forwards"_sid) spec.fill = AnimationSpec::Fill::Forwards;
                    else if (k == "backwards"_sid) spec.fill = AnimationSpec::Fill::Backwards;
                    else if (k == "both"_sid) spec.fill = AnimationSpec::Fill::Both;
                    else if (k == "paused"_sid) spec.paused = true;
                    else if (k == "running"_sid) spec.paused = false;
                    else spec.name = k;
                }
            };
            if (g.kind == ValueKind::List)
                for (const CssValue& c : g.list) handle(c);
            else handle(g);
        };

        if (animation.Valid() && !(animation.kind == ValueKind::Keyword && animation.keyword == "none"_sid))
        {
            if (animation.kind == ValueKind::List && animation.comma && !animation.list.empty())
                group(animation.list.front()); // first animation only in v1
            else group(animation);
        }

        spec.enabled = spec.name != StringId{} && spec.duration > 0.f;

        if (spec.enabled && !(rt.spec.name == spec.name && rt.wasEnabled))
            rt.elapsed = 0.f; // (re)start when the animation newly appears or its name changes
        rt.spec = spec;
        rt.wasEnabled = spec.enabled;
    }

    namespace
    {
        struct Stop
        {
            float off;
            const CssValue* val;
        };

        [[nodiscard]] std::vector<Stop> Collect(const KeyframesDef& kf, StringId a, StringId b)
        {
            std::vector<Stop> out;
            for (const KeyframeStop& s : kf.stops)
                for (const Declaration& d : s.declarations)
                    if (d.prop == a || (b != StringId{} && d.prop == b))
                    {
                        out.push_back({s.offset, &d.value});
                        break;
                    }
            std::sort(out.begin(), out.end(), [](const Stop& x, const Stop& y) { return x.off < y.off; });
            return out;
        }

        // Interpolate a scalar (px/number) property across the keyframe stops at phase t.
        [[nodiscard]] std::optional<float> SampleScalar(const KeyframesDef& kf, StringId prop, float t,
                                                        const TimingFn& timing)
        {
            const std::vector<Stop> stops = Collect(kf, prop, StringId{});
            if (stops.empty()) return std::nullopt;
            const auto num = [](const CssValue& v) {
                return v.kind == ValueKind::Length || v.kind == ValueKind::Number ? v.num : 0.f;
            };
            if (t <= stops.front().off) return num(*stops.front().val);
            if (t >= stops.back().off) return num(*stops.back().val);
            for (size_t i = 0; i + 1 < stops.size(); ++i)
                if (t >= stops[i].off && t <= stops[i + 1].off)
                {
                    const float span = stops[i + 1].off - stops[i].off;
                    const float seg = span > 0.f ? (t - stops[i].off) / span : 0.f;
                    const float e = timing.Eval(seg);
                    return num(*stops[i].val) + (num(*stops[i + 1].val) - num(*stops[i].val)) * e;
                }
            return num(*stops.back().val);
        }

        [[nodiscard]] std::optional<Color> SampleColor(const KeyframesDef& kf, StringId a, StringId b, float t,
                                                       const TimingFn& timing)
        {
            const std::vector<Stop> stops = Collect(kf, a, b);
            if (stops.empty()) return std::nullopt;
            const auto col = [](const CssValue& v) { return v.kind == ValueKind::Color ? v.color : Color{}; };
            if (t <= stops.front().off) return col(*stops.front().val);
            if (t >= stops.back().off) return col(*stops.back().val);
            for (size_t i = 0; i + 1 < stops.size(); ++i)
                if (t >= stops[i].off && t <= stops[i + 1].off)
                {
                    const float span = stops[i + 1].off - stops[i].off;
                    const float seg = span > 0.f ? (t - stops[i].off) / span : 0.f;
                    return Lerp(col(*stops[i].val), col(*stops[i + 1].val), timing.Eval(seg));
                }
            return col(*stops.back().val);
        }

        // `transform` keyframe value at phase t, interpolated per component.
        [[nodiscard]] std::optional<TransformValue> SampleTransform(const KeyframesDef& kf, float t,
                                                                    const TimingFn& timing)
        {
            const std::vector<Stop> stops = Collect(kf, "transform"_sid, StringId{});
            if (stops.empty()) return std::nullopt;
            if (t <= stops.front().off) return ToTransform(*stops.front().val);
            for (size_t i = 0; i + 1 < stops.size(); ++i)
                if (t >= stops[i].off && t <= stops[i + 1].off)
                {
                    const float span = stops[i + 1].off - stops[i].off;
                    const float e = timing.Eval(span > 0.f ? (t - stops[i].off) / span : 0.f);
                    const TransformValue a = ToTransform(*stops[i].val), b = ToTransform(*stops[i + 1].val);
                    const auto mix = [e](float p, float q) { return p + (q - p) * e; };
                    return TransformValue{mix(a.x, b.x), mix(a.y, b.y), mix(a.scaleX, b.scaleX),
                                          mix(a.scaleY, b.scaleY), mix(a.rotate, b.rotate)};
                }
            return ToTransform(*stops.back().val);
        }

        void SampleInto(const KeyframesDef& kf, float p, const TimingFn& timing, StyleState& ss, TransformState& ts)
        {
            if (auto c = SampleColor(kf, "background-color"_sid, "background"_sid, p, timing)) ss.fill = *c;
            if (auto o = SampleScalar(kf, "opacity"_sid, p, timing)) ss.opacity = *o;
            if (auto r = SampleScalar(kf, "border-radius"_sid, p, timing)) ss.radius = *r;
            if (const auto tr = SampleTransform(kf, p, timing))
            {
                ts.x = tr->x;
                ts.y = tr->y;
                ts.scaleX = tr->scaleX;
                ts.scaleY = tr->scaleY;
                ts.rotate = tr->rotate;
            }
        }
    } // namespace

    AdvanceResult AdvanceAnimation(AnimationRuntime& rt, const KeyframesDef& kf, StyleState& ss, TransformState& ts,
                                   float dt)
    {
        const AnimationSpec& spec = rt.spec;
        if (!spec.enabled) return {};
        if (ReducedMotion())
        {
            SampleInto(kf, 1.f, spec.timing, ss, ts); // snap to the final keyframe, no motion
            return {true, false};
        }
        if (!spec.paused) rt.elapsed += dt;

        const float dur = spec.duration > 0.f ? spec.duration : 0.0001f;
        const float t0 = rt.elapsed - spec.delay;

        if (t0 < 0.f)
        {
            const bool backfilled = spec.fill == AnimationSpec::Fill::Backwards ||
                spec.fill == AnimationSpec::Fill::Both;
            if (backfilled) SampleInto(kf, 0.f, spec.timing, ss, ts);
            return {backfilled, true}; // waiting out the delay
        }

        const float iterF = t0 / dur;
        const bool finished = spec.iterations >= 0.f && iterF >= spec.iterations;

        const auto directed = [&](int iterIdx, float local) {
            const bool rev = spec.direction == AnimationSpec::Direction::Reverse ||
                (spec.direction == AnimationSpec::Direction::Alternate && (iterIdx & 1)) ||
                (spec.direction == AnimationSpec::Direction::AlternateReverse && ((iterIdx & 1) == 0));
            return rev ? 1.f - local : local;
        };

        if (finished)
        {
            const bool holds = spec.fill == AnimationSpec::Fill::Forwards ||
                spec.fill == AnimationSpec::Fill::Both;
            if (holds)
            {
                const int lastIter = std::max(0, static_cast<int>(std::ceil(spec.iterations)) - 1);
                SampleInto(kf, directed(lastIter, 1.f), spec.timing, ss, ts);
            }
            return {holds, false}; // done - but the forwards-fill sample still needs painting
        }

        const int iterIdx = static_cast<int>(std::floor(iterF));
        const float local = iterF - static_cast<float>(iterIdx);
        SampleInto(kf, directed(iterIdx, local), spec.timing, ss, ts);
        return {true, true};
    }
} // namespace lumora::style
