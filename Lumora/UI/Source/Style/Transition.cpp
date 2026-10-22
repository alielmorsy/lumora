#include "Style/Transition.h"

#include "Node/Payloads.h" // StyleState, TransformState

#include <lumora/Animation/Interpolate.h> // Lerp
#include <lumora/Animation/Motion.h>      // ReducedMotion

#include <algorithm>
#include <cmath>

namespace lumora::style
{
    float TimingFn::Eval(float t) const
    {
        t = std::clamp(t, 0.f, 1.f);
        switch (kind)
        {
        case Kind::Fn: return fn ? fn(t) : t;
        case Kind::Steps:
        {
            const int n = steps > 0 ? steps : 1;
            const float s = stepPos == StepPos::Start ? std::ceil(t * static_cast<float>(n))
                                                      : std::floor(t * static_cast<float>(n));
            return std::clamp(s / static_cast<float>(n), 0.f, 1.f);
        }
        case Kind::CubicBezier:
        {
            const float x1 = bez[0], y1 = bez[1], x2 = bez[2], y2 = bez[3];
            const auto sampleX = [&](float u) {
                const float v = 1.f - u;
                return 3.f * v * v * u * x1 + 3.f * v * u * u * x2 + u * u * u;
            };
            const auto sampleY = [&](float u) {
                const float v = 1.f - u;
                return 3.f * v * v * u * y1 + 3.f * v * u * u * y2 + u * u * u;
            };
            float lo = 0.f, hi = 1.f, u = t;
            for (int i = 0; i < 24; ++i) // bisection on x(u) = t
            {
                u = 0.5f * (lo + hi);
                const float x = sampleX(u);
                if (std::fabs(x - t) < 1e-4f) break;
                if (x < t) lo = u;
                else hi = u;
            }
            return sampleY(u);
        }
        }
        return t;
    }

    namespace
    {
        [[nodiscard]] TimingFn ParseTiming(const CssValue& v)
        {
            TimingFn out;
            if (v.kind == ValueKind::Keyword)
            {
                const StringId k = v.keyword;
                if (k == "linear"_sid) { out.kind = TimingFn::Kind::Fn; out.fn = ease::Linear; }
                else if (k == "ease"_sid) { out.kind = TimingFn::Kind::CubicBezier; out.bez[0] = 0.25f; out.bez[1] = 0.1f; out.bez[2] = 0.25f; out.bez[3] = 1.f; }
                else if (k == "ease-in"_sid) { out.kind = TimingFn::Kind::CubicBezier; out.bez[0] = 0.42f; out.bez[1] = 0.f; out.bez[2] = 1.f; out.bez[3] = 1.f; }
                else if (k == "ease-out"_sid) { out.kind = TimingFn::Kind::CubicBezier; out.bez[0] = 0.f; out.bez[1] = 0.f; out.bez[2] = 0.58f; out.bez[3] = 1.f; }
                else if (k == "ease-in-out"_sid) { out.kind = TimingFn::Kind::CubicBezier; out.bez[0] = 0.42f; out.bez[1] = 0.f; out.bez[2] = 0.58f; out.bez[3] = 1.f; }
                else if (k == "step-start"_sid) { out.kind = TimingFn::Kind::Steps; out.steps = 1; out.stepPos = TimingFn::StepPos::Start; }
                else if (k == "step-end"_sid) { out.kind = TimingFn::Kind::Steps; out.steps = 1; out.stepPos = TimingFn::StepPos::End; }
            }
            else if (v.kind == ValueKind::Function)
            {
                if (v.keyword == "cubic-bezier"_sid && v.list.size() >= 4)
                {
                    out.kind = TimingFn::Kind::CubicBezier;
                    for (int i = 0; i < 4; ++i) out.bez[i] = v.list[static_cast<size_t>(i)].num;
                }
                else if (v.keyword == "steps"_sid && !v.list.empty())
                {
                    out.kind = TimingFn::Kind::Steps;
                    out.steps = static_cast<int>(v.list[0].num);
                    out.stepPos = TimingFn::StepPos::End;
                    if (v.list.size() >= 2 && v.list[1].kind == ValueKind::Keyword)
                    {
                        const StringId p = v.list[1].keyword;
                        if (p == "jump-start"_sid || p == "start"_sid) out.stepPos = TimingFn::StepPos::Start;
                    }
                }
            }
            return out;
        }

        void ApplyGroup(const CssValue& group, TransitionRuntime& rt)
        {
            // A group is `<prop> <dur> [<timing>] [<delay>]`; collect its parts.
            StringId prop{};
            float times[2] = {0.f, 0.f};
            int timeCount = 0;
            TimingFn timing{};
            bool haveTiming = false;

            const auto handle = [&](const CssValue& c) {
                if (c.kind == ValueKind::Time)
                {
                    if (timeCount < 2) times[timeCount++] = c.AsSeconds();
                }
                else if (c.kind == ValueKind::Function &&
                         (c.keyword == "cubic-bezier"_sid || c.keyword == "steps"_sid))
                {
                    timing = ParseTiming(c);
                    haveTiming = true;
                }
                else if (c.kind == ValueKind::Keyword)
                {
                    const StringId k = c.keyword;
                    if (k == "linear"_sid || k == "ease"_sid || k == "ease-in"_sid || k == "ease-out"_sid ||
                        k == "ease-in-out"_sid || k == "step-start"_sid || k == "step-end"_sid)
                    {
                        timing = ParseTiming(c);
                        haveTiming = true;
                    }
                    else prop = k; // a property name (background-color / opacity / transform / all / …)
                }
            };

            if (group.kind == ValueKind::List)
                for (const CssValue& c : group.list) handle(c);
            else handle(group);

            TransitionSpec spec;
            spec.enabled = true;
            spec.duration = times[0];
            spec.delay = timeCount >= 2 ? times[1] : 0.f;
            spec.timing = haveTiming ? timing : TimingFn{};

            const auto set = [&](TransitionSpec& s) { s = spec; };
            if (prop == "all"_sid || prop == StringId{})
            {
                set(rt.fillSpec);
                set(rt.opacitySpec);
                set(rt.radiusSpec);
                set(rt.transformSpec);
            }
            else if (prop == "background-color"_sid || prop == "background"_sid) set(rt.fillSpec);
            else if (prop == "opacity"_sid) set(rt.opacitySpec);
            else if (prop == "border-radius"_sid) set(rt.radiusSpec);
            else if (prop == "transform"_sid) set(rt.transformSpec);
        }
    } // namespace

    void ParseTransitions(const CssValue& transition, TransitionRuntime& rt)
    {
        rt.fillSpec.enabled = rt.opacitySpec.enabled = rt.radiusSpec.enabled = rt.transformSpec.enabled = false;
        if (!transition.Valid()) return;
        if (transition.kind == ValueKind::Keyword && transition.keyword == "none"_sid) return;

        if (transition.kind == ValueKind::List && transition.comma)
            for (const CssValue& group : transition.list) ApplyGroup(group, rt);
        else
            ApplyGroup(transition, rt);
    }

    namespace
    {
        // Retarget a scalar run when its target changed; returns the value to display this frame.
        void InterceptScalar(detail::ScalarTrans& r, const TransitionSpec& spec, float& field)
        {
            const float target = field;
            if (!r.init) { r.displayed = target; r.init = true; return; }
            if (!spec.enabled) { r.displayed = target; r.active = false; return; }
            const float prevTarget = r.active ? r.to : r.displayed;
            if (target != prevTarget)
            {
                r.from = r.displayed;
                r.to = target;
                r.elapsed = 0.f;
                r.active = true;
            }
            field = r.displayed;
        }

        void InterceptColor(detail::ColorTrans& r, const TransitionSpec& spec, Color& field)
        {
            const Color target = field;
            if (!r.init) { r.displayed = target; r.init = true; return; }
            if (!spec.enabled) { r.displayed = target; r.active = false; return; }
            const Color prevTarget = r.active ? r.to : r.displayed;
            if (!(target == prevTarget))
            {
                r.from = r.displayed;
                r.to = target;
                r.elapsed = 0.f;
                r.active = true;
            }
            field = r.displayed;
        }

        [[nodiscard]] AdvanceResult AdvanceScalar(detail::ScalarTrans& r, const TransitionSpec& spec, float& field,
                                                  float dt)
        {
            if (!r.active) return {};
            if (ReducedMotion() || spec.duration <= 0.f)
            {
                r.displayed = r.to;
                r.active = false;
                field = r.displayed;
                return {true, false}; // snapped to the target: written, and done
            }
            r.elapsed += dt;
            const float local = r.elapsed - spec.delay;
            if (local < 0.f) { field = r.displayed; return {false, true}; } // waiting out the delay
            if (local >= spec.duration)
            {
                r.displayed = r.to;
                r.active = false;
                field = r.displayed;
                return {true, false}; // the final step still writes
            }
            r.displayed = r.from + (r.to - r.from) * spec.timing.Eval(local / spec.duration);
            field = r.displayed;
            return {true, true};
        }

        [[nodiscard]] AdvanceResult AdvanceColor(detail::ColorTrans& r, const TransitionSpec& spec, Color& field,
                                                 float dt)
        {
            if (!r.active) return {};
            if (ReducedMotion() || spec.duration <= 0.f)
            {
                r.displayed = r.to;
                r.active = false;
                field = r.displayed;
                return {true, false};
            }
            r.elapsed += dt;
            const float local = r.elapsed - spec.delay;
            if (local < 0.f) { field = r.displayed; return {false, true}; }
            if (local >= spec.duration)
            {
                r.displayed = r.to;
                r.active = false;
                field = r.displayed;
                return {true, false};
            }
            r.displayed = Lerp(r.from, r.to, spec.timing.Eval(local / spec.duration));
            field = r.displayed;
            return {true, true};
        }
    } // namespace

    void InterceptBox(TransitionRuntime& rt, StyleState& ss, TransformState& ts)
    {
        InterceptColor(rt.fill, rt.fillSpec, ss.fill);
        InterceptScalar(rt.opacity, rt.opacitySpec, ss.opacity);
        InterceptScalar(rt.radius, rt.radiusSpec, ss.radius);
        InterceptScalar(rt.transX, rt.transformSpec, ts.x);
        InterceptScalar(rt.transY, rt.transformSpec, ts.y);
        InterceptScalar(rt.scaleX, rt.transformSpec, ts.scaleX);
        InterceptScalar(rt.scaleY, rt.transformSpec, ts.scaleY);
        InterceptScalar(rt.rotate, rt.transformSpec, ts.rotate);
    }

    AdvanceResult AdvanceBox(TransitionRuntime& rt, StyleState& ss, TransformState& ts, float dt)
    {
        AdvanceResult result;
        result |= AdvanceColor(rt.fill, rt.fillSpec, ss.fill, dt);
        result |= AdvanceScalar(rt.opacity, rt.opacitySpec, ss.opacity, dt);
        result |= AdvanceScalar(rt.radius, rt.radiusSpec, ss.radius, dt);
        result |= AdvanceScalar(rt.transX, rt.transformSpec, ts.x, dt);
        result |= AdvanceScalar(rt.transY, rt.transformSpec, ts.y, dt);
        result |= AdvanceScalar(rt.scaleX, rt.transformSpec, ts.scaleX, dt);
        result |= AdvanceScalar(rt.scaleY, rt.transformSpec, ts.scaleY, dt);
        result |= AdvanceScalar(rt.rotate, rt.transformSpec, ts.rotate, dt);
        return result;
    }

    bool AnyActive(const TransitionRuntime& rt) noexcept
    {
        return rt.fill.active || rt.opacity.active || rt.radius.active || rt.transX.active || rt.transY.active ||
               rt.scaleX.active || rt.scaleY.active || rt.rotate.active;
    }
} // namespace lumora::style
