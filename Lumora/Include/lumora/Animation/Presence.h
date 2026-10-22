#pragma once

#include <lumora/Animation/Keyframes.h>
#include <lumora/Animation/Motion.h>
#include <lumora/Animation/Spring.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Reactive/Tracking.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>

#include <concepts>
#include <type_traits>
#include <utility>

namespace lumora
{
    /// Hidden-state look; the spring interpolates from here to shown (offset {0,0}, opacity 1). `from` is a
    /// paint-only slide offset (the wrapper stays in flow). `delay` staggers the enter.
    struct PresenceConfig
    {
        Vec2 from{};
        float fromOpacity = 0.f;
        SpringConfig spring{};
        float delay = 0.f;
    };

    /// Mount/unmount with animation: the body unmounts only after the exit settles; re-showing mid-exit reverses.
    template <typename Builder>
        requires std::invocable<Builder> && std::convertible_to<std::invoke_result_t<Builder>, Elem>
    [[nodiscard]] Elem Presence(Reactive<bool> visible, Builder content, PresenceConfig cfg = {})
    {
        if (!Owner::Current())
        {
            detail::WarnAnimationNoOwner("Presence");
        }

        // `keep` gates the inner Show (shown or still exiting); `present` is the animate direction.
        // Both live in the calling scope so they survive remounts.
        Signal<bool> keep{false};
        Signal<bool> present{false};

        Effect([visible = std::move(visible), keep, present]
        {
            const bool v = visible.Get();
            Untracked([&]
            {
                if (v)
                {
                    keep = true;
                    present = true;
                }
                else
                {
                    present = false; // keep stays true until the exit settles
                }
            });
        });

        auto body = [content = std::move(content), present, keep, cfg]() -> Elem
        {
            // Enter delay (stagger): hold the hidden target until `started` flips.
            Signal<bool> started{cfg.delay <= 0.f};
            if (cfg.delay > 0.f)
            {
                Delay(cfg.delay, [started] { started = true; });
            }

            const float hiddenOpacity = cfg.fromOpacity;
            Motion<float> op = Spring<float>(
                [present, started, hiddenOpacity] { return (present() && started()) ? 1.f : hiddenOpacity; },
                hiddenOpacity, cfg.spring);

            BoxCfg box;
            box.style.opacity = op;

            Motion<float> mx;
            Motion<float> my;
            const bool slide = cfg.from.x != 0.f || cfg.from.y != 0.f;
            if (slide)
            {
                const float hx = cfg.from.x;
                const float hy = cfg.from.y;
                mx = Spring<float>([present, started, hx] { return (present() && started()) ? 0.f : hx; }, hx,
                                   cfg.spring);
                my = Spring<float>([present, started, hy] { return (present() && started()) ? 0.f : hy; }, hy,
                                   cfg.spring);
                // Paint-only translate: the body stays in flow so flex centring survives the slide.
                box.transform.x = mx;
                box.transform.y = my;
            }

            OnFrame([present, keep, op, mx, my, slide](float)
            {
                if (present.Peek())
                {
                    return;
                }
                if (op.IsAnimating())
                {
                    return;
                }
                if (slide && (mx.IsAnimating() || my.IsAnimating()))
                {
                    return;
                }
                keep = false;
            });

            return Box(std::move(box), content());
        };

        return Show(Reactive<bool>{keep}, std::move(body));
    }
}
