#include "Style/Tokenizer.h"

#include <cctype>

namespace lumora::style
{
    namespace
    {
        [[nodiscard]] bool IsIdentStart(char c) noexcept
        {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '-' || (static_cast<unsigned char>(c) >= 0x80);
        }

        [[nodiscard]] bool IsIdentChar(char c) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || (static_cast<unsigned char>(c) >= 0x80);
        }

        [[nodiscard]] bool IsNameChar(char c) noexcept
        {
            // Characters allowed after `#` (hex colours, but be lenient).
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
        }

        struct Lexer
        {
            std::string_view s;
            size_t i = 0;

            [[nodiscard]] bool Eof() const noexcept { return i >= s.size(); }
            [[nodiscard]] char Peek(size_t o = 0) const noexcept { return i + o < s.size() ? s[i + o] : '\0'; }

            [[nodiscard]] bool StartsNumber() const noexcept
            {
                const char c = Peek();
                if (std::isdigit(static_cast<unsigned char>(c))) return true;
                if (c == '.' && std::isdigit(static_cast<unsigned char>(Peek(1)))) return true;
                if ((c == '+' || c == '-'))
                {
                    const char n = Peek(1);
                    if (std::isdigit(static_cast<unsigned char>(n))) return true;
                    if (n == '.' && std::isdigit(static_cast<unsigned char>(Peek(2)))) return true;
                }
                return false;
            }

            std::string ReadIdent()
            {
                const size_t start = i;
                while (!Eof() && IsIdentChar(Peek())) ++i;
                return std::string(s.substr(start, i - start));
            }

            float ReadNumber()
            {
                const size_t start = i;
                if (Peek() == '+' || Peek() == '-') ++i;
                while (std::isdigit(static_cast<unsigned char>(Peek()))) ++i;
                if (Peek() == '.')
                {
                    ++i;
                    while (std::isdigit(static_cast<unsigned char>(Peek()))) ++i;
                }
                if (Peek() == 'e' || Peek() == 'E')
                {
                    const size_t save = i;
                    ++i;
                    if (Peek() == '+' || Peek() == '-') ++i;
                    if (std::isdigit(static_cast<unsigned char>(Peek())))
                        while (std::isdigit(static_cast<unsigned char>(Peek()))) ++i;
                    else
                        i = save; // not an exponent after all
                }
                float out = 0.f;
                const std::string num(s.substr(start, i - start));
                try { out = std::stof(num); }
                catch (...) { out = 0.f; }
                return out;
            }

            // Skip whitespace and /* */ comments. Returns true if anything was skipped.
            bool SkipTrivia()
            {
                bool skipped = false;
                for (;;)
                {
                    if (Eof()) break;
                    const char c = Peek();
                    if (std::isspace(static_cast<unsigned char>(c)))
                    {
                        ++i;
                        skipped = true;
                        continue;
                    }
                    if (c == '/' && Peek(1) == '*')
                    {
                        i += 2;
                        while (!Eof() && !(Peek() == '*' && Peek(1) == '/')) ++i;
                        if (!Eof()) i += 2;
                        skipped = true;
                        continue;
                    }
                    break;
                }
                return skipped;
            }
        };
    } // namespace

    std::vector<Token> Tokenize(std::string_view source)
    {
        std::vector<Token> out;
        Lexer lex{source};

        for (;;)
        {
            const bool ws = lex.SkipTrivia();
            if (lex.Eof())
            {
                out.push_back({TokType::Eof, {}, 0.f, ws});
                break;
            }

            Token tok;
            tok.wsBefore = ws;
            const char c = lex.Peek();

            if (lex.StartsNumber())
            {
                tok.num = lex.ReadNumber();
                if (lex.Peek() == '%')
                {
                    ++lex.i;
                    tok.type = TokType::Percentage;
                }
                else if (IsIdentStart(lex.Peek()))
                {
                    tok.type = TokType::Dimension;
                    tok.text = lex.ReadIdent();
                }
                else
                {
                    tok.type = TokType::Number;
                }
            }
            else if (c == '@')
            {
                ++lex.i;
                tok.type = TokType::AtKeyword;
                tok.text = lex.ReadIdent();
            }
            else if (c == '#')
            {
                ++lex.i;
                const size_t start = lex.i;
                while (!lex.Eof() && IsNameChar(lex.Peek())) ++lex.i;
                tok.type = TokType::Hash;
                tok.text = std::string(source.substr(start, lex.i - start));
            }
            else if (c == '"' || c == '\'')
            {
                const char quote = c;
                ++lex.i;
                const size_t start = lex.i;
                while (!lex.Eof() && lex.Peek() != quote)
                {
                    if (lex.Peek() == '\\' && lex.i + 1 < source.size()) ++lex.i;
                    ++lex.i;
                }
                tok.type = TokType::String;
                tok.text = std::string(source.substr(start, lex.i - start));
                if (!lex.Eof()) ++lex.i;
            }
            else if (IsIdentStart(c))
            {
                const std::string ident = lex.ReadIdent();
                if (lex.Peek() == '(')
                {
                    ++lex.i;
                    tok.type = TokType::Function;
                }
                else
                {
                    tok.type = TokType::Ident;
                }
                tok.text = ident;
            }
            else
            {
                ++lex.i;
                switch (c)
                {
                case ':': tok.type = TokType::Colon; break;
                case ';': tok.type = TokType::Semicolon; break;
                case ',': tok.type = TokType::Comma; break;
                case '{': tok.type = TokType::LBrace; break;
                case '}': tok.type = TokType::RBrace; break;
                case '(': tok.type = TokType::LParen; break;
                case ')': tok.type = TokType::RParen; break;
                default:
                    tok.type = TokType::Delim;
                    tok.text = std::string(1, c);
                    break;
                }
            }

            out.push_back(std::move(tok));
        }

        return out;
    }
} // namespace lumora::style
