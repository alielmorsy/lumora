#include "NodeStudio.h"

namespace nodestudio {

Elem MenuBarView(NodeStudioState& state)
{
    return Row({.className = "menubar"},
        Row({.className = "brand"},
            Text("NodeStudio", {.className = "brand-title"}),
            Text("Shader Graph", {.className = "brand-sub"})
        ),
        Spacer(20.f),
        Row({.className = "menu-actions"},
            Button({
                .label = "Undo",
                .onClick = [&state] {
                    state.graphCtrl.Undo();
                    state.Touch();
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            }),
            Button({
                .label = "Redo",
                .onClick = [&state] {
                    state.graphCtrl.Redo();
                    state.Touch();
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            }),
            Button({
                .label = "Fit All",
                .onClick = [&state] {
                    state.graphCtrl.FitAll();
                    state.Touch();
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            }),
            Button({
                .label = "Fit Selection",
                .onClick = [&state] {
                    state.graphCtrl.FitSelection();
                    state.Touch();
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            }),
            Button({
                .label = "Delete",
                .onClick = [&state] {
                    state.graphCtrl.RemoveSelection();
                    state.Touch();
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            }),
            Button({
                .label = "Reset Graph",
                .onClick = [&state] {
                    SetupNodeTypesAndSample(state);
                },
                .variant = ButtonVariant::Ghost,
                .className = "menu-btn"
            })
        ),
        Spacer(),
        Row({.className = "menu-right"},
            Text("Lumora Node Graph Editor", {.className = "menu-hint"})
        )
    );
}

} // namespace nodestudio
