#include <lumora/Text/FontSystem.h>

#include "GlyphAtlas.h"

#include <lumora/log/Log.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb.h>

#include <SheenBidi/SheenBidi.h>

#include <linebreak.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

LUMORA_LOG_CATEGORY("Lumora.Text")

namespace lumora
{
    namespace
    {
        [[nodiscard]] int PixelSize(float size) noexcept
        {
            return std::max(1, static_cast<int>(std::lround(size)));
        }

        [[nodiscard]] std::uint64_t GlyphKey(std::uint32_t faceIndex, std::uint32_t glyphIndex, int px) noexcept
        {
            return (static_cast<std::uint64_t>(faceIndex) << 40) |
                (static_cast<std::uint64_t>(px & 0xFFFF) << 24) | (glyphIndex & 0xFFFFFF);
        }

        [[nodiscard]] float AlignOffset(Align a, float lineWidth, float blockWidth) noexcept
        {
            switch (a)
            {
            case Align::Center: return std::max(0.f, (blockWidth - lineWidth) * 0.5f);
            case Align::End: return std::max(0.f, blockWidth - lineWidth);
            default: return 0.f;
            }
        }

        /// Decode UTF-8 into code points plus a code-point→byte map. `bytes` ends up size out.size()+1:
        /// bytes[k] is the start byte of code point k and bytes.back() is the total byte count.
        void DecodeUtf8(std::string_view s, std::vector<std::uint32_t>& out, std::vector<std::size_t>& bytes)
        {
            out.clear();
            bytes.clear();
            out.reserve(s.size());
            bytes.reserve(s.size() + 1);
            const std::size_t n = s.size();
            for (std::size_t i = 0; i < n;)
            {
                const std::size_t start = i;
                const auto c = static_cast<unsigned char>(s[i]);
                std::uint32_t cp = 0;
                int extra = 0;
                if (c < 0x80)
                {
                    cp = c;
                    extra = 0;
                }
                else if ((c >> 5) == 0x6)
                {
                    cp = c & 0x1F;
                    extra = 1;
                }
                else if ((c >> 4) == 0xE)
                {
                    cp = c & 0x0F;
                    extra = 2;
                }
                else if ((c >> 3) == 0x1E)
                {
                    cp = c & 0x07;
                    extra = 3;
                }
                else
                {
                    out.push_back(0xFFFD);
                    bytes.push_back(start);
                    ++i;
                    continue;
                }
                if (i + static_cast<std::size_t>(extra) >= n)
                {
                    out.push_back(0xFFFD);
                    bytes.push_back(start);
                    break;
                }
                bool ok = true;
                for (int k = 0; k < extra; ++k)
                {
                    const auto cc = static_cast<unsigned char>(s[i + 1 + static_cast<std::size_t>(k)]);
                    if ((cc & 0xC0) != 0x80)
                    {
                        ok = false;
                        break;
                    }
                    cp = (cp << 6) | (cc & 0x3F);
                }
                if (!ok)
                {
                    out.push_back(0xFFFD);
                    bytes.push_back(start);
                    ++i;
                    continue;
                }
                out.push_back(cp);
                bytes.push_back(start);
                i += static_cast<std::size_t>(extra) + 1;
            }
            bytes.push_back(n);
        }
    } // namespace

    struct FontSystem::Impl
    {
        struct Face
        {
            std::vector<std::byte> data;
            FT_Face ft = nullptr;
            hb_blob_t* blob = nullptr;
            hb_face_t* face = nullptr;
            hb_font_t* font = nullptr;
            FontSpec spec;
        };

        struct ShapedGlyph
        {
            std::uint32_t glyph;
            float advance;
            float xOffset;
            float yOffset;
            std::uint32_t cluster;
        };

        struct PositionedGlyph
        {
            std::uint32_t faceIndex;
            std::uint32_t glyph;
            float x; // pen-relative (origin 0)
            float y; // baseline
            float advance;
        };

        struct Layout
        {
            std::vector<PositionedGlyph> glyphs;
            Vec2 size{};
        };

        struct GlyphInfo
        {
            int left = 0;
            int top = 0;
            int w = 0;
            int h = 0;
            float scale = 1.f; // native bitmap → requested px (colour strikes); 1 for scalable faces
            bool color = false;
        };

        struct Run
        {
            int start;
            int len;
            std::uint32_t faceIdx;
            std::uint8_t level;
            std::vector<ShapedGlyph> glyphs; // this run's visual order
        };

        // Borrowed view of a layout key so GetLayout can probe the cache from a string_view without allocating per frame.
        struct CacheKeyView
        {
            std::string_view text;
            std::uint32_t face;
            int px;
            int wrapWidth; // -1 = no wrap
            int maxLines;
            std::uint8_t align;
            bool wrap;
            bool ellipsis;
            float lineHeight;

            [[nodiscard]] bool operator==(const CacheKeyView&) const = default;
        };

        struct CacheKey
        {
            std::string text;
            std::uint32_t face;
            int px;
            int wrapWidth;
            int maxLines;
            std::uint8_t align;
            bool wrap;
            bool ellipsis;
            float lineHeight;

            [[nodiscard]] operator CacheKeyView() const noexcept
            {
                return CacheKeyView{text, face, px, wrapWidth, maxLines, align, wrap, ellipsis, lineHeight};
            }
        };

        struct CacheHash
        {
            using is_transparent = void;

            [[nodiscard]] std::size_t operator()(const CacheKeyView& k) const noexcept
            {
                std::size_t h = std::hash<std::string_view>{}(k.text);
                const auto mix = [&h](std::size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
                mix(k.face);
                mix(static_cast<std::size_t>(k.px));
                mix(static_cast<std::size_t>(k.wrapWidth));
                mix(static_cast<std::size_t>(k.maxLines));
                mix(k.align);
                mix(k.wrap ? 1u : 0u);
                mix(k.ellipsis ? 1u : 0u);
                mix(std::hash<float>{}(k.lineHeight));
                return h;
            }
        };

        struct CacheEq
        {
            using is_transparent = void;

            [[nodiscard]] bool operator()(const CacheKeyView& a, const CacheKeyView& b) const noexcept
            {
                return a == b;
            }
        };

        FT_Library ftLib = nullptr;
        std::vector<std::unique_ptr<Face>> faces;
        std::unordered_map<std::string, std::array<FaceId, 4>> families;
        std::vector<FaceId> fallbacks;
        std::string defaultFamily;

        hb_buffer_t* shapeBuffer = nullptr;

        GlyphAtlas coverage{AtlasFormat::R8, 1024, 256};
        GlyphAtlas color{AtlasFormat::Rgba8888, 512, 128};

        std::unordered_map<std::uint64_t, GlyphInfo> glyphInfo;
        std::unordered_map<CacheKey, Layout, CacheHash, CacheEq> layoutCache;

        std::vector<std::byte> convertScratch;

        // Logical to physical pixel ratio. Shaping/layout stay logical (identical wrapping at every DPI); glyphs rasterise at
        // physical size. `generation` bumps when a scale change resets the atlas.
        float scale = 1.f;
        std::uint64_t generation = 0;

        /// The physical pixel size a glyph rasterises at for the requested logical `size`.
        [[nodiscard]] int RasterPx(float size) const noexcept
        {
            return std::max(1, static_cast<int>(std::lround(size * scale)));
        }

        Impl()
        {
            if (FT_Init_FreeType(&ftLib) != 0)
            {
                LOG_ERROR("FT_Init_FreeType failed; text disabled");
                ftLib = nullptr;
            }
            shapeBuffer = hb_buffer_create();
            init_linebreak();
        }

        ~Impl()
        {
            if (shapeBuffer) hb_buffer_destroy(shapeBuffer);
            for (auto& f : faces)
            {
                if (f->font) hb_font_destroy(f->font);
                if (f->face) hb_face_destroy(f->face);
                if (f->blob) hb_blob_destroy(f->blob);
                if (f->ft) FT_Done_Face(f->ft);
            }
            faces.clear();
            if (ftLib) FT_Done_FreeType(ftLib);
        }

        [[nodiscard]] Face* FaceOf(FaceId id) const
        {
            const auto i = static_cast<std::uint32_t>(id);
            if (i == 0 || i > faces.size())
            {
                return nullptr;
            }
            return faces[i - 1].get();
        }

        [[nodiscard]] FaceId Resolve(const FontProps& props) const
        {
            const std::string& want = props.family.empty() ? defaultFamily : props.family;
            auto it = families.find(want);
            if (it == families.end())
            {
                it = families.find(defaultFamily);
                if (it == families.end())
                {
                    return faces.empty() ? FaceId::Invalid : static_cast<FaceId>(1u);
                }
            }
            const std::array<FaceId, 4>& slots = it->second;
            const int wanted = (props.bold ? 2 : 0) | (props.italic ? 1 : 0);
            if (slots[wanted] != FaceId::Invalid) return slots[wanted];
            if (slots[0] != FaceId::Invalid) return slots[0]; // regular
            for (FaceId f : slots)
            {
                if (f != FaceId::Invalid) return f;
            }
            return FaceId::Invalid;
        }

        /// First face in {primary, fallback...} that maps `cp`; primary if none (renders tofu). Drives run itemization.
        [[nodiscard]] std::uint32_t CoveringFace(std::uint32_t cp, std::uint32_t primary) const
        {
            if (primary < faces.size() && faces[primary]->ft &&
                FT_Get_Char_Index(faces[primary]->ft, cp) != 0)
            {
                return primary;
            }
            for (FaceId f : fallbacks)
            {
                const auto idx = static_cast<std::uint32_t>(f) - 1;
                if (idx != primary && idx < faces.size() && faces[idx]->ft &&
                    FT_Get_Char_Index(faces[idx]->ft, cp) != 0)
                {
                    return idx;
                }
            }
            return primary;
        }

        struct VMetrics
        {
            float ascent;
            float height;
        };

        /// Per-line metrics in px from the primary face; colour-only bitmap faces lack scalable metrics and fall back to px-based.
        [[nodiscard]] VMetrics Metrics(std::uint32_t faceIndex, int px) const
        {
            VMetrics m{static_cast<float>(px), px * 1.2f};
            if (faceIndex >= faces.size() || !faces[faceIndex]->ft)
            {
                return m;
            }
            FT_Face ft = faces[faceIndex]->ft;
            if (!FT_HAS_COLOR(ft))
            {
                FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(px));
                m.ascent = ft->size->metrics.ascender / 64.f;
                m.height = ft->size->metrics.height / 64.f;
                if (m.height <= 0.f)
                {
                    m.height = (ft->size->metrics.ascender - ft->size->metrics.descender) / 64.f;
                }
            }
            return m;
        }

        /// Rasterise every glyph of `layout` into the atlases up front, during measurement, so the atlas is complete before
        /// the painter syncs it (a grow never moves an existing slot).
        void EnsureRastered(const Layout& layout, int px)
        {
            for (const PositionedGlyph& g : layout.glyphs)
            {
                const std::uint64_t key = GlyphKey(g.faceIndex, g.glyph, px);
                if (!glyphInfo.contains(key))
                {
                    RasterGlyph(g.faceIndex, g.glyph, px, key);
                }
            }
        }

        void RasterGlyph(std::uint32_t faceIndex, std::uint32_t glyph, int px, std::uint64_t key)
        {
            GlyphInfo gi{};
            Face* face = faces[faceIndex].get();
            FT_Face ft = face->ft;
            if (!ft)
            {
                glyphInfo.emplace(key, gi);
                return;
            }

            // Colour-bitmap (emoji) faces ship fixed-size strikes: pick the nearest and remember the scale; scalable faces
            // just set the pixel size.
            const bool isColor = FT_HAS_COLOR(ft);
            float scale = 1.f;
            if (isColor && ft->num_fixed_sizes > 0)
            {
                int best = 0;
                int bestDelta = std::numeric_limits<int>::max();
                for (int i = 0; i < ft->num_fixed_sizes; ++i)
                {
                    const int d = std::abs(static_cast<int>(ft->available_sizes[i].height) - px);
                    if (d < bestDelta)
                    {
                        bestDelta = d;
                        best = i;
                    }
                }
                FT_Select_Size(ft, best);
                const int strikeH = ft->available_sizes[best].height;
                if (strikeH > 0)
                {
                    scale = static_cast<float>(px) / static_cast<float>(strikeH);
                }
            }
            else
            {
                FT_Set_Pixel_Sizes(ft, 0, static_cast<FT_UInt>(px));
            }

            const FT_Int32 flags = FT_LOAD_RENDER | (isColor ? FT_LOAD_COLOR : 0);
            if (FT_Load_Glyph(ft, glyph, flags) != 0)
            {
                glyphInfo.emplace(key, gi);
                return;
            }
            const FT_GlyphSlot slot = ft->glyph;
            const FT_Bitmap& bmp = slot->bitmap;
            gi.left = slot->bitmap_left;
            gi.top = slot->bitmap_top;
            gi.w = static_cast<int>(bmp.width);
            gi.h = static_cast<int>(bmp.rows);
            gi.scale = scale;
            if (gi.w <= 0 || gi.h <= 0)
            {
                glyphInfo.emplace(key, gi); // whitespace / empty: advance only
                return;
            }

            if (bmp.pixel_mode == FT_PIXEL_MODE_BGRA)
            {
                // FreeType colour bitmaps are premultiplied BGRA; un-premultiply into straight RGBA.
                gi.color = true;
                convertScratch.assign(static_cast<std::size_t>(gi.w) * gi.h * 4, std::byte{0});
                auto* dst = reinterpret_cast<std::uint8_t*>(convertScratch.data());
                for (int y = 0; y < gi.h; ++y)
                {
                    const std::uint8_t* src = bmp.buffer + static_cast<std::ptrdiff_t>(y) * bmp.pitch;
                    for (int x = 0; x < gi.w; ++x)
                    {
                        const std::uint8_t* p = src + x * 4; // B,G,R,A premultiplied
                        std::uint8_t* q = dst + (static_cast<std::size_t>(y) * gi.w + x) * 4;
                        const std::uint8_t a = p[3];
                        if (a > 0)
                        {
                            q[0] = static_cast<std::uint8_t>(std::min(255, p[2] * 255 / a));
                            q[1] = static_cast<std::uint8_t>(std::min(255, p[1] * 255 / a));
                            q[2] = static_cast<std::uint8_t>(std::min(255, p[0] * 255 / a));
                        }
                        else
                        {
                            q[0] = q[1] = q[2] = 0;
                        }
                        q[3] = a;
                    }
                }
                if (!color.Add(key, dst, gi.w, gi.h, gi.w * 4))
                {
                    LOG_WARN("glyph {} ({}x{}) does not fit the colour atlas plane", glyph, gi.w, gi.h);
                    gi.w = gi.h = 0;
                }
            }
            else if (!coverage.Add(key, bmp.buffer, gi.w, gi.h, bmp.pitch))
            {
                LOG_WARN("glyph {} ({}x{}) does not fit the coverage atlas plane", glyph, gi.w, gi.h);
                gi.w = gi.h = 0;
            }
            glyphInfo.emplace(key, gi);
        }

        /// Full Unicode layout: decode, bidi levels, line-break opportunities, itemize, shape, greedy line break, L2 reorder,
        /// ellipsis, align. Produces glyphs in surface-pen-relative coords (origin 0).
        Layout BuildLayout(std::string_view text, const FontProps& props, float size,
                           const TextFlowProps& flow, AvailableSpace width)
        {
            Layout layout;
            const FaceId faceId = Resolve(props);
            Face* face = FaceOf(faceId);
            if (!face || !face->ft)
            {
                return layout;
            }
            const auto primary = static_cast<std::uint32_t>(faceId) - 1;
            const int px = PixelSize(size);

            std::vector<std::uint32_t> cps;
            std::vector<std::size_t> cpBytes;
            DecodeUtf8(text, cps, cpBytes);
            const int n = static_cast<int>(cps.size());

            const VMetrics vm = Metrics(primary, px);
            const float lineAdv = vm.height * (props.lineHeight > 0.f ? props.lineHeight : 1.f);
            const float halfLeading = (lineAdv - vm.height) * 0.5f;
            if (n == 0)
            {
                layout.size = {0.f, std::ceil(lineAdv)};
                return layout;
            }

            // 1. bidi levels (SheenBidi over UTF-32, base direction auto with LTR default), one paragraph per separator.
            std::vector<std::uint8_t> levels(static_cast<std::size_t>(n), 0);
            {
                SBCodepointSequence seq{SBStringEncodingUTF32, cps.data(), static_cast<SBUInteger>(n)};
                SBAlgorithmRef algo = SBAlgorithmCreate(&seq);
                SBUInteger offset = 0;
                while (offset < static_cast<SBUInteger>(n))
                {
                    SBParagraphRef para = SBAlgorithmCreateParagraph(algo, offset, static_cast<SBUInteger>(n) - offset,
                                                                     SBLevelDefaultLTR);
                    const SBUInteger len = SBParagraphGetLength(para);
                    const SBLevel* lv = SBParagraphGetLevelsPtr(para);
                    for (SBUInteger i = 0; i < len; ++i)
                    {
                        levels[static_cast<std::size_t>(offset + i)] = static_cast<std::uint8_t>(lv[i]);
                    }
                    SBParagraphRelease(para);
                    offset += len;
                }
                SBAlgorithmRelease(algo);
            }

            // 2. UAX#14 line-break opportunities (MUSTBREAK covers '\n').
            std::vector<char> brks(static_cast<std::size_t>(n), LINEBREAK_NOBREAK);
            set_linebreaks_utf32(reinterpret_cast<const utf32_t*>(cps.data()), static_cast<std::size_t>(n),
                                 "", brks.data());

            // 3. itemize into runs uniform in (level, script, covering face).
            hb_unicode_funcs_t* uf = hb_unicode_funcs_get_default();
            std::vector<Run> runs;
            {
                int start = 0;
                std::uint8_t curLevel = levels[0];
                std::uint32_t curFace = CoveringFace(cps[0], primary);
                hb_script_t curScript = hb_unicode_script(uf, cps[0]);
                for (int i = 1; i <= n; ++i)
                {
                    bool brk = (i == n);
                    if (!brk)
                    {
                        const std::uint32_t f = CoveringFace(cps[static_cast<std::size_t>(i)], primary);
                        const hb_script_t sc = hb_unicode_script(uf, cps[static_cast<std::size_t>(i)]);
                        const bool extends = (sc == HB_SCRIPT_COMMON || sc == HB_SCRIPT_INHERITED);
                        // Adopt the first real script BEFORE the break test so a run started on a neutral merges into the following script.
                        if (!extends && (curScript == HB_SCRIPT_COMMON || curScript == HB_SCRIPT_INHERITED))
                        {
                            curScript = sc;
                        }
                        if (levels[static_cast<std::size_t>(i)] != curLevel || f != curFace ||
                            (!extends && sc != curScript))
                        {
                            brk = true;
                        }
                    }
                    if (brk)
                    {
                        runs.push_back(Run{start, i - start, curFace, curLevel, {}});
                        if (i < n)
                        {
                            start = i;
                            curLevel = levels[static_cast<std::size_t>(i)];
                            curFace = CoveringFace(cps[static_cast<std::size_t>(i)], primary);
                            curScript = hb_unicode_script(uf, cps[static_cast<std::size_t>(i)]);
                        }
                    }
                }
            }

            // 4. shape each run; accumulate advance per code point for logical line breaking.
            std::vector<float> advByCp(static_cast<std::size_t>(n), 0.f);
            for (Run& run : runs)
            {
                Face* rf = faces[run.faceIdx].get();
                if (!rf || !rf->font)
                {
                    continue;
                }
                hb_font_set_scale(rf->font, px * 64, px * 64);
                hb_font_set_ppem(rf->font, static_cast<unsigned>(px), static_cast<unsigned>(px));
                hb_buffer_clear_contents(shapeBuffer);
                hb_buffer_add_utf32(shapeBuffer, cps.data(), n, run.start, run.len);
                hb_buffer_set_direction(shapeBuffer, (run.level & 1) ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                hb_buffer_set_script(shapeBuffer, hb_unicode_script(uf, cps[static_cast<std::size_t>(run.start)]));
                hb_buffer_set_language(shapeBuffer, hb_language_get_default());
                hb_shape(rf->font, shapeBuffer, nullptr, 0);

                unsigned int gc = 0;
                const hb_glyph_info_t* info = hb_buffer_get_glyph_infos(shapeBuffer, &gc);
                const hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(shapeBuffer, &gc);
                run.glyphs.reserve(gc);
                for (unsigned int i = 0; i < gc; ++i)
                {
                    const float adv = pos[i].x_advance / 64.f;
                    const auto cl = static_cast<int>(info[i].cluster);
                    run.glyphs.push_back(ShapedGlyph{
                        info[i].codepoint, adv, pos[i].x_offset / 64.f,
                        pos[i].y_offset / 64.f, info[i].cluster
                    });
                    if (cl >= 0 && cl < n)
                    {
                        advByCp[static_cast<std::size_t>(cl)] += adv;
                    }
                }
            }

            // 5. greedy line breaking over code points in logical order.
            const bool doWrap = flow.wrap && width.IsDefinite();
            const float maxWidth = doWrap ? width.value : std::numeric_limits<float>::infinity();
            struct LineRange
            {
                int start;
                int end;
            };
            std::vector<LineRange> lineRanges;
            {
                int lineStart = 0;
                float w = 0.f;
                int lastBreak = -1;
                for (int i = 0; i < n; ++i)
                {
                    w += advByCp[static_cast<std::size_t>(i)];
                    const bool must = (brks[static_cast<std::size_t>(i)] == LINEBREAK_MUSTBREAK);
                    if (doWrap && w > maxWidth && lastBreak >= lineStart)
                    {
                        lineRanges.push_back({lineStart, lastBreak + 1});
                        lineStart = lastBreak + 1;
                        lastBreak = -1;
                        w = 0.f;
                        for (int k = lineStart; k <= i; ++k)
                        {
                            w += advByCp[static_cast<std::size_t>(k)];
                        }
                    }
                    if (brks[static_cast<std::size_t>(i)] == LINEBREAK_ALLOWBREAK || must)
                    {
                        lastBreak = i;
                    }
                    if (must)
                    {
                        lineRanges.push_back({lineStart, i + 1});
                        lineStart = i + 1;
                        lastBreak = -1;
                        w = 0.f;
                    }
                }
                if (lineStart < n || lineRanges.empty())
                {
                    lineRanges.push_back({lineStart, n});
                }
            }

            bool truncated = false;
            if (flow.maxLines > 0 && static_cast<int>(lineRanges.size()) > flow.maxLines)
            {
                lineRanges.resize(static_cast<std::size_t>(flow.maxLines));
                truncated = true;
            }

            // 6. per visual line: gather run-segments, apply UBA rule L2, lay the pen out.
            struct Seg
            {
                std::uint8_t level;
                const Run* run;
                int first;
                int last;
            };
            struct BuiltLine
            {
                std::vector<PositionedGlyph> glyphs;
                float width;
            };
            std::vector<BuiltLine> built;
            built.reserve(lineRanges.size());

            for (std::size_t li = 0; li < lineRanges.size(); ++li)
            {
                const LineRange lr = lineRanges[li];
                std::vector<Seg> segs;
                for (const Run& run : runs)
                {
                    int first = -1;
                    for (int gi = 0; gi < static_cast<int>(run.glyphs.size()); ++gi)
                    {
                        const int cl = static_cast<int>(run.glyphs[static_cast<std::size_t>(gi)].cluster);
                        const bool in = (cl >= lr.start && cl < lr.end);
                        if (in && first < 0)
                        {
                            first = gi;
                        }
                        if (!in && first >= 0)
                        {
                            segs.push_back(Seg{run.level, &run, first, gi});
                            first = -1;
                        }
                    }
                    if (first >= 0)
                    {
                        segs.push_back(Seg{run.level, &run, first, static_cast<int>(run.glyphs.size())});
                    }
                }

                // L2: from the highest level down to the lowest odd level, reverse maximal runs of segments with level >= L.
                std::uint8_t maxLevel = 0;
                std::uint8_t minOdd = 255;
                for (const Seg& s : segs)
                {
                    maxLevel = std::max(maxLevel, s.level);
                    if (s.level & 1)
                    {
                        minOdd = std::min(minOdd, s.level);
                    }
                }
                for (int L = maxLevel; minOdd != 255 && L >= minOdd; --L)
                {
                    std::size_t i = 0;
                    while (i < segs.size())
                    {
                        if (segs[i].level >= L)
                        {
                            std::size_t j = i;
                            while (j < segs.size() && segs[j].level >= L)
                            {
                                ++j;
                            }
                            std::reverse(segs.begin() + static_cast<std::ptrdiff_t>(i),
                                         segs.begin() + static_cast<std::ptrdiff_t>(j));
                            i = j;
                        }
                        else
                        {
                            ++i;
                        }
                    }
                }

                BuiltLine bl;
                const float baseline = static_cast<float>(li) * lineAdv + halfLeading + vm.ascent;
                float x = 0.f;
                for (const Seg& s : segs)
                {
                    for (int gi = s.first; gi < s.last; ++gi)
                    {
                        const ShapedGlyph& g = s.run->glyphs[static_cast<std::size_t>(gi)];
                        // Line-break controls end the line; fonts without a blank glyph for them would draw .notdef.
                        const std::uint32_t cp = cps[g.cluster];
                        if (cp == '\n' || cp == '\r' || cp == '\v' || cp == '\f' || cp == 0x85 || cp == 0x2028 ||
                            cp == 0x2029)
                        {
                            continue;
                        }
                        bl.glyphs.push_back(
                            PositionedGlyph{s.run->faceIdx, g.glyph, x + g.xOffset, baseline - g.yOffset, g.advance});
                        x += g.advance;
                    }
                }
                bl.width = x;
                built.push_back(std::move(bl));
            }

            // 7. ellipsis on the truncated last line (best-effort visual-tail trim + U+2026).
            if (truncated && flow.ellipsis && !built.empty())
            {
                BuiltLine& last = built.back();
                const FT_UInt eg = FT_Get_Char_Index(face->ft, 0x2026);
                if (eg != 0)
                {
                    hb_font_set_scale(face->font, px * 64, px * 64);
                    const float ellAdv = hb_font_get_glyph_h_advance(face->font, eg) / 64.f;
                    const float limit = std::isfinite(maxWidth) ? maxWidth : last.width + ellAdv;
                    while (!last.glyphs.empty() && last.width + ellAdv > limit)
                    {
                        last.width -= last.glyphs.back().advance;
                        last.glyphs.pop_back();
                    }
                    const float baseline = static_cast<float>(built.size() - 1) * lineAdv + halfLeading + vm.ascent;
                    last.glyphs.push_back(PositionedGlyph{primary, eg, last.width, baseline, ellAdv});
                    last.width += ellAdv;
                }
            }

            // 8. alignment within the box + assemble.
            float maxLineW = 0.f;
            for (const BuiltLine& bl : built)
            {
                maxLineW = std::max(maxLineW, bl.width);
            }
            const float boxW = width.IsDefinite() ? width.value : maxLineW;
            for (BuiltLine& bl : built)
            {
                const float off = AlignOffset(flow.align, bl.width, boxW);
                for (PositionedGlyph& pg : bl.glyphs)
                {
                    pg.x += off;
                    layout.glyphs.push_back(pg);
                }
            }
            // Round the box height up so the snapped layout box contains the snapped baseline's descenders.
            layout.size = {maxLineW, std::ceil(static_cast<float>(built.size()) * lineAdv)};
            return layout;
        }

        const Layout& GetLayout(std::string_view text, const FontProps& props, float size,
                                const TextFlowProps& flow, AvailableSpace width)
        {
            const FaceId faceId = Resolve(props);
            const CacheKeyView view{
                .text = text,
                .face = static_cast<std::uint32_t>(faceId),
                .px = PixelSize(size),
                .wrapWidth = width.IsDefinite() ? static_cast<int>(std::lround(width.value)) : -1,
                .maxLines = flow.maxLines,
                .align = static_cast<std::uint8_t>(flow.align),
                .wrap = flow.wrap,
                .ellipsis = flow.ellipsis,
                .lineHeight = props.lineHeight
            };
            // Heterogeneous probe: no std::string allocation on a hit (the per-frame common case).
            if (const auto it = layoutCache.find(view); it != layoutCache.end())
            {
                return it->second;
            }
            Layout layout = BuildLayout(text, props, size, flow, width);
            const CacheKey key{
                .text = std::string(text),
                .face = view.face,
                .px = view.px,
                .wrapWidth = view.wrapWidth,
                .maxLines = view.maxLines,
                .align = view.align,
                .wrap = view.wrap,
                .ellipsis = view.ellipsis,
                .lineHeight = view.lineHeight
            };
            return layoutCache.emplace(key, std::move(layout)).first->second;
        }
    };

    FontSystem::FontSystem() : _impl(std::make_unique<Impl>())
    {
    }

    FontSystem::~FontSystem() = default;

    FaceId FontSystem::RegisterFont(std::span<const std::byte> fontFile, const FontSpec& spec)
    {
        if (!_impl->ftLib || fontFile.empty())
        {
            return FaceId::Invalid;
        }
        auto face = std::make_unique<Impl::Face>();
        face->data.assign(fontFile.begin(), fontFile.end());
        face->spec = spec;

        if (FT_New_Memory_Face(_impl->ftLib, reinterpret_cast<const FT_Byte*>(face->data.data()),
                               static_cast<FT_Long>(face->data.size()), 0, &face->ft) != 0)
        {
            LOG_ERROR("FT_New_Memory_Face failed for family '{}'", spec.family);
            return FaceId::Invalid;
        }
        face->blob = hb_blob_create(reinterpret_cast<const char*>(face->data.data()),
                                    static_cast<unsigned int>(face->data.size()),
                                    HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        face->face = hb_face_create(face->blob, 0);
        face->font = hb_font_create(face->face);

        _impl->faces.push_back(std::move(face));
        const auto id = static_cast<FaceId>(_impl->faces.size());

        auto& slots = _impl->families[spec.family];
        slots[(spec.bold ? 2 : 0) | (spec.italic ? 1 : 0)] = id;
        if (_impl->defaultFamily.empty())
        {
            _impl->defaultFamily = spec.family;
        }
        LOG_INFO("registered face '{}' (bold={}, italic={}) as #{}", spec.family, spec.bold, spec.italic,
                 static_cast<std::uint32_t>(id));
        return id;
    }

    void FontSystem::AddFallback(FaceId face)
    {
        if (face != FaceId::Invalid)
        {
            _impl->fallbacks.push_back(face);
        }
    }

    void FontSystem::SetDefaultFamily(std::string_view family)
    {
        _impl->defaultFamily = std::string(family);
    }

    Vec2 FontSystem::Measure(std::string_view text, const FontProps& font, float size,
                             const TextFlowProps& flow, AvailableSpace width) const
    {
        if (text.empty())
        {
            return {};
        }
        const Impl::Layout& layout = _impl->GetLayout(text, font, size, flow, width);
        _impl->EnsureRastered(layout, _impl->RasterPx(size)); // pre-warm the atlas during the layout pass
        return layout.size;
    }

    void FontSystem::BuildGlyphCache(std::string_view text, const FontProps& font, float size,
                                     const TextFlowProps& flow, AvailableSpace width,
                                     std::vector<CachedGlyph>& coverage,
                                     std::vector<CachedGlyph>& color) const
    {
        coverage.clear();
        color.clear();
        if (text.empty())
        {
            return;
        }
        const Impl::Layout& layout = _impl->GetLayout(text, font, size, flow, width);
        const int rpx = _impl->RasterPx(size);

        // Rasterise first so a grow cannot move slots mid-walk (usually a no-op, Measure pre-warmed). Slots are append-only,
        // so cached pixel rects stay valid.
        _impl->EnsureRastered(layout, rpx);

        for (const Impl::PositionedGlyph& g : layout.glyphs)
        {
            const std::uint64_t key = GlyphKey(g.faceIndex, g.glyph, rpx);
            const auto info = _impl->glyphInfo.find(key);
            if (info == _impl->glyphInfo.end())
            {
                continue;
            }
            const Impl::GlyphInfo& gi = info->second;
            if (gi.w <= 0 || gi.h <= 0)
            {
                continue; // whitespace / empty glyph: advance only, nothing to draw
            }

            GlyphAtlas& atlas = gi.color ? _impl->color : _impl->coverage;
            const GlyphAtlas::Slot* slot = atlas.Find(key);
            if (!slot)
            {
                continue;
            }
            // Bitmap metrics are in PHYSICAL pixels; divide by the content scale so `local` is LOGICAL (the painter's base
            // layer scales it back).
            const float s = gi.scale / _impl->scale;
            // Pen-relative geometry is stored unrounded: the pen's fractional offset is only added at emit, so snapping here
            // would be defeated.
            // Emit snaps `local + pen` so the linear-filtered coverage atlas maps 1:1; colour strikes stay subpixel.
            const Rect local{
                g.x + static_cast<float>(gi.left) * s, g.y - static_cast<float>(gi.top) * s,
                static_cast<float>(gi.w) * s, static_cast<float>(gi.h) * s
            };
            (gi.color ? color : coverage)
                .push_back(CachedGlyph{
                    local, static_cast<std::uint16_t>(slot->x),
                    static_cast<std::uint16_t>(slot->y),
                    static_cast<std::uint16_t>(gi.w),
                    static_cast<std::uint16_t>(gi.h)
                });
        }
    }

    AtlasView FontSystem::Atlas(AtlasKind kind) const
    {
        return kind == AtlasKind::Coverage ? _impl->coverage.View() : _impl->color.View();
    }

    Vec2 FontSystem::AtlasSize(AtlasKind kind) const
    {
        const GlyphAtlas& plane = kind == AtlasKind::Coverage ? _impl->coverage : _impl->color;
        return {static_cast<float>(plane.Width()), static_cast<float>(plane.Height())};
    }

    void FontSystem::SetContentScale(float scale)
    {
        const float s = scale > 0.f ? scale : 1.f;
        if (s == _impl->scale)
        {
            return;
        }
        _impl->scale = s;
        _impl->glyphInfo.clear();
        _impl->coverage.Reset();
        _impl->color.Reset();
        ++_impl->generation;
        LOG_INFO("content scale → {}", s);
    }

    float FontSystem::ContentScale() const noexcept
    {
        return _impl->scale;
    }

    std::uint64_t FontSystem::Generation() const noexcept
    {
        return _impl->generation;
    }
} // namespace lumora
