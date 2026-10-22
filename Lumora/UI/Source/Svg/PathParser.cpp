#include "Svg/PathParser.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace lumora::svg
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        [[nodiscard]] bool IsSep(char c) noexcept
        {
            return c == ' ' || c == ',' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        }

        [[nodiscard]] bool IsDigit(char c) noexcept { return c >= '0' && c <= '9'; }

        [[nodiscard]] bool IsCommand(char c) noexcept
        {
            switch (c)
            {
            case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V': case 'v':
            case 'C': case 'c': case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
            case 'A': case 'a': case 'Z': case 'z':
                return true;
            default:
                return false;
            }
        }

        class Scanner
        {
        public:
            explicit Scanner(std::string_view s) : m_s(s) {}

            void SkipSep() noexcept
            {
                while (m_i < m_s.size() && IsSep(m_s[m_i])) ++m_i;
            }

            [[nodiscard]] bool Eof() noexcept
            {
                SkipSep();
                return m_i >= m_s.size();
            }

            /// Peek the next non-separator char if it is a command letter; does not consume.
            [[nodiscard]] char PeekCommand() noexcept
            {
                SkipSep();
                if (m_i < m_s.size() && IsCommand(m_s[m_i]))
                {
                    return m_s[m_i];
                }
                return '\0';
            }

            char TakeCommand() noexcept
            {
                const char c = PeekCommand();
                if (c) ++m_i;
                return c;
            }

            [[nodiscard]] bool ReadNumber(float& out) noexcept
            {
                SkipSep();
                const std::size_t start = m_i;
                if (m_i < m_s.size() && (m_s[m_i] == '+' || m_s[m_i] == '-')) ++m_i;
                bool any = false;
                while (m_i < m_s.size() && IsDigit(m_s[m_i])) { ++m_i; any = true; }
                if (m_i < m_s.size() && m_s[m_i] == '.')
                {
                    ++m_i;
                    while (m_i < m_s.size() && IsDigit(m_s[m_i])) { ++m_i; any = true; }
                }
                if (any && m_i < m_s.size() && (m_s[m_i] == 'e' || m_s[m_i] == 'E'))
                {
                    const std::size_t save = m_i;
                    ++m_i;
                    if (m_i < m_s.size() && (m_s[m_i] == '+' || m_s[m_i] == '-')) ++m_i;
                    bool expDigits = false;
                    while (m_i < m_s.size() && IsDigit(m_s[m_i])) { ++m_i; expDigits = true; }
                    if (!expDigits) m_i = save;
                }
                if (!any)
                {
                    m_i = start;
                    return false;
                }
                float v = 0.f;
                const char* b = m_s.data() + start;
                const char* e = m_s.data() + m_i;
                if (std::from_chars(b, e, v).ec != std::errc{})
                {
                    return false;
                }
                out = v;
                return true;
            }

            /// Arc flags are a single '0' or '1', possibly with no separator before the next number.
            [[nodiscard]] bool ReadFlag(bool& out) noexcept
            {
                SkipSep();
                if (m_i < m_s.size() && (m_s[m_i] == '0' || m_s[m_i] == '1'))
                {
                    out = m_s[m_i] == '1';
                    ++m_i;
                    return true;
                }
                return false;
            }

        private:
            std::string_view m_s;
            std::size_t m_i = 0;
        };

        /// Endpoint-parameterised elliptical arc → a sequence of cubic segments appended to `sub`.
        void ArcToCubics(SubPath& sub, Vec2 cur, float rx, float ry, float phiDeg, bool largeArc,
                         bool sweep, Vec2 end)
        {
            if (cur == end)
            {
                return;
            }
            rx = std::abs(rx);
            ry = std::abs(ry);
            if (rx < 1e-6f || ry < 1e-6f)
            {
                sub.segs.push_back({PathSeg::Type::Line, {}, {}, end});
                return;
            }

            const float phi = phiDeg * kPi / 180.f;
            const float cosP = std::cos(phi);
            const float sinP = std::sin(phi);

            const float dx = (cur.x - end.x) * 0.5f;
            const float dy = (cur.y - end.y) * 0.5f;
            const float x1p = cosP * dx + sinP * dy;
            const float y1p = -sinP * dx + cosP * dy;

            // Scale radii up if they cannot span the endpoints.
            float lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
            if (lambda > 1.f)
            {
                const float s = std::sqrt(lambda);
                rx *= s;
                ry *= s;
            }

            const float rx2 = rx * rx;
            const float ry2 = ry * ry;
            const float num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
            const float den = rx2 * y1p * y1p + ry2 * x1p * x1p;
            float coef = den > 0.f ? std::sqrt(std::max(0.f, num / den)) : 0.f;
            if (largeArc == sweep)
            {
                coef = -coef;
            }
            const float cxp = coef * (rx * y1p / ry);
            const float cyp = coef * -(ry * x1p / rx);

            const float cx = cosP * cxp - sinP * cyp + (cur.x + end.x) * 0.5f;
            const float cy = sinP * cxp + cosP * cyp + (cur.y + end.y) * 0.5f;

            const auto angle = [](float ux, float uy, float vx, float vy) {
                const float dot = ux * vx + uy * vy;
                const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
                float a = len > 0.f ? std::acos(std::clamp(dot / len, -1.f, 1.f)) : 0.f;
                if (ux * vy - uy * vx < 0.f) a = -a;
                return a;
            };

            const float ux = (x1p - cxp) / rx;
            const float uy = (y1p - cyp) / ry;
            const float vx = (-x1p - cxp) / rx;
            const float vy = (-y1p - cyp) / ry;

            float theta1 = angle(1.f, 0.f, ux, uy);
            float deltaTheta = angle(ux, uy, vx, vy);
            if (!sweep && deltaTheta > 0.f) deltaTheta -= 2.f * kPi;
            else if (sweep && deltaTheta < 0.f) deltaTheta += 2.f * kPi;

            const int segments = std::max(1, static_cast<int>(std::ceil(std::abs(deltaTheta) / (kPi * 0.5f))));
            const float delta = deltaTheta / static_cast<float>(segments);
            const float t = (4.f / 3.f) * std::tan(delta * 0.25f);

            float angleStart = theta1;
            for (int i = 0; i < segments; ++i)
            {
                const float a1 = angleStart;
                const float a2 = angleStart + delta;
                const float cosA1 = std::cos(a1), sinA1 = std::sin(a1);
                const float cosA2 = std::cos(a2), sinA2 = std::sin(a2);

                const auto onEllipse = [&](float ca, float sa) -> Vec2 {
                    const float ex = rx * ca;
                    const float ey = ry * sa;
                    return {cx + cosP * ex - sinP * ey, cy + sinP * ex + cosP * ey};
                };
                const auto deriv = [&](float ca, float sa) -> Vec2 {
                    const float ex = -rx * sa;
                    const float ey = ry * ca;
                    return {cosP * ex - sinP * ey, sinP * ex + cosP * ey};
                };

                const Vec2 p1 = onEllipse(cosA1, sinA1);
                const Vec2 p2 = onEllipse(cosA2, sinA2);
                const Vec2 d1 = deriv(cosA1, sinA1);
                const Vec2 d2 = deriv(cosA2, sinA2);
                const Vec2 c1{p1.x + t * d1.x, p1.y + t * d1.y};
                const Vec2 c2{p2.x - t * d2.x, p2.y - t * d2.y};
                sub.segs.push_back({PathSeg::Type::Cubic, c1, c2, p2});
                angleStart = a2;
            }
        }
    } // namespace

    RawPath ParsePathData(std::string_view d)
    {
        RawPath path;
        Scanner sc(d);

        Vec2 cur{0.f, 0.f};
        Vec2 start{0.f, 0.f};
        Vec2 lastCubicCtrl{0.f, 0.f};
        Vec2 lastQuadCtrl{0.f, 0.f};
        char lastCmd = '\0';
        bool haveSub = false;

        const auto sub = [&]() -> SubPath& { return path.subs.back(); };
        const auto ensureSub = [&]() {
            if (!haveSub)
            {
                path.subs.push_back(SubPath{cur, {}, false});
                haveSub = true;
            }
        };

        char cmd = sc.TakeCommand();
        while (cmd != '\0')
        {
            const bool rel = (cmd >= 'a' && cmd <= 'z');
            const auto abs2 = [&](float x, float y) -> Vec2 { return rel ? Vec2{cur.x + x, cur.y + y} : Vec2{x, y}; };

            switch (cmd)
            {
            case 'M': case 'm':
            {
                float x, y;
                if (!sc.ReadNumber(x) || !sc.ReadNumber(y)) { cmd = '\0'; break; }
                cur = abs2(x, y);
                start = cur;
                path.subs.push_back(SubPath{cur, {}, false});
                haveSub = true;
                lastCubicCtrl = lastQuadCtrl = cur;
                // Subsequent coordinate pairs are implicit L/l.
                for (;;)
                {
                    if (sc.PeekCommand() != '\0' || !sc.ReadNumber(x)) break;
                    if (!sc.ReadNumber(y)) break;
                    const Vec2 p = abs2(x, y);
                    sub().segs.push_back({PathSeg::Type::Line, {}, {}, p});
                    cur = p;
                    lastCubicCtrl = lastQuadCtrl = cur;
                }
                break;
            }
            case 'L': case 'l':
            {
                ensureSub();
                float x, y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const Vec2 p = abs2(x, y);
                    sub().segs.push_back({PathSeg::Type::Line, {}, {}, p});
                    cur = p;
                    lastCubicCtrl = lastQuadCtrl = cur;
                }
                break;
            }
            case 'H': case 'h':
            {
                ensureSub();
                float x;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x))
                {
                    const Vec2 p = rel ? Vec2{cur.x + x, cur.y} : Vec2{x, cur.y};
                    sub().segs.push_back({PathSeg::Type::Line, {}, {}, p});
                    cur = p;
                    lastCubicCtrl = lastQuadCtrl = cur;
                }
                break;
            }
            case 'V': case 'v':
            {
                ensureSub();
                float y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(y))
                {
                    const Vec2 p = rel ? Vec2{cur.x, cur.y + y} : Vec2{cur.x, y};
                    sub().segs.push_back({PathSeg::Type::Line, {}, {}, p});
                    cur = p;
                    lastCubicCtrl = lastQuadCtrl = cur;
                }
                break;
            }
            case 'C': case 'c':
            {
                ensureSub();
                float x1, y1, x2, y2, x, y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x1) && sc.ReadNumber(y1)
                       && sc.ReadNumber(x2) && sc.ReadNumber(y2) && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const Vec2 c1 = abs2(x1, y1);
                    const Vec2 c2 = abs2(x2, y2);
                    const Vec2 p = abs2(x, y);
                    sub().segs.push_back({PathSeg::Type::Cubic, c1, c2, p});
                    lastCubicCtrl = c2;
                    lastQuadCtrl = p;
                    cur = p;
                }
                break;
            }
            case 'S': case 's':
            {
                ensureSub();
                float x2, y2, x, y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x2) && sc.ReadNumber(y2)
                       && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const bool smooth = (lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' || lastCmd == 's');
                    const Vec2 c1 = smooth ? Vec2{2.f * cur.x - lastCubicCtrl.x, 2.f * cur.y - lastCubicCtrl.y} : cur;
                    const Vec2 c2 = abs2(x2, y2);
                    const Vec2 p = abs2(x, y);
                    sub().segs.push_back({PathSeg::Type::Cubic, c1, c2, p});
                    lastCubicCtrl = c2;
                    lastQuadCtrl = p;
                    cur = p;
                    lastCmd = cmd;
                }
                break;
            }
            case 'Q': case 'q':
            {
                ensureSub();
                float x1, y1, x, y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x1) && sc.ReadNumber(y1)
                       && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const Vec2 qc = abs2(x1, y1);
                    const Vec2 p = abs2(x, y);
                    const Vec2 c1{cur.x + 2.f / 3.f * (qc.x - cur.x), cur.y + 2.f / 3.f * (qc.y - cur.y)};
                    const Vec2 c2{p.x + 2.f / 3.f * (qc.x - p.x), p.y + 2.f / 3.f * (qc.y - p.y)};
                    sub().segs.push_back({PathSeg::Type::Cubic, c1, c2, p});
                    lastQuadCtrl = qc;
                    lastCubicCtrl = p;
                    cur = p;
                }
                break;
            }
            case 'T': case 't':
            {
                ensureSub();
                float x, y;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const bool smooth = (lastCmd == 'Q' || lastCmd == 'q' || lastCmd == 'T' || lastCmd == 't');
                    const Vec2 qc = smooth ? Vec2{2.f * cur.x - lastQuadCtrl.x, 2.f * cur.y - lastQuadCtrl.y} : cur;
                    const Vec2 p = abs2(x, y);
                    const Vec2 c1{cur.x + 2.f / 3.f * (qc.x - cur.x), cur.y + 2.f / 3.f * (qc.y - cur.y)};
                    const Vec2 c2{p.x + 2.f / 3.f * (qc.x - p.x), p.y + 2.f / 3.f * (qc.y - p.y)};
                    sub().segs.push_back({PathSeg::Type::Cubic, c1, c2, p});
                    lastQuadCtrl = qc;
                    lastCubicCtrl = p;
                    cur = p;
                    lastCmd = cmd;
                }
                break;
            }
            case 'A': case 'a':
            {
                ensureSub();
                float rx, ry, rot, x, y;
                bool largeArc, sweep;
                while (sc.PeekCommand() == '\0' && sc.ReadNumber(rx) && sc.ReadNumber(ry) && sc.ReadNumber(rot)
                       && sc.ReadFlag(largeArc) && sc.ReadFlag(sweep) && sc.ReadNumber(x) && sc.ReadNumber(y))
                {
                    const Vec2 p = abs2(x, y);
                    ArcToCubics(sub(), cur, rx, ry, rot, largeArc, sweep, p);
                    cur = p;
                    lastCubicCtrl = lastQuadCtrl = cur;
                }
                break;
            }
            case 'Z': case 'z':
            {
                if (haveSub)
                {
                    sub().closed = true;
                }
                cur = start;
                lastCubicCtrl = lastQuadCtrl = cur;
                haveSub = false; // a following command without M reopens a subpath at `start`
                break;
            }
            default:
                break;
            }

            lastCmd = cmd;
            cmd = sc.TakeCommand();
        }

        // Drop subpaths that never got a segment (a lone trailing M).
        std::erase_if(path.subs, [](const SubPath& s) { return s.segs.empty(); });
        return path;
    }
} // namespace lumora::svg
