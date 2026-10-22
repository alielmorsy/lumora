#pragma once

#include <lumora/Paint/Paint2D.h>

#include "Graph/State.h"

namespace lumora::nodegraph
{
    /// Draw one graph frame. Writes `state.viewport` and `state.surfaceToLayer`; refreshes layout if the revision moved.
    void PaintGraph(State& state, Paint2D& painter, const ViewCfg& cfg);
} // namespace lumora::nodegraph
