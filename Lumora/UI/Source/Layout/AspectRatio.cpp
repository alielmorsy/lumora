#include "Layout/AspectRatio.h"

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Runtime/AppCtx.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace lumora
{
    namespace
    {
        /// Below this the patch is dropped, or a fractional-pixel difference would dirty layout every frame.
        constexpr float kEpsilon = 0.5f;
    } // namespace

    bool RunAspectRatios(AppCtx& ctx)
    {
        std::vector<std::pair<NodeId, std::uint32_t>>& tracked = ctx.arena.AspectNodes();
        if (tracked.empty())
        {
            return false;
        }

        bool                                          changed = false;
        std::vector<std::pair<NodeId, std::uint32_t>> live;
        live.reserve(tracked.size());

        for (const std::pair<NodeId, std::uint32_t>& entry : tracked)
        {
            LiveNode* node = ctx.arena.TryResolve(entry.first);
            if (!node || ctx.arena.GenerationOf(entry.first) != entry.second)
            {
                continue; // unmounted since it registered; a re-mount re-registers
            }

            LayoutStyle& ls = node->style;
            if (ls.aspectRatio <= 0.f)
            {
                continue; // the rule stopped applying (a class flipped); drop the registration
            }
            live.push_back(entry);

            // The dependent axis was decided by the resolver from the declared sizes; see AspectPin.
            if (ls.aspectPin == AspectPin::None)
            {
                continue; // both axes stated: CSS ignores the ratio rather than overriding either
            }

            if (ls.aspectPin == AspectPin::Height)
            {
                const float want = node->solved.width / ls.aspectRatio;
                if (want > 0.f && std::fabs(want - ls.height.value) > kEpsilon)
                {
                    ls.height = Fixed(want);
                    node->MarkDirty(Dirty::Layout);
                    changed = true;
                }
            }
            else
            {
                const float want = node->solved.height * ls.aspectRatio;
                if (want > 0.f && std::fabs(want - ls.width.value) > kEpsilon)
                {
                    ls.width = Fixed(want);
                    node->MarkDirty(Dirty::Layout);
                    changed = true;
                }
            }
        }

        tracked = std::move(live);
        return changed;
    }
} // namespace lumora
