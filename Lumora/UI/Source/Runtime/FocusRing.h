#pragma once

namespace lumora
{
    struct AppCtx;
    struct InteractionState;
    class Canvas;

    /// Draws the focus-visible ring around the focused node, only when focus arrived by keyboard (`InteractionState::focusVisible`).
    /// Per-kind corner radius comes from Widget::FocusRingShape.
    void PaintFocusRing(AppCtx& ctx, const InteractionState& interaction, Canvas& canvas);
} // namespace lumora
