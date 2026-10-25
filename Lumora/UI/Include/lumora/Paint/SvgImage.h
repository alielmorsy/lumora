#pragma once

#include <lumora/Api.h>
#include <lumora/Ref.h>
#include <lumora/AssetLoader.h>
#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Widgets/SvgTypes.h>

#include <string_view>

namespace lumora
{
    class Canvas;
    class Paint2D;

    namespace detail
    {
        class SvgImageData;
    }

    /// A parsed SVG replayable into a rect of a Paint2D surface from a Painter onPaint. Copies share the scene and geometry
    /// cache; the cache rebuilds only when the scale bucket changes.
    class LUMORA_API SvgImage
    {
    public:
        // Special members are out-of-line: Ref<SvgImageData> needs the complete type to release.
        SvgImage();
        SvgImage(const SvgImage&);
        SvgImage(SvgImage&&) noexcept;
        SvgImage& operator=(const SvgImage&);
        SvgImage& operator=(SvgImage&&) noexcept;
        ~SvgImage();

        [[nodiscard]] static SvgImage FromFile(std::string_view path, AssetLoader loader = &DefaultFileLoad);
        [[nodiscard]] static SvgImage FromText(std::string_view svgText);

        [[nodiscard]] bool Valid() const noexcept;
        [[nodiscard]] Vec2 Intrinsic() const noexcept; ///< root width/height attrs, else viewBox size

        [[nodiscard]] Rect ViewBox() const noexcept; ///< root viewBox (x, y, w, h), else {0, 0, Intrinsic()}

        /// Replay into `box` (Paint2D layer-local pixels), clipped to it; `opacity` multiplies the document opacity.
        void Draw(Paint2D& surface, const Rect& box, float opacity = 1.f,
                  SvgAlign align = SvgAlign::XMidYMid,
                  SvgMeetOrSlice meetOrSlice = SvgMeetOrSlice::Meet) const;

        /// Same replay onto a raw Canvas; `contentScale` is the DPI scale used to pick tessellation density.
        void Draw(Canvas& canvas, const Rect& box, float opacity = 1.f, float contentScale = 1.f,
                  SvgAlign align = SvgAlign::XMidYMid,
                  SvgMeetOrSlice meetOrSlice = SvgMeetOrSlice::Meet) const;

    private:
        Ref<detail::SvgImageData> m_data;
    };
}
