#pragma once

#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Widgets/Props.h>

#include <algorithm>

namespace lumora {

/// Map an image of natural size `img` into content rect `content` per `fit`, returning the dst rect
/// for Canvas::DrawImage. Cover/None may exceed `content` (the caller clips). A non-positive `img`
/// (unresolved) returns `content` unchanged.
[[nodiscard]] inline Rect ImageFitRect(ImageFit fit, const Rect& content, Vec2 img) {
    if (img.x <= 0.f || img.y <= 0.f) {
        return content;
    }
    const auto centered = [&](float w, float h) -> Rect {
        return {content.x + (content.width - w) * 0.5f, content.y + (content.height - h) * 0.5f, w, h};
    };
    switch (fit) {
        case ImageFit::Fill:
            return content;
        case ImageFit::None:
            return centered(img.x, img.y);
        case ImageFit::Contain: {
            const float s = std::min(content.width / img.x, content.height / img.y);
            return centered(img.x * s, img.y * s);
        }
        case ImageFit::Cover: {
            const float s = std::max(content.width / img.x, content.height / img.y);
            return centered(img.x * s, img.y * s);
        }
    }
    return content;
}

/// Whether `fit` can place pixels outside the content rect, so Paint must clip to it.
[[nodiscard]] inline bool ImageFitClips(ImageFit fit) noexcept {
    return fit == ImageFit::Cover || fit == ImageFit::None;
}

} // namespace lumora
