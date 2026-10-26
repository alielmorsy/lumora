#include <lumora/Paint/SvgImage.h>

#include "Runtime/Paint.h"
#include "Svg/SvgDocument.h"
#include "Svg/SvgRenderer.h"
#include "Svg/SvgScene.h"

#include <lumora/Paint/Paint2D.h>

#include <string>

namespace lumora
{
    namespace detail
    {
        class SvgImageData final : public RefCounted
        {
        public:
            svg::SvgScene           scene;
            mutable svg::SceneCache cache; // shared memo, rebuilt on scale change; single UI thread
        };
    } // namespace detail

    SvgImage::SvgImage()                                     = default;
    SvgImage::SvgImage(const SvgImage&)                      = default;
    SvgImage::SvgImage(SvgImage&&) noexcept                  = default;
    SvgImage& SvgImage::operator=(const SvgImage&)           = default;
    SvgImage& SvgImage::operator=(SvgImage&&) noexcept       = default;
    SvgImage::~SvgImage()                                    = default;

    SvgImage SvgImage::FromFile(std::string_view path, AssetLoader loader)
    {
        if (!loader)
        {
            loader = &DefaultFileLoad;
        }
        std::string bytes;
        if (path.empty() || !loader(path, bytes))
        {
            return {};
        }
        return FromText(bytes);
    }

    SvgImage SvgImage::FromText(std::string_view svgText)
    {
        auto data = MakeRef<detail::SvgImageData>();
        data->scene = svg::ParseSvg(svgText);
        if (!data->scene.valid)
        {
            return {};
        }
        SvgImage image;
        image.m_data = std::move(data);
        return image;
    }

    bool SvgImage::Valid() const noexcept
    {
        return m_data && m_data->scene.valid;
    }

    Vec2 SvgImage::Intrinsic() const noexcept
    {
        return Valid() ? m_data->scene.intrinsic : Vec2{};
    }

    Rect SvgImage::ViewBox() const noexcept
    {
        if (!Valid())
        {
            return {};
        }
        const svg::SvgScene& sc = m_data->scene;
        if (sc.viewBoxSize.x > 0.f && sc.viewBoxSize.y > 0.f)
        {
            return {sc.viewBoxMin.x, sc.viewBoxMin.y, sc.viewBoxSize.x, sc.viewBoxSize.y};
        }
        return {0.f, 0.f, sc.intrinsic.x, sc.intrinsic.y};
    }

    void SvgImage::Draw(Paint2D& surface, const Rect& box, float opacity, SvgAlign align,
                        SvgMeetOrSlice meetOrSlice) const
    {
        Draw(surface.m_canvas, box, opacity, surface.m_ctx.contentScale, align, meetOrSlice);
    }

    void SvgImage::Draw(Canvas& canvas, const Rect& box, float opacity, float contentScale, SvgAlign align,
                        SvgMeetOrSlice meetOrSlice) const
    {
        if (!Valid() || box.width <= 0.f || box.height <= 0.f)
        {
            return;
        }
        canvas.PushClip(box);
        svg::RenderScene(canvas, m_data->scene, m_data->cache, box, align, meetOrSlice, opacity, contentScale);
        canvas.PopClip();
    }
} // namespace lumora
