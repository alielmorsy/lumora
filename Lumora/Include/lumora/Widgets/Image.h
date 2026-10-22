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
    ImageFit              fit     = ImageFit::Contain;
    Reactive<float>       opacity = 1.f;
    Reactive<float>       radius  = 0.f;
    LayoutProps           layout{};
    ViewProps             view{};
    InteractProps         on{};
};

/// Image leaf. Draws `handle` if set, else decodes `src` through the app's ImageProvider. A headless
/// app (no provider) and an unresolved source draw nothing and measure empty.
LUMORA_API Elem Image(ImageCfg cfg = {});

}
