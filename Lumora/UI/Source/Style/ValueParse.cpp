#include "Style/ValueParse.h"

#include "Svg/AttrParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace lumora::style
{
    namespace
    {
        [[nodiscard]] bool IEquals(std::string_view a, std::string_view b) noexcept
        {
            if (a.size() != b.size()) return false;
            for (size_t k = 0; k < a.size(); ++k)
                if (std::tolower(static_cast<unsigned char>(a[k])) != std::tolower(static_cast<unsigned char>(b[k])))
                    return false;
            return true;
        }

        /// Depth-aware end of the current function call: given `begin` pointing just after a Function
        /// token (i.e. at its first argument), return the index of the matching RParen.
        [[nodiscard]] size_t MatchParen(std::span<const Token> t, size_t begin)
        {
            int depth = 1;
            size_t k = begin;
            for (; k < t.size(); ++k)
            {
                if (t[k].type == TokType::Function || t[k].type == TokType::LParen) ++depth;
                else if (t[k].type == TokType::RParen)
                {
                    if (--depth == 0) return k;
                }
            }
            return k; // unterminated - treat rest as the argument list
        }

        [[nodiscard]] Color HslToColor(float h, float s, float l, float a)
        {
            h = std::fmod(std::fmod(h, 360.f) + 360.f, 360.f) / 360.f;
            s = std::clamp(s, 0.f, 1.f);
            l = std::clamp(l, 0.f, 1.f);
            const auto hue = [](float p, float q, float t) {
                if (t < 0.f) t += 1.f;
                if (t > 1.f) t -= 1.f;
                if (t < 1.f / 6.f) return p + (q - p) * 6.f * t;
                if (t < 1.f / 2.f) return q;
                if (t < 2.f / 3.f) return p + (q - p) * (2.f / 3.f - t) * 6.f;
                return p;
            };
            float r, g, b;
            if (s == 0.f)
            {
                r = g = b = l;
            }
            else
            {
                const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
                const float p = 2.f * l - q;
                r = hue(p, q, h + 1.f / 3.f);
                g = hue(p, q, h);
                b = hue(p, q, h - 1.f / 3.f);
            }
            const auto to8 = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
            return {to8(r), to8(g), to8(b), to8(std::clamp(a, 0.f, 1.f))};
        }

        CssValue ParseComponent(std::span<const Token> t, size_t& i);

        CssValue ParseGroup(std::span<const Token> t, size_t begin, size_t end)
        {
            std::vector<CssValue> parts;
            size_t i = begin;
            while (i < end)
            {
                const size_t before = i;
                CssValue c = ParseComponent(t.subspan(0, end), i);
                if (i == before) { ++i; continue; } // guard against non-advancing garbage
                if (c.Valid()) parts.push_back(std::move(c));
            }
            if (parts.empty()) return {};
            if (parts.size() == 1) return std::move(parts.front());
            CssValue list;
            list.kind = ValueKind::List;
            list.comma = false;
            list.list = std::move(parts);
            return list;
        }

        CssValue ParseFunction(std::span<const Token> t, const std::string& name, size_t argBegin, size_t argEnd)
        {
            if (IEquals(name, "var"))
            {
                CssValue v;
                v.kind = ValueKind::VarRef;
                size_t k = argBegin;
                if (k < argEnd && t[k].type == TokType::Ident)
                {
                    v.keyword = StringId(t[k].text);
                    v.text = t[k].text;
                    ++k;
                }
                while (k < argEnd && (t[k].type == TokType::Comma)) ++k;
                if (k < argEnd)
                {
                    CssValue fallback = ParseGroup(t, k, argEnd);
                    if (fallback.Valid()) v.list.push_back(std::move(fallback));
                }
                return v;
            }

            if (IEquals(name, "rgb") || IEquals(name, "rgba"))
            {
                std::vector<const Token*> chan;
                for (size_t k = argBegin; k < argEnd; ++k)
                    if (t[k].type == TokType::Number || t[k].type == TokType::Percentage)
                        chan.push_back(&t[k]);
                Color c{};
                if (chan.size() >= 3)
                {
                    const auto rgb = [](const Token& tk) {
                        const float v = tk.type == TokType::Percentage ? tk.num / 100.f * 255.f : tk.num;
                        return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f) + 0.5f);
                    };
                    uint8_t a = 255;
                    if (chan.size() >= 4)
                    {
                        const Token& at = *chan[3];
                        const float av = at.type == TokType::Percentage ? at.num / 100.f : at.num;
                        a = static_cast<uint8_t>(std::clamp(av, 0.f, 1.f) * 255.f + 0.5f);
                    }
                    c = Color(rgb(*chan[0]), rgb(*chan[1]), rgb(*chan[2]), a);
                }
                return CssValue::Rgba(c);
            }

            if (IEquals(name, "hsl") || IEquals(name, "hsla"))
            {
                std::vector<float> ch;
                for (size_t k = argBegin; k < argEnd; ++k)
                {
                    if (t[k].type == TokType::Number) ch.push_back(t[k].num);
                    else if (t[k].type == TokType::Percentage) ch.push_back(t[k].num / 100.f);
                    else if (t[k].type == TokType::Dimension) ch.push_back(t[k].num); // deg
                }
                if (ch.size() >= 3)
                    return CssValue::Rgba(HslToColor(ch[0], ch[1], ch[2], ch.size() >= 4 ? ch[3] : 1.f));
                return {};
            }

            // Generic function (cubic-bezier, steps, translate*, scale, rotate, calc, gradients, …).
            CssValue fn;
            fn.kind = ValueKind::Function;
            fn.keyword = StringId(name);
            fn.text = name;
            size_t start = argBegin;
            int depth = 0;
            for (size_t k = argBegin; k <= argEnd; ++k)
            {
                const bool atEnd = (k == argEnd);
                if (!atEnd && (t[k].type == TokType::Function || t[k].type == TokType::LParen)) ++depth;
                else if (!atEnd && t[k].type == TokType::RParen) --depth;
                if (atEnd || (depth == 0 && t[k].type == TokType::Comma))
                {
                    if (k > start)
                    {
                        CssValue arg = ParseGroup(t, start, k);
                        if (arg.Valid()) fn.list.push_back(std::move(arg));
                    }
                    start = k + 1;
                }
            }
            return fn;
        }

        CssValue ParseComponent(std::span<const Token> t, size_t& i)
        {
            const Token& tok = t[i];
            switch (tok.type)
            {
            case TokType::Number:
                ++i;
                return CssValue::Number(tok.num);
            case TokType::Percentage:
                ++i;
                return CssValue::Percent(tok.num);
            case TokType::Dimension:
            {
                ++i;
                const std::string& u = tok.text;
                if (IEquals(u, "px")) return CssValue::Px(tok.num);
                if (IEquals(u, "em")) return CssValue::Length(tok.num, Unit::Em);
                if (IEquals(u, "rem")) return CssValue::Length(tok.num, Unit::Rem);
                if (IEquals(u, "vw")) return CssValue::Length(tok.num, Unit::Vw);
                if (IEquals(u, "vh")) return CssValue::Length(tok.num, Unit::Vh);
                if (IEquals(u, "fr")) return CssValue::Length(tok.num, Unit::Fr);
                if (IEquals(u, "deg")) return CssValue::Angle(tok.num, Unit::Deg);
                if (IEquals(u, "rad")) return CssValue::Angle(tok.num, Unit::Rad);
                if (IEquals(u, "turn")) return CssValue::Angle(tok.num, Unit::Turn);
                if (IEquals(u, "s")) return CssValue::Time(tok.num, Unit::S);
                if (IEquals(u, "ms")) return CssValue::Time(tok.num, Unit::Ms);
                // Absolute units → px (context-free conversions).
                if (IEquals(u, "pt")) return CssValue::Px(tok.num * 96.f / 72.f);
                if (IEquals(u, "pc")) return CssValue::Px(tok.num * 16.f);
                if (IEquals(u, "in")) return CssValue::Px(tok.num * 96.f);
                if (IEquals(u, "cm")) return CssValue::Px(tok.num * 96.f / 2.54f);
                if (IEquals(u, "mm")) return CssValue::Px(tok.num * 96.f / 25.4f);
                return CssValue::Number(tok.num);
            }
            case TokType::Hash:
            {
                ++i;
                if (auto c = svg::ParseColor("#" + tok.text, Color::Black)) return CssValue::Rgba(*c);
                return {};
            }
            case TokType::Ident:
            {
                ++i;
                // CSS keywords are case-insensitive; custom-property names (var(--X)) are not.
                if (tok.text.rfind("--", 0) == 0) return CssValue::Word(StringId(tok.text), tok.text);
                std::string lower(tok.text);
                for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                // Resolve named colours here: StringId is lossy in release, so the keyword text is unrecoverable at apply time.
                // `currentColor` is contextual and stays a keyword.
                if (lower != "currentcolor")
                    if (auto c = svg::ParseColor(lower, Color::Black)) return CssValue::Rgba(*c);
                return CssValue::Word(StringId(lower), lower);
            }
            case TokType::String:
                ++i;
                return CssValue::Word(StringId(tok.text), tok.text);
            case TokType::Function:
            {
                std::string name = tok.text; // function names are case-insensitive
                for (char& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                const size_t argBegin = i + 1;
                const size_t argEnd = MatchParen(t, argBegin);
                i = (argEnd < t.size()) ? argEnd + 1 : argEnd;
                return ParseFunction(t, name, argBegin, argEnd);
            }
            case TokType::Delim:
                // Stray delimiters inside a value (e.g. `/` in font shorthand) - keep as a keyword.
                ++i;
                return CssValue::Word(StringId(tok.text));
            default:
                ++i;
                return {};
            }
        }
    } // namespace

    CssValue ParseValue(std::span<const Token> tokens)
    {
        // Trim leading/trailing Eof or empties defensively.
        size_t end = tokens.size();
        if (end == 0) return {};

        std::vector<CssValue> groups;
        size_t start = 0;
        int depth = 0;
        for (size_t k = 0; k <= end; ++k)
        {
            const bool atEnd = (k == end);
            if (!atEnd && (tokens[k].type == TokType::Function || tokens[k].type == TokType::LParen)) ++depth;
            else if (!atEnd && tokens[k].type == TokType::RParen) --depth;
            if (atEnd || (depth == 0 && tokens[k].type == TokType::Comma))
            {
                if (k > start)
                {
                    CssValue g = ParseGroup(tokens, start, k);
                    if (g.Valid()) groups.push_back(std::move(g));
                }
                start = k + 1;
            }
        }

        if (groups.empty()) return {};
        if (groups.size() == 1) return std::move(groups.front());
        CssValue list;
        list.kind = ValueKind::List;
        list.comma = true;
        list.list = std::move(groups);
        return list;
    }
} // namespace lumora::style
