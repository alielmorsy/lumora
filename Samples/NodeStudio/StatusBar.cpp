#include "NodeStudio.h"

#include <format>

namespace nodestudio {

Elem StatusBarView(NodeStudioState& state)
{
    Computed<int> nodeCount([&state] {
        state.revision();
        int count = 0;
        const auto& data = state.graphCtrl.Data();
        for (std::uint32_t i = 0; i < data.SlotCount(); ++i) {
            if (data.Alive(data.At(i))) ++count;
        }
        return count;
    });

    Computed<int> linkCount([&state] {
        state.revision();
        int count = 0;
        const auto& data = state.graphCtrl.Data();
        for (std::uint32_t i = 0; i < data.LinkSlotCount(); ++i) {
            if (data.Alive(data.LinkAt(i))) ++count;
        }
        return count;
    });

    Computed<std::string> selectionText([&state] {
        state.revision();
        const auto sel = state.graphCtrl.Selection();
        if (sel.empty()) return std::string("No selection");
        if (sel.size() == 1) {
            const GraphNodeId id = sel.front();
            const Node* n = state.graphCtrl.Data().Get(id);
            if (n) {
                const NodeTypeDesc* d = state.graphCtrl.Types().Find(n->type);
                const std::string name = n->title.empty() ? (d ? d->title : "Node") : n->title;
                return std::format("Selected: {} (#{})", name, id.index);
            }
        }
        return std::format("{} nodes selected", sel.size());
    });

    Computed<float> zoomPct([&state] {
        state.revision();
        return state.graphCtrl.View().Zoom() * 100.f;
    });

    return Row({.className = "statusbar"},
        Row({.className = "status-section"},
            Text(Format("{} nodes", nodeCount), {.className = "status-item"}),
            Text("•", {.className = "status-dim"}),
            Text(Format("{} links", linkCount), {.className = "status-item"}),
            Text("•", {.className = "status-dim"}),
            Text(selectionText, {.className = "status-item"})
        ),
        Spacer(),
        Row({.className = "status-section"},
            Text("RMB: Add/Delete Menu", {.className = "status-hint"}),
            Text("•", {.className = "status-dim"}),
            Text(Format("Zoom: {:.0f}%", zoomPct), {.className = "status-item"})
        )
    );
}

} // namespace nodestudio
