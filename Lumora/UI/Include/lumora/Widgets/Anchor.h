#pragma once

#include <lumora/Ref.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Signal.h>

#include <cstdint>

namespace lumora
{
    namespace detail
    {
        /// Shared cell behind an Anchor handle: the trigger writes its (id, generation) at mount (generation-safe), the positioner
        /// reads its live solved rect each frame. A pointer anchor stores a point.
        struct AnchorState : RefCounted
        {
            NodeId        id{};
            std::uint32_t gen = 0;
            Vec2          pointer{};
            bool          isPointer = false;

            /// The trigger's live solved rect (surface space), refreshed each layout pass while bound via `.ref`; equality-gated. Read
            /// it through Anchor::Bounds()/Width()/Height() so reactive consumers re-run on resize/move.
            Signal<Rect>  rect{};
        };
    }

    /// Links a floating surface to its trigger: create one, bind it via the `.ref` prop, pass it to a Popover/Menu/Tooltip.
    /// Copies share the cell.
    class Anchor
    {
    public:
        Anchor() : m_state(MakeRef<detail::AnchorState>()) {}

        /// Point the anchor at a live node (called by the `.ref` binding at mount); `gen` is the arena generation, so a reused slot resolves to nothing.
        void Bind(NodeId id, std::uint32_t gen) const
        {
            m_state->id        = id;
            m_state->gen       = gen;
            m_state->isPointer = false;
        }

        /// Anchor to a surface-space point instead of a node (context menus open at the cursor).
        void SetPointer(Vec2 p) const
        {
            m_state->pointer   = p;
            m_state->isPointer = true;
        }

        [[nodiscard]] const Ref<detail::AnchorState>& State() const noexcept { return m_state; }

        /// The trigger's live solved rect in surface space (tracked read); zero until first laid out.
        [[nodiscard]] const Rect& Bounds() const { return m_state->rect(); }
        [[nodiscard]] float       Width() const { return m_state->rect().width; }
        [[nodiscard]] float       Height() const { return m_state->rect().height; }
        [[nodiscard]] Vec2         Size() const { return m_state->rect().Size(); }

        /// Implicitly decays to its shared cell so `.ref = anchor` binds the underlying state.
        operator const Ref<detail::AnchorState>&() const noexcept { return m_state; } // NOLINT(*-explicit-*)

    private:
        Ref<detail::AnchorState> m_state;
    };
}
