#include "Style/Convert.h"

#include <array>
#include <cctype>

namespace lumora::style
{
    std::vector<StringId> SplitClasses(const std::string& s)
    {
        std::vector<StringId> out;
        size_t i = 0;
        while (i < s.size())
        {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            const size_t start = i;
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
            if (i > start) out.push_back(StringId(std::string_view(s).substr(start, i - start)));
        }
        return out;
    }

    bool KeywordIs(const CssValue& v, StringId kw) { return v.kind == ValueKind::Keyword && v.keyword == kw; }

    Color ToColor(const CssValue& v, Color fb)
    {
        if (v.kind == ValueKind::Color) return v.color;
        return fb;
    }

    float ToNumber(const CssValue& v, float fb)
    {
        if (v.kind == ValueKind::Number || v.kind == ValueKind::Length) return v.num;
        return fb;
    }

    float ToPx(const CssValue& v, float fb)
    {
        if (v.kind == ValueKind::Length)
        {
            if (v.unit == Unit::Percent) return fb;
            return v.num;
        }
        if (v.kind == ValueKind::Number) return v.num;
        return fb;
    }

    SizeHint ToSizeHint(const CssValue& v, SizeHint fb)
    {
        if (v.kind == ValueKind::Length)
        {
            if (v.unit == Unit::Percent) return Percent(v.num);  // native % to Masharif
            return Fixed(v.num);
        }
        if (v.kind == ValueKind::Number) return Fixed(v.num);
        if (v.kind == ValueKind::Keyword)
        {
            if (v.keyword == "auto"_sid || v.keyword == "none"_sid) return Wrap;
        }
        return fb;
    }

    Align ToAlign(const CssValue& v, Align fb)
    {
        if (v.kind != ValueKind::Keyword) return fb;
        const StringId k = v.keyword;
        if (k == "flex-start"_sid || k == "start"_sid || k == "left"_sid) return Align::Start;
        if (k == "center"_sid) return Align::Center;
        if (k == "flex-end"_sid || k == "end"_sid || k == "right"_sid) return Align::End;
        if (k == "stretch"_sid) return Align::Stretch;
        if (k == "space-between"_sid) return Align::Between;
        if (k == "space-around"_sid) return Align::Around;
        if (k == "space-evenly"_sid) return Align::Evenly;
        if (k == "auto"_sid) return Align::Auto; // valid for align-self / justify-self only
        return fb;
    }

    FlexAxis ToFlexAxis(const CssValue& v, FlexAxis fb)
    {
        if (v.kind != ValueKind::Keyword) return fb;
        const StringId k = v.keyword;
        if (k == "row"_sid || k == "row-reverse"_sid) return FlexAxis::Row;
        if (k == "column"_sid || k == "column-reverse"_sid) return FlexAxis::Column;
        return fb;
    }

    Position ToPosition(const CssValue& v, Position fb)
    {
        if (v.kind != ValueKind::Keyword) return fb;
        const StringId k = v.keyword;
        if (k == "absolute"_sid) return Position::Absolute;
        if (k == "fixed"_sid) return Position::Fixed;
        if (k == "static"_sid || k == "relative"_sid || k == "sticky"_sid) return Position::Flow;
        return fb;
    }

    float ToInsetEdge(const CssValue& v, float fbAuto)
    {
        if (v.kind == ValueKind::Keyword) return fbAuto; // `auto` (or any keyword) → containing-block static pos
        if (v.kind == ValueKind::Length)
        {
            if (v.unit == Unit::Percent) return fbAuto; // percent insets not modelled → auto
            return v.num;
        }
        if (v.kind == ValueKind::Number) return v.num;
        return fbAuto;
    }

    FlexWrap ToFlexWrap(const CssValue& v, FlexWrap fb)
    {
        if (v.kind != ValueKind::Keyword) return fb;
        const StringId k = v.keyword;
        if (k == "nowrap"_sid) return FlexWrap::NoWrap;
        if (k == "wrap"_sid) return FlexWrap::Wrap;
        if (k == "wrap-reverse"_sid) return FlexWrap::WrapReverse;
        return fb;
    }

    float ToAspectRatio(const CssValue& v)
    {
        if (KeywordIs(v, "auto"_sid) || KeywordIs(v, "none"_sid)) return 0.f;

        // Collect the numeric components and ignore what lies between: `16 / 9` arrives as a list whose middle component may or
        // may not survive.
        float nums[2]{};
        int found = 0;
        const auto take = [&](const CssValue& c)
        {
            if (found < 2 && (c.kind == ValueKind::Number || c.kind == ValueKind::Length))
            {
                nums[found++] = c.num;
            }
        };
        if (v.kind == ValueKind::List)
        {
            for (const CssValue& c : v.list) take(c);
        }
        else
        {
            take(v);
        }

        if (found == 2) return (nums[1] != 0.f && nums[0] > 0.f) ? nums[0] / nums[1] : 0.f;
        if (found == 1) return nums[0] > 0.f ? nums[0] : 0.f;
        return 0.f;
    }

    bool ToBorderDashed(const CssValue& v, bool fb)
    {
        if (v.kind != ValueKind::Keyword) return fb;
        return v.keyword == "dashed"_sid;
    }

    FlexShorthand ToFlex(const CssValue& v)
    {
        // A single keyword: `none` (0 0 auto) or `auto`/`initial` (1 1 auto).
        if (v.kind == ValueKind::Keyword)
        {
            if (v.keyword == "none"_sid) return {0.f, 0.f, Wrap};
            return {1.f, 1.f, Wrap};
        }
        // A single number: `flex: 2` == grow 2, shrink 1, basis 0.
        if (v.kind == ValueKind::Number) return {v.num, 1.f, Fixed(0.f)};
        // A single length: `flex: 100px` == grow 1, shrink 1, basis 100px.
        if (v.kind == ValueKind::Length) return {1.f, 1.f, ToSizeHint(v, Fixed(0.f))};

        if (v.kind == ValueKind::List && !v.comma)
        {
            FlexShorthand out{1.f, 1.f, Fixed(0.f)};
            const std::vector<CssValue>& L = v.list;
            int numbersSeen = 0;
            bool basisSet = false;
            for (const CssValue& c : L)
            {
                if (c.kind == ValueKind::Keyword)
                {
                    out.basis = ToSizeHint(c, Wrap);
                    basisSet = true;
                }
                else if (c.kind == ValueKind::Length)
                {
                    out.basis = ToSizeHint(c, Fixed(0.f));
                    basisSet = true;
                }
                else if (c.kind == ValueKind::Number)
                {
                    if (numbersSeen == 0) out.grow = c.num;
                    else if (numbersSeen == 1) out.shrink = c.num;
                    ++numbersSeen;
                }
            }
            if (numbersSeen == 1 && !basisSet) out.shrink = 1.f; // `flex: <g> <basis>` keeps shrink 1
            return out;
        }
        return {1.f, 1.f, Fixed(0.f)};
    }

    namespace
    {
        [[nodiscard]] bool EdgePx(const CssValue& c, float& px, float& pct)
        {
            // returns true if the value contributed an edge; sets px XOR pct.
            if (c.kind == ValueKind::Length)
            {
                if (c.unit == Unit::Percent)
                {
                    pct = c.num;
                    px = 0.f;
                    return true;
                }
                px = c.num;
                pct = 0.f;
                return true;
            }
            if (c.kind == ValueKind::Number)
            {
                px = c.num;
                pct = 0.f;
                return true;
            }
            return false;
        }
    } // namespace

    SpacingSplit ToSpacingSplit(const CssValue& v, Spacing fbPx)
    {
        auto edge = [](const CssValue& c, float& px, float& pct) { EdgePx(c, px, pct); };

        SpacingSplit out{fbPx, Spacing{}};
        float px[4]{}, pct[4]{};
        if (v.kind == ValueKind::List && !v.comma && !v.list.empty())
        {
            const std::vector<CssValue>& L = v.list;
            const size_t n = L.size();
            // CSS shorthand order: 1=all, 2={v,h}, 3={t,h,b}, 4={t,r,b,l}
            float p[4]{}, q[4]{};
            for (size_t i = 0; i < 4 && i < n; ++i) edge(L[i], p[i], q[i]);
            if (n == 1)
            {
                px[0] = px[1] = px[2] = px[3] = p[0];
                pct[0] = pct[1] = pct[2] = pct[3] = q[0];
            }
            else if (n == 2)
            {
                px[0] = px[2] = p[0];
                px[1] = px[3] = p[1];
                pct[0] = pct[2] = q[0];
                pct[1] = pct[3] = q[1];
            }
            else if (n == 3)
            {
                px[0] = p[0];
                px[1] = px[3] = p[1];
                px[2] = p[2];
                pct[0] = q[0];
                pct[1] = pct[3] = q[1];
                pct[2] = q[2];
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    px[i] = p[i];
                    pct[i] = q[i];
                }
            }
            out.px = Spacing(px[0], px[1], px[2], px[3]);
            out.pct = Spacing(pct[0], pct[1], pct[2], pct[3]);
            return out;
        }
        if (v.kind == ValueKind::Length || v.kind == ValueKind::Number)
        {
            float p = 0.f, q = 0.f;
            edge(v, p, q);
            out.px = Spacing(p);
            out.pct = Spacing(q);
            return out;
        }
        return out;
    }

    namespace
    {
        [[nodiscard]] SpacingSplit ResolveSpacing(const ComputedStyle& cs, StringId shorthand,
                                                  const std::array<StringId, 4>& longhands, Spacing fbPx)
        {
            SpacingSplit out = cs.Get(shorthand) ? ToSpacingSplit(*cs.Get(shorthand), fbPx) : SpacingSplit{fbPx, {}};
            const std::array<float*, 4> px{&out.px.top, &out.px.right, &out.px.bottom, &out.px.left};
            const std::array<float*, 4> pct{&out.pct.top, &out.pct.right, &out.pct.bottom, &out.pct.left};
            for (size_t side = 0; side < longhands.size(); ++side)
            {
                if (const CssValue* v = cs.Get(longhands[side])) EdgePx(*v, *px[side], *pct[side]);
            }
            return out;
        }
    } // namespace

    SpacingSplit ToPaddingSplit(const ComputedStyle& cs, Spacing fbPx)
    {
        return ResolveSpacing(cs, "padding"_sid,
                              {"padding-top"_sid, "padding-right"_sid, "padding-bottom"_sid, "padding-left"_sid}, fbPx);
    }

    SpacingSplit ToMarginSplit(const ComputedStyle& cs, Spacing fbPx)
    {
        return ResolveSpacing(cs, "margin"_sid,
                              {"margin-top"_sid, "margin-right"_sid, "margin-bottom"_sid, "margin-left"_sid}, fbPx);
    }

    GapSplit ToGapSplit(const CssValue& v, float fbPx)
    {
        if (v.kind == ValueKind::Length)
        {
            if (v.unit == Unit::Percent) return {0.f, v.num};
            return {v.num, 0.f};
        }
        if (v.kind == ValueKind::Number) return {v.num, 0.f};
        return {fbPx, 0.f};
    }

    TransformValue ToTransform(const CssValue& v)
    {
        TransformValue t;
        const auto factor = [](const CssValue& a)
        {
            if (a.kind == ValueKind::Length && a.unit == Unit::Percent) return a.num / 100.f;
            return a.kind == ValueKind::Number ? a.num : 1.f;
        };
        const auto radians = [](const CssValue& a)
        {
            constexpr float pi = 3.14159265358979f;
            if (a.unit == Unit::Deg) return a.num * pi / 180.f;
            if (a.unit == Unit::Turn) return a.num * 2.f * pi;
            return a.num; // rad, or a bare 0
        };
        const auto handleFn = [&](const CssValue& fn)
        {
            if (fn.kind != ValueKind::Function || fn.list.empty()) return;
            const CssValue& a0 = fn.list[0];
            if (fn.keyword == "translatex"_sid) t.x += ToPx(a0, 0.f);
            else if (fn.keyword == "translatey"_sid) t.y += ToPx(a0, 0.f);
            else if (fn.keyword == "translate"_sid)
            {
                t.x += ToPx(a0, 0.f);
                if (fn.list.size() >= 2) t.y += ToPx(fn.list[1], 0.f);
            }
            else if (fn.keyword == "scalex"_sid) t.scaleX *= factor(a0);
            else if (fn.keyword == "scaley"_sid) t.scaleY *= factor(a0);
            else if (fn.keyword == "scale"_sid)
            {
                const float sx = factor(a0);
                t.scaleX *= sx;
                t.scaleY *= fn.list.size() >= 2 ? factor(fn.list[1]) : sx;
            }
            else if (fn.keyword == "rotate"_sid) t.rotate += radians(a0);
        };
        if (v.kind == ValueKind::Function) handleFn(v);
        else if (v.kind == ValueKind::List)
            for (const CssValue& c : v.list) handleFn(c);
        return t;
    }

    Vec2 ToTransformOrigin(const CssValue& v, Vec2 fb)
    {
        // One component: a keyword names its axis (top/bottom → y); a lone percentage is x.
        const auto one = [](const CssValue& c, float& x, float& y, bool second)
        {
            if (c.kind == ValueKind::Length && c.unit == Unit::Percent) (second ? y : x) = c.num / 100.f;
            else if (KeywordIs(c, "left"_sid)) x = 0.f;
            else if (KeywordIs(c, "right"_sid)) x = 1.f;
            else if (KeywordIs(c, "top"_sid)) y = 0.f;
            else if (KeywordIs(c, "bottom"_sid)) y = 1.f;
        };
        Vec2 o = fb;
        if (v.kind == ValueKind::List && !v.comma)
        {
            o = {0.5f, 0.5f};
            for (std::size_t i = 0; i < v.list.size() && i < 2; ++i) one(v.list[i], o.x, o.y, i == 1);
        }
        else if (v.kind == ValueKind::Length || v.kind == ValueKind::Keyword)
        {
            o = {0.5f, 0.5f};
            one(v, o.x, o.y, false);
        }
        return o;
    }

    namespace
    {
        [[nodiscard]] TrackSize ToTrack(const CssValue& c)
        {
            if (c.kind == ValueKind::Length)
            {
                if (c.unit == Unit::Fr) return TrackSize::MakeFr(c.num);
                if (c.unit == Unit::Percent) return TrackSize::MakeFr(c.num / 100.f); // % track ≈ fr
                return TrackSize::MakePx(c.num);
            }
            if (c.kind == ValueKind::Number) return TrackSize::MakeFr(c.num);
            return TrackSize::MakeFr(1.f); // auto / min-content / max-content → 1fr fallback
        }

        void AppendTracks(const CssValue& c, std::vector<TrackSize>& out)
        {
            if (c.kind == ValueKind::List && !c.comma)
            {
                for (const CssValue& x : c.list) AppendTracks(x, out);
                return;
            }
            if (c.kind == ValueKind::Function && c.keyword == "repeat"_sid&& c.list.size() >= 2)
            {
                const int n = static_cast<int>(c.list[0].num);
                for (int r = 0; r < n && r < 4096; ++r)
                    for (std::size_t k = 1; k < c.list.size(); ++k) AppendTracks(c.list[k], out);
                return;
            }
            if (c.Valid()) out.push_back(ToTrack(c));
        }
    } // namespace

    std::vector<TrackSize> ToTrackList(const CssValue& v)
    {
        std::vector<TrackSize> out;
        // `none` (or an invalid value) yields no tracks.
        if (v.kind == ValueKind::Keyword && v.keyword == "none"_sid) return out;
        AppendTracks(v, out);
        return out;
    }

    GridLine ToGridLine(const CssValue& v)
    {
        GridLine g;
        std::vector<const CssValue*> toks;
        if (v.kind == ValueKind::List && !v.comma)
            for (const CssValue& c : v.list) toks.push_back(&c);
        else if (v.Valid())
            toks.push_back(&v);

        std::size_t slash = toks.size();
        for (std::size_t i = 0; i < toks.size(); ++i)
            if (toks[i]->kind == ValueKind::Keyword && toks[i]->keyword == "/"_sid)
        {
            slash = i;
            break;
        }

        const auto parse = [&](std::size_t begin, std::size_t end, bool& isSpan, int& value, bool& hasValue)
        {
            isSpan = false;
            value = 0;
            hasValue = false;
            for (std::size_t i = begin; i < end; ++i)
            {
                const CssValue& c = *toks[i];
                if (c.kind == ValueKind::Keyword && c.keyword == "span"_sid) isSpan = true;
                else if (c.kind == ValueKind::Number)
                {
                    value = static_cast<int>(c.num);
                    hasValue = true;
                }
            }
        };

        bool sSpan, sHas;
        int sVal;
        parse(0, slash, sSpan, sVal, sHas);
        if (sSpan)
        {
            g.start = -1;
            g.span = sVal > 0 ? sVal : 1;
            return g;
        } // `span N`
        if (sHas) g.start = sVal > 0 ? sVal - 1 : 0; // 1-based line → 0-based track

        if (slash < toks.size())
        {
            bool eSpan, eHas;
            int eVal;
            parse(slash + 1, toks.size(), eSpan, eVal, eHas);
            if (eSpan) g.span = eVal > 0 ? eVal : 1;
            else if (eHas && g.start >= 0) g.span = (eVal - 1) - g.start > 0 ? (eVal - 1) - g.start : 1;
        }
        return g;
    }

    Border ToBorder(const ComputedStyle& cs, Border fb)
    {
        Border b = fb;
        bool touched = false;

        if (const CssValue* v = cs.Get("border"_sid))
        {
            touched = true;
            const auto scan = [&](const CssValue& c)
            {
                if (c.kind == ValueKind::Length) b.width = ToPx(c, b.width);
                else if (c.kind == ValueKind::Color) b.color = c.color;
                else if (c.kind == ValueKind::Keyword)
                {
                    if (c.keyword == "dashed"_sid) b.dashed = true;
                    else if (c.keyword == "solid"_sid) b.dashed = false;
                    else if (c.keyword == "none"_sid) b.width = 0.f;
                }
            };
            if (v->kind == ValueKind::List)
            {
                for (const CssValue& c : v->list) scan(c);
            }
            else
            {
                scan(*v);
            }
        }

        if (const CssValue* w = cs.Get("border-width"_sid))
        {
            touched = true;
            if (w->kind == ValueKind::List && !w->comma && !w->list.empty())
            {
                const auto& L = w->list;
                const size_t n = L.size();
                float p[4]{};
                for (size_t i = 0; i < 4 && i < n; ++i) p[i] = ToPx(L[i], b.width);
                if (n == 1)
                {
                    b.top = b.right = b.bottom = b.left = p[0];
                }
                else if (n == 2)
                {
                    b.top = b.bottom = p[0];
                    b.right = b.left = p[1];
                }
                else if (n == 3)
                {
                    b.top = p[0];
                    b.right = b.left = p[1];
                    b.bottom = p[2];
                }
                else
                {
                    b.top = p[0];
                    b.right = p[1];
                    b.bottom = p[2];
                    b.left = p[3];
                }
            }
            else
            {
                b.width = ToPx(*w, b.width);
                b.top = b.right = b.bottom = b.left = b.width;
            }
        }

        if (const CssValue* c = cs.Get("border-color"_sid))
        {
            touched = true;
            if (c->kind == ValueKind::List && !c->comma && !c->list.empty())
            {
                const auto& L = c->list;
                const size_t n = L.size();
                Color col[4]{};
                for (size_t i = 0; i < 4 && i < n; ++i) col[i] = ToColor(L[i], b.color);
                if (n == 1)
                {
                    b.topColor = b.rightColor = b.bottomColor = b.leftColor = col[0];
                }
                else if (n == 2)
                {
                    b.topColor = b.bottomColor = col[0];
                    b.rightColor = b.leftColor = col[1];
                }
                else if (n == 3)
                {
                    b.topColor = col[0];
                    b.rightColor = b.leftColor = col[1];
                    b.bottomColor = col[2];
                }
                else
                {
                    b.topColor = col[0];
                    b.rightColor = col[1];
                    b.bottomColor = col[2];
                    b.leftColor = col[3];
                }
                b.hasTopColor = b.hasRightColor = b.hasBottomColor = b.hasLeftColor = true;
            }
            else
            {
                b.color = ToColor(*c, b.color);
                b.topColor = b.rightColor = b.bottomColor = b.leftColor = b.color;
                b.hasTopColor = b.hasRightColor = b.hasBottomColor = b.hasLeftColor = true;
            }
        }

        if (const CssValue* s = cs.Get("border-style"_sid))
        {
            touched = true;
            if (s->kind == ValueKind::List && !s->comma && !s->list.empty())
            {
                const auto& L = s->list;
                const size_t n = L.size();
                bool dsh[4]{};
                for (size_t i = 0; i < 4 && i < n; ++i) dsh[i] = ToBorderDashed(L[i], b.dashed);
                if (n == 1)
                {
                    b.topDashed = b.rightDashed = b.bottomDashed = b.leftDashed = dsh[0];
                }
                else if (n == 2)
                {
                    b.topDashed = b.bottomDashed = dsh[0];
                    b.rightDashed = b.leftDashed = dsh[1];
                }
                else if (n == 3)
                {
                    b.topDashed = dsh[0];
                    b.rightDashed = b.leftDashed = dsh[1];
                    b.bottomDashed = dsh[2];
                }
                else
                {
                    b.topDashed = dsh[0];
                    b.rightDashed = dsh[1];
                    b.bottomDashed = dsh[2];
                    b.leftDashed = dsh[3];
                }
                b.hasTopStyle = b.hasRightStyle = b.hasBottomStyle = b.hasLeftStyle = true;
            }
            else
            {
                b.dashed = ToBorderDashed(*s, b.dashed);
                b.topDashed = b.rightDashed = b.bottomDashed = b.leftDashed = b.dashed;
                b.hasTopStyle = b.hasRightStyle = b.hasBottomStyle = b.hasLeftStyle = true;
            }
        }

        auto parseSideShorthand = [&](const CssValue* v, float& width, Color& color, bool& hasColor,
                                      bool& dashed, bool& hasStyle, float fbW, Color fbC, bool fbD)
        {
            if (!v) return;
            touched = true;
            const auto scan = [&](const CssValue& item)
            {
                if (item.kind == ValueKind::Length) width = ToPx(item, fbW);
                else if (item.kind == ValueKind::Color) { color = item.color; hasColor = true; }
                else if (item.kind == ValueKind::Keyword)
                {
                    if (item.keyword == "dashed"_sid) { dashed = true; hasStyle = true; }
                    else if (item.keyword == "solid"_sid) { dashed = false; hasStyle = true; }
                    else if (item.keyword == "none"_sid) { width = 0.f; hasStyle = true; }
                }
            };
            if (v->kind == ValueKind::List)
            {
                for (const CssValue& item : v->list) scan(item);
            }
            else
            {
                scan(*v);
            }
        };

        parseSideShorthand(cs.Get("border-top"_sid), b.top, b.topColor, b.hasTopColor, b.topDashed, b.hasTopStyle,
                           b.WidthTop(), b.ColorTop(), b.DashedTop());
        parseSideShorthand(cs.Get("border-right"_sid), b.right, b.rightColor, b.hasRightColor, b.rightDashed, b.hasRightStyle,
                           b.WidthRight(), b.ColorRight(), b.DashedRight());
        parseSideShorthand(cs.Get("border-bottom"_sid), b.bottom, b.bottomColor, b.hasBottomColor, b.bottomDashed, b.hasBottomStyle,
                           b.WidthBottom(), b.ColorBottom(), b.DashedBottom());
        parseSideShorthand(cs.Get("border-left"_sid), b.left, b.leftColor, b.hasLeftColor, b.leftDashed, b.hasLeftStyle,
                           b.WidthLeft(), b.ColorLeft(), b.DashedLeft());

        if (const CssValue* v = cs.Get("border-top-width"_sid)) { b.top = ToPx(*v, b.WidthTop()); touched = true; }
        if (const CssValue* v = cs.Get("border-top-color"_sid)) { b.topColor = ToColor(*v, b.ColorTop()); b.hasTopColor = true; touched = true; }
        if (const CssValue* v = cs.Get("border-top-style"_sid)) { b.topDashed = ToBorderDashed(*v, b.DashedTop()); b.hasTopStyle = true; touched = true; }

        if (const CssValue* v = cs.Get("border-right-width"_sid)) { b.right = ToPx(*v, b.WidthRight()); touched = true; }
        if (const CssValue* v = cs.Get("border-right-color"_sid)) { b.rightColor = ToColor(*v, b.ColorRight()); b.hasRightColor = true; touched = true; }
        if (const CssValue* v = cs.Get("border-right-style"_sid)) { b.rightDashed = ToBorderDashed(*v, b.DashedRight()); b.hasRightStyle = true; touched = true; }

        if (const CssValue* v = cs.Get("border-bottom-width"_sid)) { b.bottom = ToPx(*v, b.WidthBottom()); touched = true; }
        if (const CssValue* v = cs.Get("border-bottom-color"_sid)) { b.bottomColor = ToColor(*v, b.ColorBottom()); b.hasBottomColor = true; touched = true; }
        if (const CssValue* v = cs.Get("border-bottom-style"_sid)) { b.bottomDashed = ToBorderDashed(*v, b.DashedBottom()); b.hasBottomStyle = true; touched = true; }

        if (const CssValue* v = cs.Get("border-left-width"_sid)) { b.left = ToPx(*v, b.WidthLeft()); touched = true; }
        if (const CssValue* v = cs.Get("border-left-color"_sid)) { b.leftColor = ToColor(*v, b.ColorLeft()); b.hasLeftColor = true; touched = true; }
        if (const CssValue* v = cs.Get("border-left-style"_sid)) { b.leftDashed = ToBorderDashed(*v, b.DashedLeft()); b.hasLeftStyle = true; touched = true; }

        if (b.IsUniform())
        {
            b.width = b.WidthTop();
            b.color = b.ColorTop();
            b.dashed = b.DashedTop();
        }

        return touched ? b : fb;
    }
} // namespace lumora::style
