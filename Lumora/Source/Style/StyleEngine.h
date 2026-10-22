#pragma once

#include "Style/Cascade.h"
#include "Style/CssNodeState.h"
#include "Style/Matcher.h"

#include <lumora/Debug/FrameCounters.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Style/Stylesheet.h>

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lumora
{
    class NodeArena;
    class ReactiveGraph;
    class Owner;
}

namespace lumora::style
{
    /// Per-app owner of the parsed stylesheet + rule index, the hot-reload generation signal and the per-node state
    /// registry. Resolver effects subscribe to `Generation()`.
    class StyleEngine
    {
    public:
        /// Replace the sheet, rebuild the index, bump the generation (call under a GraphScope so resolver effects flush).
        /// Does not merge: pass the fully merged sheet.
        void SetStylesheet(Stylesheet sheet);

        void LoadSource(std::string_view css);

        [[nodiscard]] const Stylesheet& Sheet() const noexcept { return m_sheet; }
        [[nodiscard]] const StyleIndex& Index() const noexcept { return m_index; }
        [[nodiscard]] const Signal<std::uint64_t>& Generation() const noexcept { return m_generation; }
        [[nodiscard]] const Signal<Vec2>& Viewport() const noexcept { return m_viewport; }
        [[nodiscard]] const Signal<std::uint64_t>& MediaState() const noexcept { return m_mediaState; }
        [[nodiscard]] float RootFontSize() const noexcept { return m_rootFontSize; }
        [[nodiscard]] const VarMap& RootVars() const noexcept { return m_rootVars; }

        /// Update the viewport and the @media breakpoint bitmask (equality-gated; call at the top of Layout).
        void SetViewport(Vec2 size);

        /// Rules of every @media group whose condition currently passes, in source order.
        [[nodiscard]] std::vector<const Rule*> PassingMediaRules() const;

        /// InheritedStyle of the nearest ancestor that resolves style (reads its inheritEpoch, tracked); resolver-less ancestors
        /// (Dock, Expanded, Splitter, Painter, Svg) are skipped. Null means use the root var map.
        [[nodiscard]] const InheritedStyle* ParentInherited(NodeArena& arena, NodeId id);

        /// Per-node state, created on first touch; references stay valid across insertions.
        [[nodiscard]] CssNodeState& NodeState(NodeId id);
        [[nodiscard]] CssNodeState* TryNodeState(NodeId id) noexcept;
        void ForgetNode(NodeId id);

        /// Write a pseudo-state signal (no-op for nodes without CSS state; equality-gated).
        void SetPseudo(NodeId id, Pseudo p, bool on);

        /// Attach the app's per-frame counter block (`cascadeCalls`); null outside a MountedApp.
        void SetCounters(FrameCounters* counters) noexcept { m_counters = counters; }
        [[nodiscard]] FrameCounters* Counters() const noexcept { return m_counters; }

        /// Advances one enrolled node. A plain function pointer (payload and member pointers are template arguments at the enrol
        /// site) so an animating node allocates nothing. Returns whether to stay enrolled.
        using AdvanceFn = bool (*)(NodeArena&, NodeId, CssNodeState&, const KeyframesDef*, float);

        /// Enrol a node with the single animation driver; idempotent via `driverRegistered`.
        void Enrol(NodeId id, std::uint32_t gen, AdvanceFn advance);

        /// Register the one animation driver on `graph`, owned by `owner`. Exactly one call per app; a host that builds an AppCtx
        /// by hand must call it too or nothing animates.
        void AttachDriver(ReactiveGraph& graph, Owner& owner, NodeArena& arena);

        /// Advance every enrolled node and withdraw finished ones; public for hosts with their own frame loop.
        void AdvanceAnimations(NodeArena& arena, float dt);

        /// Nodes currently being driven.
        [[nodiscard]] std::size_t AnimatingCount() const noexcept { return m_animating.size(); }

        /// Build the matcher callbacks. `parent` reads the ancestor's classEpoch (tracked); `classes` reads plainly (a node
        /// re-resolves via its own className).
        [[nodiscard]] MatchOps MakeOps(NodeArena& arena);

    private:
        void RecomputeMediaState();

        /// The node's @keyframes block, resolved once per (name, sheet generation) and cached on its
        /// AnimationRuntime instead of looked up by string every frame.
        [[nodiscard]] const KeyframesDef* KeyframesFor(CssNodeState& state);

        struct AnimatedNode
        {
            NodeId id{};
            std::uint32_t gen = 0; ///< arena generation, so a recycled slot is never mistaken for this node
            AdvanceFn advance = nullptr;
        };

        Stylesheet m_sheet;
        StyleIndex m_index;
        Signal<std::uint64_t> m_generation{0ull};
        Signal<Vec2> m_viewport{Vec2{}};
        Signal<std::uint64_t> m_mediaState{0ull};
        float m_rootFontSize = 16.f;
        VarMap m_rootVars;
        std::unordered_map<std::uint32_t, std::unique_ptr<CssNodeState>> m_nodes;
        std::vector<AnimatedNode> m_animating;
        FrameCounters* m_counters = nullptr;
    };
} // namespace lumora::style
