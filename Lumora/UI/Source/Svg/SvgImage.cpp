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

    void SvgImage::Draw(Paint2D& surface, const Rect& box, float opacity, SvgAlign align,
                        SvgMeetOrSlice meetOrSlice) const
    {
        if (!Valid() || box.width <= 0.f || box.height <= 0.f)
        {
            return;
        }
        surface.PushClip(box);
        svg::RenderScene(surface, surface.m_canvas, m_data->scene, m_data->cache, box, align, meetOrSlice,
                         opacity, surface.m_ctx.contentScale);
        surface.PopClip();
    }
} // namespace lumora
