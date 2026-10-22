#pragma once

#include <lumora/Dock/Dock.h>
#include <lumora/Elem.h>
#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Graph/Graph.h>
#include <lumora/Reactive/Computed.h>
#include <lumora/Reactive/Format.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/Widgets.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nodestudio {

using namespace lumora;
using namespace lumora::nodegraph;

struct NodeProps {
    std::string comment;
    std::string customTitle;
    double paramA = 1.0;
    double paramB = 0.5;
    int operation = 0;
    int mode = 0;
};

struct NodeStudioState {
    Controller graphCtrl;
    Signal<std::uint64_t> revision{0};
    Signal<GraphNodeId> selectedNode{GraphNodeId{}};
    Signal<std::string> searchQuery{""};
    std::unordered_map<std::uint32_t, NodeProps> props;

    void Touch() {
        revision = revision.Peek() + 1;
    }

    NodeProps& GetProps(GraphNodeId id) {
        return props[id.index];
    }

    [[nodiscard]] const NodeProps* FindProps(GraphNodeId id) const {
        auto it = props.find(id.index);
        return it != props.end() ? &it->second : nullptr;
    }
};

void SetupNodeTypesAndSample(NodeStudioState& state);

Elem MenuBarView(NodeStudioState& state);
Elem PaletteView(NodeStudioState& state);
Elem GraphCanvasView(NodeStudioState& state);
Elem InspectorView(NodeStudioState& state);
Elem StatusBarView(NodeStudioState& state);

extern const char* const kNodeStudioStylesheet;

} // namespace nodestudio
