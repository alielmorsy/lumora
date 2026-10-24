#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>
#include <lumora/Widgets/TextSelection.h>

#include <cstdint>

namespace lumora {

/// `size` and `color` are promoted to the top level; typeface and flow controls stay grouped.
struct TextCfg {
    Reactive<float> size  = 14.f;
    Reactive<Color> color = Color::Black;
    FontProps       font{};
    TextFlowProps   flow{};
    LayoutProps     layout{};
    InteractProps   on{};
    Str                   className{};
    /// Per-node CSS declarations (`padding: 4px; color: red`); reactive. Beat stylesheet rules except `!important`.
    Str                   inlineCss{};

    /// Opt this leaf into an enclosing TextSelectionScope: the scope drives the gesture and the leaf paints its share.
    /// `selectionKey` is the leaf's reading-order position across the scope (a row index for a list), not a node id, so it
    /// survives unmounted rows.
    Ref<TextSelection> selection{};
    std::uint64_t      selectionKey = 0;
};

/// Text leaf; the content is a Str (literal, std::string, signal, computed or lambda) and patches when its source changes.
LUMORA_API Elem Text(Str content, TextCfg cfg = {});

}
