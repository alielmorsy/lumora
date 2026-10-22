#include "Svg/XmlReader.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace lumora::svg
{
    const std::string* XmlNode::Attr(std::string_view name) const noexcept
    {
        for (const XmlAttr& a : attrs)
        {
            if (a.name == name)
            {
                return &a.value;
            }
        }
        return nullptr;
    }

    namespace
    {
        [[nodiscard]] bool IsSpace(char c) noexcept
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        }

        [[nodiscard]] bool IsNameChar(char c) noexcept
        {
            return !IsSpace(c) && c != '>' && c != '/' && c != '=' && c != '<';
        }

        void AppendCodepoint(std::string& out, std::uint32_t cp)
        {
            if (cp <= 0x7F)
            {
                out.push_back(static_cast<char>(cp));
            }
            else if (cp <= 0x7FF)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp <= 0xFFFF)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        [[nodiscard]] std::string DecodeEntities(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size();)
            {
                if (s[i] != '&')
                {
                    out.push_back(s[i++]);
                    continue;
                }
                const std::size_t semi = s.find(';', i);
                if (semi == std::string_view::npos)
                {
                    out.push_back(s[i++]);
                    continue;
                }
                const std::string_view ent = s.substr(i + 1, semi - i - 1);
                if (ent == "amp") out.push_back('&');
                else if (ent == "lt") out.push_back('<');
                else if (ent == "gt") out.push_back('>');
                else if (ent == "quot") out.push_back('"');
                else if (ent == "apos") out.push_back('\'');
                else if (!ent.empty() && ent[0] == '#')
                {
                    std::uint32_t cp = 0;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                    {
                        for (std::size_t k = 2; k < ent.size(); ++k)
                        {
                            const char c = ent[k];
                            cp *= 16;
                            if (c >= '0' && c <= '9') cp += static_cast<std::uint32_t>(c - '0');
                            else if (c >= 'a' && c <= 'f') cp += static_cast<std::uint32_t>(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') cp += static_cast<std::uint32_t>(c - 'A' + 10);
                        }
                    }
                    else
                    {
                        for (std::size_t k = 1; k < ent.size(); ++k)
                        {
                            if (ent[k] >= '0' && ent[k] <= '9') cp = cp * 10 + static_cast<std::uint32_t>(ent[k] - '0');
                        }
                    }
                    AppendCodepoint(out, cp);
                }
                else
                {
                    out.append(s.substr(i, semi - i + 1)); // unknown entity: keep verbatim
                }
                i = semi + 1;
            }
            return out;
        }

        /// Strip a namespace prefix from a tag, but keep `xlink:` on attribute names so the builder can
        /// find xlink:href. Tags are always stripped.
        [[nodiscard]] std::string_view StripTagPrefix(std::string_view name) noexcept
        {
            const std::size_t colon = name.rfind(':');
            return colon == std::string_view::npos ? name : name.substr(colon + 1);
        }

        class Cursor
        {
        public:
            explicit Cursor(std::string_view s) : m_s(s) {}

            [[nodiscard]] bool Eof() const noexcept { return m_i >= m_s.size(); }
            [[nodiscard]] char Peek() const noexcept { return m_i < m_s.size() ? m_s[m_i] : '\0'; }
            void Advance(std::size_t n = 1) noexcept { m_i += n; }

            void SkipSpace() noexcept
            {
                while (!Eof() && IsSpace(m_s[m_i])) ++m_i;
            }

            [[nodiscard]] bool StartsWith(std::string_view lit) const noexcept
            {
                return m_s.compare(m_i, lit.size(), lit) == 0;
            }

            void SkipUntil(std::string_view lit) noexcept
            {
                const std::size_t at = m_s.find(lit, m_i);
                m_i = at == std::string_view::npos ? m_s.size() : at + lit.size();
            }

            [[nodiscard]] std::string_view Take(std::size_t n) noexcept
            {
                n = std::min(n, m_s.size() - m_i);
                const std::string_view r = m_s.substr(m_i, n);
                m_i += n;
                return r;
            }

            [[nodiscard]] std::string_view TakeWhile(bool (*pred)(char)) noexcept
            {
                const std::size_t start = m_i;
                while (!Eof() && pred(m_s[m_i])) ++m_i;
                return m_s.substr(start, m_i - start);
            }

            [[nodiscard]] std::size_t Pos() const noexcept { return m_i; }
            [[nodiscard]] std::string_view Rest() const noexcept { return m_s.substr(m_i); }

        private:
            std::string_view m_s;
            std::size_t m_i = 0;
        };

        /// Skip prolog / comments / DOCTYPE / PI at the current position. Returns true if it consumed one.
        bool SkipMisc(Cursor& c)
        {
            c.SkipSpace();
            if (c.StartsWith("<!--")) { c.Advance(4); c.SkipUntil("-->"); return true; }
            if (c.StartsWith("<?"))   { c.Advance(2); c.SkipUntil("?>"); return true; }
            if (c.StartsWith("<![CDATA[")) { c.Advance(9); c.SkipUntil("]]>"); return true; }
            if (c.StartsWith("<!"))   { c.Advance(2); c.SkipUntil(">"); return true; } // DOCTYPE etc.
            return false;
        }

        void ParseAttrs(Cursor& c, XmlNode& node)
        {
            for (;;)
            {
                c.SkipSpace();
                const char p = c.Peek();
                if (p == '>' || p == '/' || p == '\0')
                {
                    return;
                }
                const std::string_view rawName = c.TakeWhile(IsNameChar);
                if (rawName.empty())
                {
                    c.Advance(); // defensive: don't spin on an unexpected char
                    continue;
                }
                c.SkipSpace();
                std::string value;
                if (c.Peek() == '=')
                {
                    c.Advance();
                    c.SkipSpace();
                    const char q = c.Peek();
                    if (q == '"' || q == '\'')
                    {
                        c.Advance();
                        const std::size_t start = c.Pos();
                        std::string_view rest = c.Rest();
                        const std::size_t end = rest.find(q);
                        const std::string_view raw = end == std::string_view::npos ? rest : rest.substr(0, end);
                        value = DecodeEntities(raw);
                        c.Advance(raw.size());
                        if (c.Peek() == q) c.Advance();
                        (void)start;
                    }
                    else
                    {
                        value = std::string(c.TakeWhile(IsNameChar));
                    }
                }
                std::string name(rawName);
                // Keep xlink:href as-is; strip other prefixes so e.g. svg:fill -> fill.
                if (name != "xlink:href")
                {
                    const std::size_t colon = name.rfind(':');
                    if (colon != std::string::npos)
                    {
                        name = name.substr(colon + 1);
                    }
                }
                node.attrs.push_back({std::move(name), std::move(value)});
            }
        }

        /// Parse one element at `<`. Returns false on malformed input.
        bool ParseElement(Cursor& c, XmlNode& node)
        {
            if (c.Peek() != '<')
            {
                return false;
            }
            c.Advance();
            const std::string_view rawTag = c.TakeWhile(IsNameChar);
            if (rawTag.empty())
            {
                return false;
            }
            node.tag = std::string(StripTagPrefix(rawTag));
            ParseAttrs(c, node);

            c.SkipSpace();
            if (c.Peek() == '/')
            {
                c.Advance();
                if (c.Peek() == '>') c.Advance();
                return true; // self-closing
            }
            if (c.Peek() == '>')
            {
                c.Advance();
            }
            else
            {
                return true; // truncated open tag
            }

            for (;;)
            {
                if (c.Eof())
                {
                    return true;
                }
                if (c.StartsWith("</"))
                {
                    c.Advance(2);
                    c.TakeWhile(IsNameChar);
                    c.SkipUntil(">");
                    return true;
                }
                if (SkipMisc(c))
                {
                    continue;
                }
                if (c.Peek() == '<')
                {
                    XmlNode child;
                    if (ParseElement(c, child))
                    {
                        node.children.push_back(std::move(child));
                    }
                    else
                    {
                        return true;
                    }
                }
                else
                {
                    c.Advance(); // text content - skipped
                }
            }
        }
    } // namespace

    bool ParseXml(std::string_view text, XmlNode& out)
    {
        Cursor c(text);
        for (;;)
        {
            c.SkipSpace();
            if (c.Eof())
            {
                return false;
            }
            if (SkipMisc(c))
            {
                continue;
            }
            if (c.Peek() == '<')
            {
                return ParseElement(c, out);
            }
            c.Advance();
        }
    }
} // namespace lumora::svg
