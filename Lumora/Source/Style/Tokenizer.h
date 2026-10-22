#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lumora::style
{
    enum class TokType : uint8_t
    {
        Ident,      ///< text = identifier (includes `--custom`, `-webkit-…`)
        Function,   ///< text = name; the opening `(` has been consumed
        AtKeyword,  ///< text = keyword without the leading `@`
        Hash,       ///< text = characters after `#` (a hex colour or fragment)
        String,     ///< text = the unquoted contents
        Number,     ///< num
        Dimension,  ///< num + text (the unit)
        Percentage, ///< num (already the percent magnitude)
        Colon,
        Semicolon,
        Comma,
        LBrace,
        RBrace,
        LParen,
        RParen,
        Delim, ///< text = the single delimiter character (`.`, `>`, `*`, `&`, `!`, `/`, …)
        Eof,
    };

    struct Token
    {
        TokType type = TokType::Eof;
        std::string text;
        float num = 0.f;
        bool wsBefore = false; ///< whitespace/comment immediately preceded this token
    };

    /// Tokenise CSS into a flat stream ending in one Eof token. Comments/whitespace fold into the next token's `wsBefore`
    /// (descendant combinators and value separation depend on it).
    [[nodiscard]] std::vector<Token> Tokenize(std::string_view source);
} // namespace lumora::style
