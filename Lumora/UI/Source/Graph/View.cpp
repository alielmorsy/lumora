#include "Graph/Input.h"
#include "Graph/Paint.h"
#include "Graph/State.h"

#include <lumora/Widgets/Widgets.h>

namespace lumora::nodegraph
{
    namespace
    {
        /// The inline text editor: an Input in a Popover-layer portal pinned over the field's surface rect, painted through
        /// `contentScale` so text, caret and selection scale with the camera.
        [[nodiscard]] Elem TextEditor(Ref<State> state)
        {
            return Show(state->editOpen, [state]
            {
                return Portal(Layer::Popover, [state]
                {
                    const Theme& theme = state->theme;

                    BoxCfg box;
                    box.position.position = Position::Fixed;
                    box.position.insetLeft = [state] { return state->editRect().x; };
                    box.position.insetTop = [state] { return state->editRect().y; };
                    box.layout.width = [state] { return Fixed(state->editRect().width / state->editZoom()); };
                    box.layout.height = [state] { return Fixed(state->editRect().height / state->editZoom()); };
                    box.contentScale = [state] { return state->editZoom(); };

                    InputCfg input{.value = state->editValue,
                                   .color = theme.Text,
                                   // The editor closes before its callback runs, so the blur that
                                   // follows an Enter or Escape finds nothing open and does nothing.
                                   .onSubmit = [state](std::string text)
                                   {
                                       if (state->textEdit.active)
                                       {
                                           state->editValue = std::move(text);
                                       }
                                       CommitText(*state);
                                   },
                                   .onCancel = [state] { CancelText(*state); },
                                   .onBlur = [state](std::string text)
                                   {
                                       if (state->textEdit.active)
                                       {
                                           state->editValue = std::move(text);
                                       }
                                       CommitText(*state);
                                   },
                                   .autofocus = true,
                                   .selectOnFocus = true,
                                   .layout = {.width = Expand,
                                              .height = Expand,
                                              .padding = Spacing::Symmetric(0.f, 4.f)},
                                   .style = {.fill = theme.FieldBg,
                                             .radius = 3.f,
                                             .border = {1.f, theme.NodeSelected}}};
                    return Box(std::move(box), Input(std::move(input)));
                });
            });
        }
    } // namespace

    Elem NodeGraph(Controller& controller, ViewCfg cfg)
    {
        // detail::Access, not controller.m_state: Access is the one friend seam every State consumer uses.
        State& state = detail::Access(controller);
        state.theme = cfg.theme;
        state.cfg = cfg;
        state.Invalidate();

        // cfg is captured BY VALUE: it is immutable after mount, and Ref<T> is intrusive, so sharing would force a refcount
        // base onto the public ViewCfg aggregate.
        Controller* owner = &controller;

        PainterCfg painter;
        painter.width = Expand;
        painter.height = Expand;
        painter.repaint = controller.Revision();
        painter.onPaint = [owner, cfg](Paint2D& surface)
        { PaintGraph(detail::Access(*owner), surface, cfg); };

        painter.on.focusPolicy = FocusPolicy::Click;
        // A canvas pans on middle-drag, so it opts in to the middle button. Dispatch drops middle
        // presses for every node that does not.
        painter.on.middleButton = true;

        painter.on.onPress = [owner](PointerInfo info)
        {
            OnPress(*owner, info.position + detail::Access(*owner).surfaceToLayer, info.modifiers,
                    info.button == static_cast<int>(PointerButton::Middle));
        };

        painter.on.onMove = [owner](PointerInfo info)
        {
            OnMove(*owner, info.position + detail::Access(*owner).surfaceToLayer, info.delta,
                   info.modifiers);
        };

        painter.on.onRelease = [owner](PointerInfo info)
        { OnRelease(*owner, info.position + detail::Access(*owner).surfaceToLayer, info.modifiers); };

        painter.on.onScroll = [owner](float notches) { OnScroll(*owner, notches); };

        painter.on.onDoubleClick = [owner]
        { OnDoubleClick(*owner, detail::Access(*owner).lastPointerLayer); };

        painter.on.onKeyDown = [owner](KeyInfo key) { return OnKey(*owner, key); };

        painter.on.onTextInput = [owner](std::string_view text) { return OnText(*owner, text); };

        painter.on.onContextMenu = [owner, cfg]
        { return BuildMenu(*owner, cfg, detail::Access(*owner).lastPointerLayer); };

        if (cfg.dropAccepts)
        {
            painter.on.dropAccepts = [owner](const DragPayload& payload) { return DropAccepts(*owner, payload); };
            painter.on.onDrop = [owner](const DragPayload& payload, PointerInfo info)
            { OnDrop(*owner, payload, info.position + detail::Access(*owner).surfaceToLayer); };
        }

        return Box({.layout = {.width = Expand, .height = Expand}}, Painter(std::move(painter)),
                   TextEditor(Ref<State>(&state)));
    }
} // namespace lumora::nodegraph
