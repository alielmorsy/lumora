#include "Svg/AttrParser.h"

#include <lumora/FromChars.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>

namespace lumora::svg
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;

        [[nodiscard]] bool IsSpace(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == ',';
        }

        [[nodiscard]] std::string_view Trim(std::string_view s) noexcept
        {
            std::size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r' || s[a] == '\f')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r' || s[b - 1] == '\f')) --b;
            return s.substr(a, b - a);
        }

        [[nodiscard]] std::string Lower(std::string_view s)
        {
            std::string out(s);
            for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        [[nodiscard]] bool LeadingNumber(std::string_view s, float& out, std::size_t& consumed) noexcept
        {
            std::size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == ',')) ++i;
            const std::size_t start = i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            bool any = false;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; any = true; }
            if (i < s.size() && s[i] == '.')
            {
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; any = true; }
            }
            if (any && i < s.size() && (s[i] == 'e' || s[i] == 'E'))
            {
                const std::size_t save = i;
                ++i;
                if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
                bool expDigits = false;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9') { ++i; expDigits = true; }
                if (!expDigits) i = save;
            }
            if (!any)
            {
                return false;
            }
            float v = 0.f;
            if (FromChars(s.data() + start, s.data() + i, v).ec != std::errc{})
            {
                return false;
            }
            out = v;
            consumed = i;
            return true;
        }

        struct NamedColor { std::string_view name; std::uint32_t rgb; };

        // Common SVG/CSS named colours. Unknown names fall back to the caller's default.
        constexpr std::array<NamedColor, 48> kNamed{{
            {"black", 0x000000}, {"white", 0xFFFFFF}, {"red", 0xFF0000}, {"green", 0x008000},
            {"blue", 0x0000FF}, {"yellow", 0xFFFF00}, {"cyan", 0x00FFFF}, {"aqua", 0x00FFFF},
            {"magenta", 0xFF00FF}, {"fuchsia", 0xFF00FF}, {"gray", 0x808080}, {"grey", 0x808080},
            {"silver", 0xC0C0C0}, {"maroon", 0x800000}, {"olive", 0x808000}, {"lime", 0x00FF00},
            {"teal", 0x008080}, {"navy", 0x000080}, {"purple", 0x800080}, {"orange", 0xFFA500},
            {"pink", 0xFFC0CB}, {"brown", 0xA52A2A}, {"gold", 0xFFD700}, {"indigo", 0x4B0082},
            {"violet", 0xEE82EE}, {"crimson", 0xDC143C}, {"coral", 0xFF7F50}, {"salmon", 0xFA8072},
            {"khaki", 0xF0E68C}, {"orchid", 0xDA70D6}, {"plum", 0xDDA0DD}, {"tan", 0xD2B48C},
            {"beige", 0xF5F5DC}, {"ivory", 0xFFFFF0}, {"azure", 0xF0FFFF}, {"lavender", 0xE6E6FA},
            {"turquoise", 0x40E0D0}, {"darkgray", 0xA9A9A9}, {"darkgrey", 0xA9A9A9}, {"lightgray", 0xD3D3D3},
            {"lightgrey", 0xD3D3D3}, {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"gainsboro", 0xDCDCDC},
            {"steelblue", 0x4682B4}, {"skyblue", 0x87CEEB}, {"tomato", 0xFF6347}, {"chocolate", 0xD2691E},
        }};

        [[nodiscard]] std::optional<Color> NamedLookup(std::string_view lower)
        {
            for (const NamedColor& nc : kNamed)
            {
                if (nc.name == lower)
                {
                    return Color::Hex(0xFF000000u | nc.rgb);
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] int HexNibble(char c) noexcept
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        [[nodiscard]] std::optional<Color> ParseHex(std::string_view s) noexcept
        {
            std::array<int, 8> n{};
            const std::size_t len = s.size();
            if (len != 3 && len != 4 && len != 6 && len != 8)
            {
                return std::nullopt;
            }
            for (std::size_t i = 0; i < len; ++i)
            {
                n[i] = HexNibble(s[i]);
                if (n[i] < 0) return std::nullopt;
            }
            const auto dup = [](int v) { return static_cast<std::uint8_t>(v * 16 + v); };
            const auto byte = [](int hi, int lo) { return static_cast<std::uint8_t>(hi * 16 + lo); };
            if (len == 3) return Color::Rgba(dup(n[0]), dup(n[1]), dup(n[2]), 255);
            if (len == 4) return Color::Rgba(dup(n[0]), dup(n[1]), dup(n[2]), dup(n[3]));
            if (len == 6) return Color::Rgba(byte(n[0], n[1]), byte(n[2], n[3]), byte(n[4], n[5]), 255);
            return Color::Rgba(byte(n[0], n[1]), byte(n[2], n[3]), byte(n[4], n[5]), byte(n[6], n[7]));
        }

        [[nodiscard]] std::uint8_t Component(std::string_view tok) noexcept
        {
            tok = Trim(tok);
            float v = 0.f;
            std::size_t used = 0;
            if (!LeadingNumber(tok, v, used))
            {
                return 0;
            }
            if (used < tok.size() && tok[used] == '%')
            {
                v = v * 255.f / 100.f;
            }
            return static_cast<std::uint8_t>(std::clamp(v, 0.f, 255.f) + 0.5f);
        }

        [[nodiscard]] std::optional<Color> ParseRgbFunc(std::string_view s)
        {
            const std::size_t open = s.find('(');
            const std::size_t close = s.find(')');
            if (open == std::string_view::npos || close == std::string_view::npos || close < open)
            {
                return std::nullopt;
            }
            const std::string_view inner = s.substr(open + 1, close - open - 1);
            std::array<std::string_view, 4> parts{};
            int count = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i <= inner.size() && count < 4; ++i)
            {
                if (i == inner.size() || inner[i] == ',' || (IsSpace(inner[i]) && inner[i] != ','))
                {
                    if (i > start)
                    {
                        const std::string_view tok = Trim(inner.substr(start, i - start));
                        if (!tok.empty()) parts[count++] = tok;
                    }
                    start = i + 1;
                }
            }
            if (count < 3)
            {
                return std::nullopt;
            }
            const std::uint8_t r = Component(parts[0]);
            const std::uint8_t g = Component(parts[1]);
            const std::uint8_t b = Component(parts[2]);
            std::uint8_t a = 255;
            if (count >= 4)
            {
                float av = 1.f;
                std::size_t used = 0;
                if (LeadingNumber(parts[3], av, used))
                {
                    a = static_cast<std::uint8_t>(std::clamp(av, 0.f, 1.f) * 255.f + 0.5f);
                }
            }
            return Color::Rgba(r, g, b, a);
        }
    } // namespace

    bool ParseNumber(std::string_view s, float& out)
    {
        std::size_t used = 0;
        return LeadingNumber(s, out, used);
    }

    float ParseLength(std::string_view s, float reference, float fallback)
    {
        s = Trim(s);
        float v = 0.f;
        std::size_t used = 0;
        if (!LeadingNumber(s, v, used))
        {
            return fallback;
        }
        const std::string_view unit = Trim(s.substr(used));
        if (unit.empty() || unit == "px") return v;
        if (unit == "%") return v / 100.f * reference;
        if (unit == "pt") return v * (96.f / 72.f);
        if (unit == "pc") return v * 16.f;
        if (unit == "mm") return v * (96.f / 25.4f);
        if (unit == "cm") return v * (96.f / 2.54f);
        if (unit == "in") return v * 96.f;
        if (unit == "em") return v * 16.f;
        if (unit == "ex") return v * 8.f;
        return v;
    }

    std::vector<float> ParseNumberList(std::string_view s)
    {
        std::vector<float> out;
        while (!s.empty())
        {
            float v = 0.f;
            std::size_t used = 0;
            if (!LeadingNumber(s, v, used))
            {
                break;
            }
            out.push_back(v);
            s = s.substr(used);
        }
        return out;
    }

    std::optional<Color> ParseColor(std::string_view s, Color currentColor)
    {
        s = Trim(s);
        if (s.empty())
        {
            return std::nullopt;
        }
        const std::string lower = Lower(s);
        if (lower == "none") return std::nullopt;
        if (lower == "transparent") return Color::Rgba(0, 0, 0, 0);
        if (lower == "currentcolor") return currentColor;
        if (s.front() == '#') return ParseHex(s.substr(1));
        if (lower.rfind("rgb", 0) == 0) return ParseRgbFunc(s);
        return NamedLookup(lower);
    }

    PaintSpec ParsePaint(std::string_view s, Color currentColor)
    {
        s = Trim(s);
        PaintSpec spec;
        if (s.empty())
        {
            return spec;
        }
        const std::string lower = Lower(s);
        if (lower == "none")
        {
            spec.kind = PaintKind::None;
            return spec;
        }
        if (lower.rfind("url(", 0) == 0)
        {
            const std::size_t close = s.find(')');
            std::string_view inner = s.substr(4, close == std::string_view::npos ? std::string_view::npos : close - 4);
            inner = Trim(inner);
            if (!inner.empty() && inner.front() == '#') inner = inner.substr(1);
            // strip surrounding quotes if present
            if (!inner.empty() && (inner.front() == '"' || inner.front() == '\'')) inner = inner.substr(1);
            if (!inner.empty() && (inner.back() == '"' || inner.back() == '\'')) inner = inner.substr(0, inner.size() - 1);
            spec.kind = PaintKind::Gradient;
            spec.ref = std::string(inner);
            return spec;
        }
        if (const std::optional<Color> c = ParseColor(s, currentColor))
        {
            spec.kind = PaintKind::Solid;
            spec.color = *c;
        }
        return spec;
    }

    Affine ParseTransform(std::string_view s)
    {
        Affine result;
        std::size_t i = 0;
        const auto skip = [&]() { while (i < s.size() && (IsSpace(s[i]) || s[i] == ' ')) ++i; };

        while (i < s.size())
        {
            skip();
            const std::size_t nameStart = i;
            while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
            const std::string_view fn = s.substr(nameStart, i - nameStart);
            if (fn.empty())
            {
                break;
            }
            const std::size_t open = s.find('(', i);
            const std::size_t close = open == std::string_view::npos ? std::string_view::npos : s.find(')', open);
            if (open == std::string_view::npos || close == std::string_view::npos)
            {
                break;
            }
            const std::vector<float> a = ParseNumberList(s.substr(open + 1, close - open - 1));
            i = close + 1;

            Affine t;
            if (fn == "matrix" && a.size() >= 6)
            {
                t = {a[0], a[1], a[2], a[3], a[4], a[5]};
            }
            else if (fn == "translate" && !a.empty())
            {
                t = {1.f, 0.f, 0.f, 1.f, a[0], a.size() > 1 ? a[1] : 0.f};
            }
            else if (fn == "scale" && !a.empty())
            {
                const float sx = a[0];
                const float sy = a.size() > 1 ? a[1] : a[0];
                t = {sx, 0.f, 0.f, sy, 0.f, 0.f};
            }
            else if (fn == "rotate" && !a.empty())
            {
                const float rad = a[0] * kPi / 180.f;
                const float cs = std::cos(rad), sn = std::sin(rad);
                if (a.size() >= 3)
                {
                    const float cx = a[1], cy = a[2];
                    const Affine to{1, 0, 0, 1, cx, cy};
                    const Affine rot{cs, sn, -sn, cs, 0, 0};
                    const Affine from{1, 0, 0, 1, -cx, -cy};
                    t = to.Compose(rot).Compose(from);
                }
                else
                {
                    t = {cs, sn, -sn, cs, 0.f, 0.f};
                }
            }
            else if (fn == "skewX" && !a.empty())
            {
                t = {1.f, 0.f, std::tan(a[0] * kPi / 180.f), 1.f, 0.f, 0.f};
            }
            else if (fn == "skewY" && !a.empty())
            {
                t = {1.f, std::tan(a[0] * kPi / 180.f), 0.f, 1.f, 0.f, 0.f};
            }
            else
            {
                continue;
            }
            result = result.Compose(t);
        }
        return result;
    }

    bool ParseViewBox(std::string_view s, Vec2& min, Vec2& size)
    {
        const std::vector<float> v = ParseNumberList(s);
        if (v.size() < 4)
        {
            return false;
        }
        min = {v[0], v[1]};
        size = {v[2], v[3]};
        return true;
    }
} // namespace lumora::svg
