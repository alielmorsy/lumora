#include "Style/StyleEngine.h"

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Reactive/ReactiveGraph.h"

#include <lumora/Reactive/Effect.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Style/Css.h>

#include <algorithm>

namespace lumora::style
{
    void StyleEngine::SetStylesheet(Stylesheet sheet)
    {
        m_sheet = std::move(sheet);
        m_index = BuildIndex(m_sheet);
        m_rootVars.clear();
        for (const Declaration& d : m_sheet.rootVars) m_rootVars[d.prop] = d.value; // last wins
        RecomputeMediaState();
        m_generation = m_generation.Peek() + 1; // equality-gated write; flushes resolver effects
    }

    void StyleEngine::RecomputeMediaState()
    {
        const Vec2 vp = m_viewport.Peek();
        std::uint64_t bits = 0;
        const size_t n = m_sheet.media.size();
        for (size_t i = 0; i < n && i < 64; ++i)
        {
            bool pass = true;
            for (const MediaFeature& f : m_sheet.media[i].condition.features)
            {
                switch (f.type)
                {
                case MediaFeature::Type::MinWidth: pass = pass && vp.x >= f.value; break;
                case MediaFeature::Type::MaxWidth: pass = pass && vp.x <= f.value; break;
                case MediaFeature::Type::MinHeight: pass = pass && vp.y >= f.value; break;
                case MediaFeature::Type::MaxHeight: pass = pass && vp.y <= f.value; break;
                }
            }
            if (pass) bits |= (std::uint64_t{1} << i);
        }
        m_mediaState = bits; // equality-gated: only notifies when a breakpoint is crossed
    }

    void StyleEngine::SetViewport(Vec2 size)
    {
        m_viewport = size;      // equality-gated (Vec2 has operator==)
        RecomputeMediaState();
    }

    std::vector<const Rule*> StyleEngine::PassingMediaRules() const
    {
        std::vector<const Rule*> out;
        const std::uint64_t bits = m_mediaState.Peek();
        const size_t n = m_sheet.media.size();
        for (size_t i = 0; i < n && i < 64; ++i)
        {
            if (((bits >> i) & 1u) == 0) continue;
            for (const Rule& r : m_sheet.media[i].rules) out.push_back(&r);
        }
        return out;
    }

    const InheritedStyle* StyleEngine::ParentInherited(NodeArena& arena, NodeId id)
    {
        // Nearest ancestor that resolves style, not just the parent: Dock, Expanded, Splitter, Painter never wire a resolver,
        // so their default `inheritedOut` would erase the var map for the whole subtree.
        NodeId cursor = arena.Resolve(id).parent;
        while (cursor.Valid())
        {
            if (CssNodeState* st = TryNodeState(cursor); st != nullptr && st->participates)
            {
                (void)st->inheritEpoch.Read(); // tracked: subscribe the caller to that ancestor's inherited output
                return &st->inheritedOut;
            }
            cursor = arena.Resolve(cursor).parent;
        }
        return nullptr; // nothing above resolves style: the caller falls back to the root var map
    }

    void StyleEngine::LoadSource(std::string_view css) { SetStylesheet(ParseStylesheet(css)); }

    CssNodeState& StyleEngine::NodeState(NodeId id)
    {
        return m_nodes.try_emplace(id.value).first->second;
    }

    CssNodeState* StyleEngine::TryNodeState(NodeId id) noexcept
    {
        auto it = m_nodes.find(id.value);
        return it == m_nodes.end() ? nullptr : &it->second;
    }

    void StyleEngine::ForgetNode(NodeId id)
    {
        std::erase_if(m_animating, [id](const AnimatedNode& node) { return node.id == id; });
        m_nodes.erase(id.value);
    }

    void StyleEngine::Enrol(NodeId id, std::uint32_t gen, AdvanceFn advance)
    {
        if (!advance || !id.Valid())
        {
            return;
        }
        m_animating.push_back(AnimatedNode{id, gen, advance});
    }

    void StyleEngine::AttachDriver(ReactiveGraph& graph, Owner& owner, NodeArena& arena)
    {
        GraphScope ambient(graph);
        Owner::Scope scoped(owner);
        OnFrame([this, &arena](float dt) { AdvanceAnimations(arena, dt); });
    }

    const KeyframesDef* StyleEngine::KeyframesFor(CssNodeState& state)
    {
        if (!state.motion)
        {
            return nullptr;
        }
        AnimationRuntime& anim = state.motion->anim;
        if (!anim.spec.enabled)
        {
            return nullptr;
        }
        const std::uint64_t generation = m_generation.Peek();
        if (!anim.resolvedValid || anim.resolvedGen != generation || !(anim.resolvedName == anim.spec.name))
        {
            const auto it = m_sheet.keyframes.find(anim.spec.name);
            anim.resolved = it != m_sheet.keyframes.end() ? &it->second : nullptr;
            anim.resolvedName = anim.spec.name;
            anim.resolvedGen = generation;
            anim.resolvedValid = true;
        }
        return anim.resolved;
    }

    void StyleEngine::AdvanceAnimations(NodeArena& arena, float dt)
    {
        // Swap-erase in place while walking: a finished run withdraws immediately.
        for (std::size_t i = 0; i < m_animating.size();)
        {
            const AnimatedNode node = m_animating[i];
            CssNodeState* state = TryNodeState(node.id);
            bool keep = false;
            if (state && arena.GenerationOf(node.id) == node.gen)
            {
                keep = node.advance(arena, node.id, *state, KeyframesFor(*state), dt);
            }
            if (keep)
            {
                ++i;
                continue;
            }
            if (state && state->motion)
            {
                state->motion->transitions.driverRegistered = false; // re-enrollable by the next resolve
            }
            m_animating[i] = m_animating.back();
            m_animating.pop_back();
        }
    }

    void StyleEngine::SetPseudo(NodeId id, Pseudo p, bool on)
    {
        CssNodeState* st = TryNodeState(id);
        if (!st) return; // unstyled node: no rule can test its pseudo-state
        if (m_counters) ++m_counters->pseudoWrites;
        const auto bit = static_cast<std::uint8_t>(p);
        const std::uint8_t mask = st->pseudo.Peek();
        st->pseudo.Write(static_cast<std::uint8_t>(on ? mask | bit : mask & ~bit));
    }

    MatchOps StyleEngine::MakeOps(NodeArena& arena)
    {
        NodeArena* a = &arena;
        StyleEngine* self = this;
        MatchOps ops;
        ops.parent = [a, self](NodeHandle h) -> NodeHandle {
            const NodeId p = a->Resolve(NodeId{h}).parent;
            if (p.Valid()) (void)self->NodeState(p).classEpoch.Read(); // subscribe to ancestor class changes
            return p.value;
        };
        ops.classes = [self](NodeHandle h) -> const std::vector<StringId>& {
            return self->NodeState(NodeId{h}).classes;
        };
        ops.pseudo = [self](NodeHandle h) -> PseudoMask {
            CssNodeState* st = self->TryNodeState(NodeId{h});
            if (!st) return 0;
            return st->pseudo.Read();
        };
        ops.isRoot = [a](NodeHandle h) -> bool { return !a->Resolve(NodeId{h}).parent.Valid(); };
        return ops;
    }
} // namespace lumora::style
