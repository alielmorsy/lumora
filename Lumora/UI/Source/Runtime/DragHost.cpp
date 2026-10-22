#include "Runtime/DragHost.h"

#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Runtime/ChildOrder.h"

#include <lumora/Reactive/Reactive.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/Text.h>

#include <cmath>
#include <utility>

namespace lumora
{
    namespace
    {
        /// The ghost sits down-right of the hot spot so the drop target under the pointer stays visible.
        constexpr float kGhostOffsetX = 12.f;
        constexpr float kGhostOffsetY = 10.f;

        /// The node's InteractState, or null if it is gone or carries none.
        [[nodiscard]] InteractState* StateOf(NodeArena& arena, const NodeRef& ref)
        {
            const NodeId id = ref.Resolve(arena);
            if (!id.Valid())
            {
                return nullptr;
            }
            return detail::InteractOf(arena, arena.Resolve(id));
        }
    } // namespace

    void DragHost::Press(NodeRef source, DragPayload payload, Vec2 at)
    {
        m_phase   = Phase::Pending;
        m_source  = source;
        m_payload = std::move(payload);
        m_start   = at;
        m_target  = NodeRef{};
    }

    bool DragHost::PassedThreshold(Vec2 now) const noexcept
    {
        const float dx = now.x - m_start.x;
        const float dy = now.y - m_start.y;
        return (dx * dx + dy * dy) >= (kThreshold * kThreshold);
    }

    void DragHost::Arm()
    {
        m_phase     = Phase::Active;
        sourceValue = m_payload.value;
        pointer     = m_start;
        overTarget  = false;
        armed       = true;
    }

    void DragHost::LeaveTarget(NodeArena& arena, InteractionState& state)
    {
        if (InteractState* on = StateOf(arena, m_target))
        {
            on->dropActive = false;
            if (on->onDragOver)
            {
                on->onDragOver(m_payload, false);
            }
        }
        if (const NodeId id = m_target.Resolve(arena); id.Valid())
        {
            WritePseudo(state, id, style::Pseudo::DropActive, false);
            arena.Resolve(id).MarkDirty(Dirty::Repaint);
        }
        m_target = NodeRef{};
    }

    void DragHost::SetTarget(NodeArena& arena, InteractionState& state, NodeRef next)
    {
        if (m_target == next)
        {
            return;
        }
        LeaveTarget(arena, state);

        m_target = next;
        if (InteractState* on = StateOf(arena, m_target))
        {
            on->dropActive = true;
            if (on->onDragOver)
            {
                on->onDragOver(m_payload, true);
            }
        }
        if (const NodeId id = m_target.Resolve(arena); id.Valid())
        {
            WritePseudo(state, id, style::Pseudo::DropActive, true);
            arena.Resolve(id).MarkDirty(Dirty::Repaint);
        }
        overTarget = m_target.Resolve(arena).Valid();
    }

    bool DragHost::Drop(NodeArena& arena, InteractionState& state, const PointerInfo& info)
    {
        // Copy the payload before Clear() resets it: onDrop may outlive this call.
        const DragPayload  payload = m_payload;
        InteractState*     on      = StateOf(arena, m_target);
        Fn<void(const DragPayload&, PointerInfo)> drop = on ? on->onDrop : nullptr;

        Clear(arena, state);

        if (!drop)
        {
            return false;
        }
        drop(payload, info);
        return true;
    }

    void DragHost::Clear(NodeArena& arena, InteractionState& state)
    {
        LeaveTarget(arena, state);
        m_phase   = Phase::Idle;
        m_payload = {};
        m_source  = NodeRef{};
        m_start   = Vec2{};

        armed       = false;
        overTarget  = false;
        sourceValue = std::string{};
    }

    Elem BuildDragOverlay(DragHost* host)
    {
        auto isArmed = Reactive<bool>([host] { return host->armed(); });
        return Show(std::move(isArmed), [host]
        {
            // Into the Drag layer: a full-surface absolute containing block, so the inset resolves in surface coords, escapes ancestor clips,
            // and is skipped by the hit-test.
            return Portal(Layer::Drag, [host]
            {
                // Built once at mount (rebuilding per pointer move is what a ghost must not do). The fallback is a plain marker, not a glyph:
                // a font lacking the glyph renders a box. Apps wanting a real ghost call SetDragPreview.
                Elem body = host->Preview() != nullptr
                                ? host->Preview()(host->Payload())
                                : Box(BoxCfg{.layout = {.width = Fixed(14.f), .height = Fixed(14.f)},
                                             .style  = {.fill   = Color::Rgba(220, 220, 220, 190),
                                                        .radius = 3.f}});

                return Box(BoxCfg{
                               .position = {.position = Position::Absolute,
                                            .insetLeft = Reactive<float>(
                                                [host] { return host->pointer().x + kGhostOffsetX; }),
                                            .insetTop = Reactive<float>(
                                                [host] { return host->pointer().y + kGhostOffsetY; })},
                               .on = {.hitThrough = true}},
                           std::move(body));
            });
        });
    }
} // namespace lumora
