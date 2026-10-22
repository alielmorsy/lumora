#pragma once

#include "Style/Tokenizer.h"

#include <lumora/Style/CssValue.h>

#include <span>

namespace lumora::style
{
    /// Parse a declaration value's tokens into a CssValue: comma parts become `List{comma=true}` of space-separated groups
    /// (multi-part groups are `List{comma=false}`); colours resolve here, `var()` is a VarRef, other `name(...)` a Function.
    [[nodiscard]] CssValue ParseValue(std::span<const Token> tokens);
} // namespace lumora::style
