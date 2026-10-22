#pragma once

#include <lumora/Debug/DebugOverlay.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>

namespace lumora {

struct AppCtx;
struct InteractionState;
class Canvas;

/// Draws the debug overlay for one app's tree onto `canvas`, over the emitted UI. Surface-space; `surfaceSize` clamps
/// the tooltip and places the legend. Reads only, never mutates the tree.
void PaintDebugOverlay(AppCtx& ctx, NodeId root, const InteractionState& interaction, Canvas& canvas,
                       Vec2 surfaceSize, DebugOverlayFlags flags);

} // namespace lumora
