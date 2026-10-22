#include "NodeStudio.h"

namespace nodestudio {

Elem InspectorView(NodeStudioState& state)
{
    return Column({.layout = {.width = Expand, .height = Expand}, .className = "inspector-panel"},
        Column({.className = "panel-header"},
            Text("Node Inspector", {.className = "panel-title"})
        ),
        Scroll({.layout = {.width = Expand, .height = Expand}, .className = "inspector-scroll"},
            Show([&state] {
                const GraphNodeId id = state.selectedNode();
                return id.Valid() && state.graphCtrl.Data().Alive(id);
            },
            [&state] {
                const GraphNodeId id = state.selectedNode();
                const Node* node = state.graphCtrl.Data().Get(id);
                const NodeTypeDesc* desc = node ? state.graphCtrl.Types().Find(node->type) : nullptr;
                const std::string typeTitle = desc ? desc->title : "Unknown Type";
                NodeProps& props = state.GetProps(id);

                Signal<std::string> titleSignal{node ? (node->title.empty() ? typeTitle : node->title) : ""};
                Signal<std::string> commentSignal{props.comment};
                Signal<double> widthSignal{node ? (node->width > 0.f ? static_cast<double>(node->width) : 150.0) : 150.0};
                Signal<double> paramASignal{props.paramA};
                Signal<double> paramBSignal{props.paramB};
                Signal<int> opSignal{props.operation};
                Signal<int> modeSignal{props.mode};

                return Column({.className = "inspector-content"},
                    Row({.className = "inspector-badge-row"},
                        Text(typeTitle, {.className = "inspector-badge"}),
                        Spacer(),
                        Text(Format("#{} (gen {})", id.index, id.gen), {.className = "inspector-dim"})
                    ),

                    Column({.className = "prop-group"},
                        Text("Node Title", {.className = "prop-label"}),
                        Input({
                            .value = titleSignal,
                            .placeholder = "Enter custom title...",
                            .onChange = [&state, id](std::string val) {
                                state.graphCtrl.SetNodeTitle(id, val);
                                state.Touch();
                            },
                            .className = "prop-input"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Node Width", {.className = "prop-label"}),
                        NumberField({
                            .value = widthSignal,
                            .min = 120.0,
                            .max = 400.0,
                            .step = 10.0,
                            .decimals = 0,
                            .label = "Width",
                            .onChange = [&state, id](double val) {
                                state.graphCtrl.SetNodeWidth(id, static_cast<float>(val));
                                state.Touch();
                            },
                            .className = "prop-number"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Operation", {.className = "prop-label"}),
                        ComboBox({
                            .options = {"Add", "Subtract", "Multiply", "Divide", "Power", "Min", "Max"},
                            .selected = opSignal,
                            .onChange = [&state, id](int picked) {
                                state.GetProps(id).operation = picked;
                                state.Touch();
                            },
                            .stretch = true,
                            .className = "prop-combo"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Blend Mode", {.className = "prop-label"}),
                        ComboBox({
                            .options = {"Normal", "Multiply", "Screen", "Overlay", "Linear Dodge"},
                            .selected = modeSignal,
                            .onChange = [&state, id](int picked) {
                                state.GetProps(id).mode = picked;
                                state.Touch();
                            },
                            .stretch = true,
                            .className = "prop-combo"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Parameter A", {.className = "prop-label"}),
                        NumberField({
                            .value = paramASignal,
                            .step = 0.1,
                            .decimals = 3,
                            .label = "Param A",
                            .onChange = [&state, id](double val) {
                                state.GetProps(id).paramA = val;
                                state.Touch();
                            },
                            .className = "prop-number"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Parameter B", {.className = "prop-label"}),
                        NumberField({
                            .value = paramBSignal,
                            .step = 0.1,
                            .decimals = 3,
                            .label = "Param B",
                            .onChange = [&state, id](double val) {
                                state.GetProps(id).paramB = val;
                                state.Touch();
                            },
                            .className = "prop-number"
                        })
                    ),

                    Column({.className = "prop-group"},
                        Text("Note / Comment", {.className = "prop-label"}),
                        Input({
                            .value = commentSignal,
                            .placeholder = "Optional notes...",
                            .onChange = [&state, id](std::string val) {
                                state.GetProps(id).comment = std::move(val);
                            },
                            .className = "prop-input"
                        })
                    ),

                    Divider({.margin = Spacing::Symmetric(12.f, 0.f)}),

                    Column({.className = "inspector-actions"},
                        Button({
                            .label = "Center in View",
                            .onClick = [&state, id] {
                                const Node* n = state.graphCtrl.Data().Get(id);
                                if (n) {
                                    state.graphCtrl.View().SetPan(n->position - Vec2{300.f, 200.f});
                                    state.Touch();
                                }
                            },
                            .variant = ButtonVariant::Outline,
                            .className = "action-btn"
                        }),
                        Button({
                            .label = "Duplicate Node",
                            .onClick = [&state, id] {
                                const auto dupes = state.graphCtrl.Duplicate({&id, 1}, Vec2{30.f, 30.f});
                                if (!dupes.empty()) state.graphCtrl.Select(dupes);
                            },
                            .variant = ButtonVariant::Outline,
                            .className = "action-btn"
                        }),
                        Button({
                            .label = "Delete Node",
                            .onClick = [&state, id] {
                                state.graphCtrl.RemoveNode(id);
                            },
                            .variant = ButtonVariant::Outline,
                            .className = "delete-btn"
                        })
                    )
                );
            },
            [] {
                return Column({.className = "inspector-empty"},
                    Text("No Node Selected", {.className = "empty-title"}),
                    Text("Select a node on the canvas to inspect its fields and parameters.",
                         {.className = "empty-desc"})
                );
            })
        )
    );
}

} // namespace nodestudio
