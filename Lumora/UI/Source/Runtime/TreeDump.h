#pragma once

#include <lumora/Foundation/NodeId.h>

namespace lumora {

class NodeArena;

/// Debug helper: logs the live tree under `root`, one indented line per node (kind, id, solved rect, flex info, dirty bits).
/// No-op for a null root.
void LogLiveTree(const NodeArena& arena, NodeId root);

} // namespace lumora
