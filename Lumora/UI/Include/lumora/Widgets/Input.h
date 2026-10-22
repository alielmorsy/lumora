#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Bind.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/ContextMenu.h>
#include <lumora/Widgets/Props.h>

#include <functional>
#include <string>
#include <vector>

namespace lumora {

struct InputCfg {
    Bind<std::string>                value;
    Reactive<Color>                  color = Color::Black; ///< text + caret colour (placeholder is a faded tint)
    Str                              placeholder{};
    std::function<void(std::string)> onChange{};
    std::function<void(std::string)> onSubmit{};
    std::function<void()>            onCancel{}; ///< Escape pressed while focused - discard the edit
    /// Focus left the field; fires with the current text after FocusLost is applied, so a composed editor can commit and close.
    std::function<void(std::string)> onBlur{};
    /// Right-click menu builder. Unset: built-in Cut/Copy/Paste/Select All; an empty list disables the menu; items replace the default.
    std::function<std::vector<ContextMenuItem>()> contextMenu{};
    bool                             multiLine = false;
    /// Reactive: greys, drops focus and stops accepting input without a rebuild. A plain `bool` binds as a static prop.
    Reactive<bool>                   disabled  = false;
    FocusPolicy                      focusPolicy = FocusPolicy::Tab; ///< keyboard focus; None opts out, Click skips Tab
    int                              tabIndex  = 0;
    bool                             autofocus = false;
    bool                             selectOnFocus = false;
    LayoutProps                      layout{.padding = Spacing::Symmetric(8.f, 10.f)};
    StyleProps                       style{.radius = 4.f, .border = {1.f, Color::Gray}};
    /// Space-separated CSS class list (reactive). When the app has a stylesheet, CSS drives
    /// fill/border/radius/color/padding for the field; otherwise the inline props above apply.
    Str                              className{};
};

/// Text input editor (caret, selection, clipboard, undo/redo). Single-line by default; set multiLine to allow newlines.
LUMORA_API Elem Input(InputCfg cfg);

}
