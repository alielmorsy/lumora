#pragma once

#include <lumora/Api.h>
#include <lumora/Elem.h>
#include <lumora/Paint/DrawTypes.h>
#include <lumora/Reactive/Reactive.h>
#include <lumora/Reactive/Str.h>
#include <lumora/Widgets/Props.h>

namespace lumora {

struct ImageCfg {
    Str                   src{};
    Reactive<ImageHandle> handle{};   ///< a GPU-registered image (RegisterExternalImage); wins over src when Valid()
    Reactive<ImageFit>    fit     = ImageFit::Contain;
    Reactive<float>       opacity = 1.f;
    Reactive<float>       radius  = 0.f;
    LayoutProps           layout{};
    ViewProps             view{};
    InteractProps         on{};
    /// Space-separated CSS class list (reactive); layout size, opacity and border-radius come from CSS when a StyleEngine is wired.
    Str                   className{};
    /// Per-node CSS declarations (`padding: 4px; color: red`); reactive. Beat stylesheet rules except `!important`.
    Str                   inlineCss{};
};

/// Image leaf. Draws `handle` if set, else decodes `src` through the app's ImageProvider. A headless
/// app (no provider) and an unresolved source draw nothing and measure empty.
LUMORA_API Elem Image(ImageCfg cfg = {});

}
