#include "NodeStudio.h"
#include <SampleHost.h>

namespace nodestudio {

class NodeStudioApp {
public:
    NodeStudioApp()
    {
        SetupNodeTypesAndSample(m_state);
    }

    Elem Render()
    {
        return Column({.className = "app"},
            MenuBarView(m_state),
            Row({.layout = {.width = Expand, .height = Expand}, .className = "body"},
                PaletteView(m_state),
                GraphCanvasView(m_state),
                InspectorView(m_state)
            ),
            StatusBarView(m_state)
        );
    }

private:
    NodeStudioState m_state;
};

} // namespace nodestudio

int lumora::platform::Main(const lumora::platform::LaunchArgs& args)
{
    return lumora::samples::RunSample({
        .title  = "NodeStudio",
        .width  = 1280,
        .height = 800,
        .root   = [] { return lumora::Elem(nodestudio::NodeStudioApp{}); },
        .css    = nodestudio::kNodeStudioStylesheet
    }, args.argc, args.argv);
}
