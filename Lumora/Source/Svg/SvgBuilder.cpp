#include "Svg/SvgBuilder.h"

#include "Svg/AttrParser.h"
#include "Svg/PathParser.h"
#include "Svg/XmlReader.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lumora::svg
{
    namespace
    {
        constexpr float kKappa = 0.5522847498307936f; ///< circle→cubic control-point factor

        void BakePath(RawPath& p, const Affine& m)
        {
            for (SubPath& sub : p.subs)
            {
                sub.start = m.Apply(sub.start);
                for (PathSeg& seg : sub.segs)
                {
                    if (seg.type == PathSeg::Type::Cubic)
                    {
                        seg.c1 = m.Apply(seg.c1);
                        seg.c2 = m.Apply(seg.c2);
                    }
                    seg.p = m.Apply(seg.p);
                }
            }
        }

        void AppendCubic(SubPath& s, Vec2 c1, Vec2 c2, Vec2 p) { s.segs.push_back({PathSeg::Type::Cubic, c1, c2, p}); }
        void AppendLine(SubPath& s, Vec2 p) { s.segs.push_back({PathSeg::Type::Line, {}, {}, p}); }

        SubPath EllipseSub(float cx, float cy, float rx, float ry)
        {
            SubPath s;
            s.start = {cx + rx, cy};
            const float ox = rx * kKappa;
            const float oy = ry * kKappa;
            AppendCubic(s, {cx + rx, cy + oy}, {cx + ox, cy + ry}, {cx, cy + ry});
            AppendCubic(s, {cx - ox, cy + ry}, {cx - rx, cy + oy}, {cx - rx, cy});
            AppendCubic(s, {cx - rx, cy - oy}, {cx - ox, cy - ry}, {cx, cy - ry});
            AppendCubic(s, {cx + ox, cy - ry}, {cx + rx, cy - oy}, {cx + rx, cy});
            s.closed = true;
            return s;
        }

        RawPath RectPath(float x, float y, float w, float h, float rx, float ry)
        {
            RawPath p;
            SubPath s;
            if (rx <= 0.f && ry <= 0.f)
            {
                s.start = {x, y};
                AppendLine(s, {x + w, y});
                AppendLine(s, {x + w, y + h});
                AppendLine(s, {x, y + h});
                s.closed = true;
                p.subs.push_back(std::move(s));
                return p;
            }
            rx = std::min(rx, w * 0.5f);
            ry = std::min(ry, h * 0.5f);
            const float ox = rx * kKappa;
            const float oy = ry * kKappa;
            s.start = {x + rx, y};
            AppendLine(s, {x + w - rx, y});
            AppendCubic(s, {x + w - rx + ox, y}, {x + w, y + ry - oy}, {x + w, y + ry});
            AppendLine(s, {x + w, y + h - ry});
            AppendCubic(s, {x + w, y + h - ry + oy}, {x + w - rx + ox, y + h}, {x + w - rx, y + h});
            AppendLine(s, {x + rx, y + h});
            AppendCubic(s, {x + rx - ox, y + h}, {x, y + h - ry + oy}, {x, y + h - ry});
            AppendLine(s, {x, y + ry});
            AppendCubic(s, {x, y + ry - oy}, {x + rx - ox, y}, {x + rx, y});
            s.closed = true;
            p.subs.push_back(std::move(s));
            return p;
        }

        RawPath PolyPath(const std::vector<float>& nums, bool close)
        {
            RawPath p;
            if (nums.size() < 4)
            {
                return p;
            }
            SubPath s;
            s.start = {nums[0], nums[1]};
            for (std::size_t i = 2; i + 1 < nums.size(); i += 2)
            {
                AppendLine(s, {nums[i], nums[i + 1]});
            }
            s.closed = close;
            p.subs.push_back(std::move(s));
            return p;
        }

        [[nodiscard]] std::string_view TrimSv(std::string_view s) noexcept
        {
            std::size_t a = 0, b = s.size();
            while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
            while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
            return s.substr(a, b - a);
        }

        /// CSS declarations from a `style="..."` attribute, plus fall-through to presentation attributes.
        struct PropSet
        {
            const XmlNode* node = nullptr;
            std::vector<std::pair<std::string, std::string>> style;

            [[nodiscard]] std::optional<std::string_view> Get(std::string_view key) const
            {
                for (const auto& kv : style)
                {
                    if (kv.first == key) return std::string_view(kv.second);
                }
                if (const std::string* a = node->Attr(key)) return std::string_view(*a);
                return std::nullopt;
            }
        };

        [[nodiscard]] PropSet MakeProps(const XmlNode& n)
        {
            PropSet ps;
            ps.node = &n;
            if (const std::string* style = n.Attr("style"))
            {
                std::string_view s = *style;
                std::size_t i = 0;
                while (i < s.size())
                {
                    const std::size_t semi = s.find(';', i);
                    const std::string_view decl = s.substr(i, semi == std::string_view::npos ? std::string_view::npos : semi - i);
                    const std::size_t colon = decl.find(':');
                    if (colon != std::string_view::npos)
                    {
                        const std::string_view k = TrimSv(decl.substr(0, colon));
                        const std::string_view v = TrimSv(decl.substr(colon + 1));
                        if (!k.empty()) ps.style.emplace_back(std::string(k), std::string(v));
                    }
                    if (semi == std::string_view::npos) break;
                    i = semi + 1;
                }
            }
            return ps;
        }

        struct InheritState
        {
            Affine ctm;
            PaintSpec fill{PaintKind::Solid, Color::Black, {}};
            PaintSpec stroke{PaintKind::None, {}, {}};
            float fillOpacity = 1.f;
            float strokeOpacity = 1.f;
            float strokeWidth = 1.f;
            LineCapStyle cap = LineCapStyle::Butt;
            LineJoinStyle join = LineJoinStyle::Miter;
            float miterLimit = 4.f;
            std::vector<float> dash;
            float dashOffset = 0.f;
            FillRule fillRule = FillRule::NonZero;
            Color currentColor = Color::Black;
        };

        [[nodiscard]] float ClampUnit(float v) noexcept { return std::clamp(v, 0.f, 1.f); }

        class Builder
        {
        public:
            SvgScene Build(const XmlNode& root)
            {
                IndexIds(root);
                ParseRootViewport(root);

                InheritState st;
                const float diag = std::sqrt(m_vbSize.x * m_vbSize.x + m_vbSize.y * m_vbSize.y) / std::sqrt(2.f);
                (void)diag;
                Walk(root, st, 0);
                m_scene.valid = true;
                return std::move(m_scene);
            }

        private:
            SvgScene m_scene;
            Vec2 m_vbSize{0.f, 0.f};
            std::unordered_map<std::string, const XmlNode*> m_byId;
            std::unordered_map<std::string, int> m_gradientIndex;
            std::unordered_map<std::string, int> m_clipIndex;
            std::unordered_set<std::string> m_gradientResolving;

            void IndexIds(const XmlNode& n)
            {
                if (const std::string* id = n.Attr("id"); id && !id->empty())
                {
                    m_byId.emplace(*id, &n);
                }
                for (const XmlNode& c : n.children)
                {
                    IndexIds(c);
                }
            }

            void ParseRootViewport(const XmlNode& root)
            {
                Vec2 vbMin{0.f, 0.f}, vbSize{0.f, 0.f};
                bool haveVb = false;
                if (const std::string* vb = root.Attr("viewBox"))
                {
                    haveVb = ParseViewBox(*vb, vbMin, vbSize);
                }
                float w = 0.f, h = 0.f;
                if (const std::string* ws = root.Attr("width")) w = ParseLength(*ws, vbSize.x, 0.f);
                if (const std::string* hs = root.Attr("height")) h = ParseLength(*hs, vbSize.y, 0.f);

                if (haveVb)
                {
                    m_scene.viewBoxMin = vbMin;
                    m_scene.viewBoxSize = vbSize;
                }
                else if (w > 0.f && h > 0.f)
                {
                    m_scene.viewBoxMin = {0.f, 0.f};
                    m_scene.viewBoxSize = {w, h};
                }
                m_vbSize = m_scene.viewBoxSize;

                if (w > 0.f && h > 0.f)
                {
                    m_scene.intrinsic = {w, h};
                }
                else
                {
                    m_scene.intrinsic = m_scene.viewBoxSize;
                }

                if (const std::string* op = root.Attr("opacity"))
                {
                    float v = 1.f;
                    if (ParseNumber(*op, v)) m_scene.docOpacity = ClampUnit(v);
                }
            }

            void ApplyProps(const PropSet& ps, InheritState& st)
            {
                const auto num = [&](std::string_view key, float& dst) {
                    if (auto v = ps.Get(key)) { float f = dst; if (ParseNumber(*v, f)) dst = f; }
                };
                if (auto v = ps.Get("color"))
                {
                    if (auto c = ParseColor(*v, st.currentColor)) st.currentColor = *c;
                }
                if (auto v = ps.Get("fill")) st.fill = ParsePaint(*v, st.currentColor);
                if (auto v = ps.Get("stroke")) st.stroke = ParsePaint(*v, st.currentColor);
                if (auto v = ps.Get("fill-opacity")) { float f = 1.f; if (ParseNumber(*v, f)) st.fillOpacity = ClampUnit(f); }
                if (auto v = ps.Get("stroke-opacity")) { float f = 1.f; if (ParseNumber(*v, f)) st.strokeOpacity = ClampUnit(f); }
                if (auto v = ps.Get("stroke-width")) st.strokeWidth = ParseLength(*v, m_vbSize.x, st.strokeWidth);
                num("stroke-miterlimit", st.miterLimit);
                if (auto v = ps.Get("fill-rule")) st.fillRule = (TrimSv(*v) == "evenodd") ? FillRule::EvenOdd : FillRule::NonZero;
                if (auto v = ps.Get("stroke-linecap"))
                {
                    const std::string_view t = TrimSv(*v);
                    st.cap = t == "round" ? LineCapStyle::Round : t == "square" ? LineCapStyle::Square : LineCapStyle::Butt;
                }
                if (auto v = ps.Get("stroke-linejoin"))
                {
                    const std::string_view t = TrimSv(*v);
                    st.join = t == "round" ? LineJoinStyle::Round : t == "bevel" ? LineJoinStyle::Bevel : LineJoinStyle::Miter;
                }
                if (auto v = ps.Get("stroke-dasharray"))
                {
                    if (TrimSv(*v) == "none") st.dash.clear();
                    else st.dash = ParseNumberList(*v);
                }
                if (auto v = ps.Get("stroke-dashoffset")) st.dashOffset = ParseLength(*v, m_vbSize.x, 0.f);
            }

            [[nodiscard]] Paint ToPaint(const PaintSpec& spec, float opacity)
            {
                Paint p;
                p.opacity = opacity;
                if (spec.kind == PaintKind::Solid)
                {
                    p.kind = PaintKind::Solid;
                    p.solid = spec.color;
                }
                else if (spec.kind == PaintKind::Gradient)
                {
                    const int gi = ResolveGradient(spec.ref);
                    if (gi >= 0)
                    {
                        p.kind = PaintKind::Gradient;
                        p.gradient = gi;
                    }
                }
                return p;
            }

            void EmitShape(RawPath local, const InheritState& st, bool canFill, bool canStroke)
            {
                if (local.Empty())
                {
                    return;
                }
                BakePath(local, st.ctm);

                if (canFill && st.fill.kind != PaintKind::None)
                {
                    const Paint fp = ToPaint(st.fill, st.fillOpacity);
                    if (fp.kind != PaintKind::None)
                    {
                        DrawOp op;
                        op.type = OpType::Fill;
                        op.path = local;
                        op.paint = fp;
                        op.fillRule = st.fillRule;
                        m_scene.ops.push_back(std::move(op));
                    }
                }
                if (canStroke && st.stroke.kind != PaintKind::None && st.strokeWidth > 0.f)
                {
                    const Paint sp = ToPaint(st.stroke, st.strokeOpacity);
                    if (sp.kind != PaintKind::None)
                    {
                        DrawOp op;
                        op.type = OpType::Stroke;
                        op.path = std::move(local);
                        op.paint = sp;
                        op.stroke = {st.strokeWidth, st.cap, st.join, st.miterLimit, st.dash, st.dashOffset};
                        m_scene.ops.push_back(std::move(op));
                    }
                }
            }

            [[nodiscard]] RawPath ShapeToPath(const XmlNode& n, bool& canFill)
            {
                canFill = true;
                const std::string& tag = n.tag;
                const auto len = [&](std::string_view k, float ref, float fb) -> float {
                    if (const std::string* v = n.Attr(k)) return ParseLength(*v, ref, fb);
                    return fb;
                };
                if (tag == "rect")
                {
                    const float x = len("x", m_vbSize.x, 0.f);
                    const float y = len("y", m_vbSize.y, 0.f);
                    const float w = len("width", m_vbSize.x, 0.f);
                    const float h = len("height", m_vbSize.y, 0.f);
                    if (w <= 0.f || h <= 0.f) return {};
                    float rx = n.Attr("rx") ? len("rx", m_vbSize.x, 0.f) : -1.f;
                    float ry = n.Attr("ry") ? len("ry", m_vbSize.y, 0.f) : -1.f;
                    if (rx < 0.f && ry >= 0.f) rx = ry;
                    if (ry < 0.f && rx >= 0.f) ry = rx;
                    if (rx < 0.f) rx = 0.f;
                    if (ry < 0.f) ry = 0.f;
                    return RectPath(x, y, w, h, rx, ry);
                }
                if (tag == "circle")
                {
                    const float r = len("r", m_vbSize.x, 0.f);
                    if (r <= 0.f) return {};
                    RawPath p;
                    p.subs.push_back(EllipseSub(len("cx", m_vbSize.x, 0.f), len("cy", m_vbSize.y, 0.f), r, r));
                    return p;
                }
                if (tag == "ellipse")
                {
                    const float rx = len("rx", m_vbSize.x, 0.f);
                    const float ry = len("ry", m_vbSize.y, 0.f);
                    if (rx <= 0.f || ry <= 0.f) return {};
                    RawPath p;
                    p.subs.push_back(EllipseSub(len("cx", m_vbSize.x, 0.f), len("cy", m_vbSize.y, 0.f), rx, ry));
                    return p;
                }
                if (tag == "line")
                {
                    canFill = false;
                    RawPath p;
                    SubPath s;
                    s.start = {len("x1", m_vbSize.x, 0.f), len("y1", m_vbSize.y, 0.f)};
                    AppendLine(s, {len("x2", m_vbSize.x, 0.f), len("y2", m_vbSize.y, 0.f)});
                    p.subs.push_back(std::move(s));
                    return p;
                }
                if (tag == "polyline" || tag == "polygon")
                {
                    std::vector<float> nums;
                    if (const std::string* pts = n.Attr("points")) nums = ParseNumberList(*pts);
                    return PolyPath(nums, tag == "polygon");
                }
                if (tag == "path")
                {
                    if (const std::string* d = n.Attr("d")) return ParsePathData(*d);
                    return {};
                }
                return {};
            }

            [[nodiscard]] bool IsShape(const std::string& tag) const
            {
                return tag == "rect" || tag == "circle" || tag == "ellipse" || tag == "line"
                    || tag == "polyline" || tag == "polygon" || tag == "path";
            }

            void Walk(const XmlNode& n, InheritState st, int useDepth)
            {
                const PropSet ps = MakeProps(n);

                if (auto d = ps.Get("display"); d && TrimSv(*d) == "none")
                {
                    return;
                }
                if (auto v = ps.Get("visibility"); v && TrimSv(*v) == "hidden")
                {
                    return;
                }

                if (const std::string* tr = n.Attr("transform"))
                {
                    st.ctm = st.ctm.Compose(ParseTransform(*tr));
                }
                ApplyProps(ps, st);

                float elemOpacity = 1.f;
                if (auto v = ps.Get("opacity")) { float f = 1.f; if (ParseNumber(*v, f)) elemOpacity = ClampUnit(f); }

                int clipIndex = -1;
                if (auto cp = ps.Get("clip-path"))
                {
                    const PaintSpec ref = ParsePaint(*cp, st.currentColor);
                    if (ref.kind == PaintKind::Gradient && !ref.ref.empty())
                    {
                        clipIndex = ResolveClip(ref.ref);
                    }
                }

                const bool pushOpacity = elemOpacity < 1.f;
                if (pushOpacity)
                {
                    DrawOp op;
                    op.type = OpType::PushGroupOpacity;
                    op.groupOpacity = elemOpacity;
                    m_scene.ops.push_back(op);
                }
                if (clipIndex >= 0)
                {
                    DrawOp op;
                    op.type = OpType::PushClip;
                    op.clip = clipIndex;
                    m_scene.ops.push_back(op);
                }

                const std::string& tag = n.tag;
                if (IsShape(tag))
                {
                    bool canFill = true;
                    RawPath path = ShapeToPath(n, canFill);
                    EmitShape(std::move(path), st, canFill, true);
                }
                else if (tag == "use")
                {
                    if (useDepth < 16)
                    {
                        EmitUse(n, st, useDepth);
                    }
                }
                else if (tag == "g" || tag == "svg" || tag == "a")
                {
                    for (const XmlNode& c : n.children)
                    {
                        Walk(c, st, useDepth);
                    }
                }
                // defs / clipPath / gradients / symbol / metadata / title / desc / style: not drawn here.

                if (clipIndex >= 0)
                {
                    DrawOp op;
                    op.type = OpType::PopClip;
                    m_scene.ops.push_back(op);
                }
                if (pushOpacity)
                {
                    DrawOp op;
                    op.type = OpType::PopGroupOpacity;
                    m_scene.ops.push_back(op);
                }
            }

            void EmitUse(const XmlNode& n, InheritState st, int useDepth)
            {
                const std::string* href = n.Attr("href");
                if (!href) href = n.Attr("xlink:href");
                if (!href || href->empty() || href->front() != '#')
                {
                    return;
                }
                const auto it = m_byId.find(href->substr(1));
                if (it == m_byId.end())
                {
                    return;
                }
                const float x = n.Attr("x") ? ParseLength(*n.Attr("x"), m_vbSize.x, 0.f) : 0.f;
                const float y = n.Attr("y") ? ParseLength(*n.Attr("y"), m_vbSize.y, 0.f) : 0.f;
                if (x != 0.f || y != 0.f)
                {
                    st.ctm = st.ctm.Compose(Affine{1.f, 0.f, 0.f, 1.f, x, y});
                }
                const XmlNode& target = *it->second;
                if (target.tag == "symbol" || target.tag == "svg")
                {
                    for (const XmlNode& c : target.children)
                    {
                        Walk(c, st, useDepth + 1);
                    }
                }
                else
                {
                    Walk(target, st, useDepth + 1);
                }
            }

            [[nodiscard]] int ResolveClip(const std::string& id)
            {
                if (const auto it = m_clipIndex.find(id); it != m_clipIndex.end())
                {
                    return it->second;
                }
                const auto node = m_byId.find(id);
                if (node == m_byId.end() || node->second->tag != "clipPath")
                {
                    return -1;
                }
                ClipPath clip;
                clip.rule = FillRule::NonZero;
                Affine base;
                if (const std::string* tr = node->second->Attr("transform"))
                {
                    base = ParseTransform(*tr);
                }
                for (const XmlNode& c : node->second->children)
                {
                    if (!IsShape(c.tag) && c.tag != "use")
                    {
                        continue;
                    }
                    bool canFill = true;
                    RawPath cp = ShapeToPath(c, canFill);
                    Affine m = base;
                    if (const std::string* tr = c.Attr("transform"))
                    {
                        m = m.Compose(ParseTransform(*tr));
                    }
                    BakePath(cp, m);
                    for (SubPath& s : cp.subs)
                    {
                        clip.path.subs.push_back(std::move(s));
                    }
                }
                if (const std::string* fr = node->second->Attr("clip-rule"))
                {
                    if (TrimSv(*fr) == "evenodd") clip.rule = FillRule::EvenOdd;
                }
                const int index = static_cast<int>(m_scene.clips.size());
                m_scene.clips.push_back(std::move(clip));
                m_clipIndex.emplace(id, index);
                return index;
            }

            [[nodiscard]] int ResolveGradient(const std::string& id)
            {
                if (const auto it = m_gradientIndex.find(id); it != m_gradientIndex.end())
                {
                    return it->second;
                }
                const auto node = m_byId.find(id);
                if (node == m_byId.end())
                {
                    return -1;
                }
                const std::string& tag = node->second->tag;
                if (tag != "linearGradient" && tag != "radialGradient")
                {
                    return -1;
                }
                if (m_gradientResolving.contains(id))
                {
                    return -1; // href cycle
                }
                m_gradientResolving.insert(id);
                Gradient g = ParseGradientElement(*node->second);
                m_gradientResolving.erase(id);

                const int index = static_cast<int>(m_scene.gradients.size());
                m_scene.gradients.push_back(std::move(g));
                m_gradientIndex.emplace(id, index);
                return index;
            }

            [[nodiscard]] Gradient ParseGradientElement(const XmlNode& n)
            {
                Gradient g;
                g.kind = (n.tag == "radialGradient") ? GradientKind::Radial : GradientKind::Linear;

                // href inheritance: pull stops / attributes from a referenced gradient first.
                const std::string* href = n.Attr("href");
                if (!href) href = n.Attr("xlink:href");
                const XmlNode* parent = nullptr;
                if (href && !href->empty() && href->front() == '#')
                {
                    if (const auto it = m_byId.find(href->substr(1)); it != m_byId.end())
                    {
                        parent = it->second;
                    }
                }
                if (parent && (parent->tag == "linearGradient" || parent->tag == "radialGradient"))
                {
                    const std::string* pid = parent->Attr("id");
                    const std::string pidStr = pid ? *pid : std::string{};
                    if (pidStr.empty() || !m_gradientResolving.contains(pidStr))
                    {
                        if (!pidStr.empty()) m_gradientResolving.insert(pidStr);
                        Gradient inherited = ParseGradientElement(*parent);
                        if (!pidStr.empty()) m_gradientResolving.erase(pidStr);
                        g.spread = inherited.spread;
                        g.userSpace = inherited.userSpace;
                        g.transform = inherited.transform;
                        g.p0 = inherited.p0;
                        g.p1 = inherited.p1;
                        g.center = inherited.center;
                        g.focal = inherited.focal;
                        g.radius = inherited.radius;
                        g.stops = inherited.stops;
                    }
                }

                if (const std::string* u = n.Attr("gradientUnits"))
                {
                    g.userSpace = (TrimSv(*u) == "userSpaceOnUse");
                }
                if (const std::string* t = n.Attr("gradientTransform"))
                {
                    g.transform = ParseTransform(*t);
                }
                if (const std::string* sp = n.Attr("spreadMethod"))
                {
                    const std::string_view v = TrimSv(*sp);
                    g.spread = v == "reflect" ? SpreadMethod::Reflect : v == "repeat" ? SpreadMethod::Repeat : SpreadMethod::Pad;
                }

                const float refX = g.userSpace ? m_vbSize.x : 1.f;
                const float refY = g.userSpace ? m_vbSize.y : 1.f;
                const auto coord = [&](std::string_view key, float ref, float fallback) -> float {
                    if (const std::string* v = n.Attr(key)) return ParseLength(*v, ref, fallback);
                    return fallback;
                };

                if (g.kind == GradientKind::Linear)
                {
                    g.p0 = {coord("x1", refX, g.p0.x), coord("y1", refY, g.p0.y)};
                    g.p1 = {coord("x2", refX, n.Attr("x2") ? g.p1.x : (g.userSpace ? m_vbSize.x : 1.f)),
                            coord("y2", refY, g.p1.y)};
                    if (!n.Attr("x2") && !parent) g.p1.x = g.userSpace ? m_vbSize.x : 1.f;
                }
                else
                {
                    const float cx = coord("cx", refX, g.userSpace ? m_vbSize.x * 0.5f : 0.5f);
                    const float cy = coord("cy", refY, g.userSpace ? m_vbSize.y * 0.5f : 0.5f);
                    const float r = coord("r", refX, g.userSpace ? m_vbSize.x * 0.5f : 0.5f);
                    g.center = {cx, cy};
                    g.radius = r;
                    g.focal = {n.Attr("fx") ? coord("fx", refX, cx) : cx, n.Attr("fy") ? coord("fy", refY, cy) : cy};
                }

                std::vector<GradientStop> stops;
                for (const XmlNode& c : n.children)
                {
                    if (c.tag != "stop")
                    {
                        continue;
                    }
                    const PropSet sp = MakeProps(c);
                    GradientStop stop;
                    if (auto o = sp.Get("offset"))
                    {
                        stop.offset = ClampUnit(ParseLength(*o, 1.f, 0.f));
                    }
                    Color col = Color::Black;
                    if (auto sc = sp.Get("stop-color"))
                    {
                        if (auto c2 = ParseColor(*sc, Color::Black)) col = *c2;
                    }
                    if (auto so = sp.Get("stop-opacity"))
                    {
                        float a = 1.f;
                        if (ParseNumber(*so, a))
                        {
                            col = col.WithAlpha(static_cast<std::uint8_t>(ClampUnit(a) * static_cast<float>(col.a) + 0.5f));
                        }
                    }
                    stop.color = col;
                    stops.push_back(stop);
                }
                if (!stops.empty())
                {
                    g.stops = std::move(stops);
                }
                std::stable_sort(g.stops.begin(), g.stops.end(),
                                 [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
                return g;
            }
        };
    } // namespace

    SvgScene BuildScene(const XmlNode& root)
    {
        Builder b;
        return b.Build(root);
    }
} // namespace lumora::svg
