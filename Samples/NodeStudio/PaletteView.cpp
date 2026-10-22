#include "NodeStudio.h"

namespace nodestudio {

Elem PaletteView(NodeStudioState& state)
{
    Computed<std::vector<NodeTypeId>> filtered([&state] {
        const std::string query = state.searchQuery();
        std::vector<NodeTypeId> list;
        const auto all = state.graphCtrl.Types().All();
        for (const NodeTypeId id : all) {
            const NodeTypeDesc* d = state.graphCtrl.Types().Find(id);
            if (!d) continue;
            if (query.empty() || FuzzyScore(query, d->title) >= 0 || FuzzyScore(query, d->category) >= 0) {
                list.push_back(id);
            }
        }
        return list;
    });

    return Column({.layout = {.width = Expand, .height = Expand}, .className = "palette-panel"},
        Column({.className = "panel-header"},
            Text("Node Palette", {.className = "panel-title"}),
            Input({
                .value = state.searchQuery,
                .placeholder = "Filter nodes...",
                .className = "palette-search"
            })
        ),
        Scroll({.layout = {.width = Expand, .height = Expand}, .className = "palette-scroll"},
            Column({.className = "palette-list"},
                Each(filtered, [](const NodeTypeId& id) { return id.value; },
                     [&state](const NodeTypeId& id) {
                         const NodeTypeDesc* desc = state.graphCtrl.Types().Find(id);
                         const std::string title = desc ? desc->title : "Node";
                         const std::string category = desc ? desc->category : "General";

                         return Row({.className = "palette-item"},
                             Column({.layout = {.width = Expand}, .className = "palette-item-text"},
                                 Text(title, {.className = "palette-item-title"}),
                                 Text(category, {.className = "palette-item-category"})
                             ),
                             Button({
                                 .label = "+ Add",
                                 .onClick = [&state, id] {
                                     Vec2 center = state.graphCtrl.View().Pan() + Vec2{300.f, 200.f};
                                     static float s_offset = 0.f;
                                     s_offset += 24.f;
                                     if (s_offset > 120.f) s_offset = 0.f;
                                     center.x += s_offset;
                                     center.y += s_offset;
                                     const GraphNodeId added = state.graphCtrl.AddNode(id, center);
                                     state.graphCtrl.Select({&added, 1});
                                 },
                                 .variant = ButtonVariant::Ghost,
                                 .className = "palette-add-btn"
                             })
                         );
                     })
            )
        )
    );
}

} // namespace nodestudio
