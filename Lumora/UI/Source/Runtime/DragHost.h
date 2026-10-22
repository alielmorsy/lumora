#pragma once

#include <lumora/Elem.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Input/DragDrop.h>
#include <lumora/Reactive/Signal.h>

#include "Input/Interaction.h"

#include <cstdint>
#include <functional>
#include <string>

namespace lumora
{
    class NodeArena;

    /// Per-app drag state, reached via `InteractionState::drag`. A press stays a *click* (`Pending`: payload known, nothing armed,
    /// no ghost) until the pointer travels, or every selection click in a list would become a drag. The reactive cells drive the UI; the rest is written only by the dispatcher.
    class DragHost
    {
    public:
        enum class Phase : std::uint8_t { Idle, Pending, Active };

        /// How far the pointer must travel before a press becomes a drag (logical px).
        static constexpr float kThreshold = 4.f;

        Signal<bool>        armed{false};       ///< a ghost-worthy drag is in flight
        Signal<Vec2>        pointer{};          ///< cursor in surface px, while armed
        Signal<std::string> sourceValue{};      ///< the active payload's value, for the source's own dim
        Signal<bool>        overTarget{false};  ///< the cursor is over a node that accepts the payload

        /// Draws a dragged payload; one builder per app so a given kind looks the same whichever node started it. Unset draws a small default marker.
        void SetPreview(std::function<Elem(const DragPayload&)> builder) { m_preview = std::move(builder); }

        /// A press landed on a drag source: record what it would carry. Not armed yet.
        void Press(NodeRef source, DragPayload payload, Vec2 at);

        /// True once the pointer has moved far enough from the press to mean a drag.
        [[nodiscard]] bool PassedThreshold(Vec2 now) const noexcept;

        /// Promotes Pending -> Active, publishing `armed`; the ghost mounts on the next Tick (structural edits are deferred).
        void Arm();

        void SetPointer(Vec2 at) { pointer = at; }

        /// Move the drop target, firing onDragOver(false) on the old one and onDragOver(true) on the new,
        /// and writing `:drop-active` on both. Idempotent for an unchanged target.
        void SetTarget(NodeArena& arena, InteractionState& state, NodeRef next);

        /// Delivers onDrop to the current target, then clears. False when there was no target under the cursor.
        bool Drop(NodeArena& arena, InteractionState& state, const PointerInfo& info);

        /// Clear without dropping: Escape, a source that vanished, or a press that never armed.
        void Clear(NodeArena& arena, InteractionState& state);

        [[nodiscard]] Phase              CurrentPhase() const noexcept { return m_phase; }
        [[nodiscard]] bool               Pending() const noexcept { return m_phase == Phase::Pending; }
        [[nodiscard]] bool               Armed() const noexcept { return m_phase == Phase::Active; }
        [[nodiscard]] const DragPayload& Payload() const noexcept { return m_payload; }
        [[nodiscard]] NodeRef            Source() const noexcept { return m_source; }
        [[nodiscard]] const std::function<Elem(const DragPayload&)>& Preview() const noexcept { return m_preview; }

    private:
        /// Clear `:drop-active` and fire onDragOver(false) on whatever is currently targeted.
        void LeaveTarget(NodeArena& arena, InteractionState& state);

        Phase       m_phase = Phase::Idle;
        DragPayload m_payload{};
        NodeRef     m_source{};
        NodeRef     m_target{};
        Vec2        m_start{};

        /// Outlives any one drag, so it is not cleared with the payload.
        std::function<Elem(const DragPayload&)> m_preview{};
    };

    /// The ghost: mounted into the Drag layer while `armed`, at the cursor, pointer-transparent. The layer is also excluded from the
    /// hit chain during a drag, because `hitThrough` still tests children, which here are whatever the caller's preview built.
    [[nodiscard]] Elem BuildDragOverlay(DragHost* host);
} // namespace lumora
