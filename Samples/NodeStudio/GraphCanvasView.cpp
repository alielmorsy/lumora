#include "NodeStudio.h"

namespace nodestudio {

Elem GraphCanvasView(NodeStudioState& state)
{
    ViewCfg cfg;
    cfg.frameVerbs = true;
    cfg.showGrid = true;
    return Column({.layout = {.width = Expand, .height = Expand}, .className = "canvas-wrapper"},
        NodeGraph(state.graphCtrl, std::move(cfg))
    );
}

} // namespace nodestudio
