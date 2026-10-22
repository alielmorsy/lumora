#pragma once

#include <lumora/Api.h>
#include <lumora/Style/CssValue.h>
#include <lumora/Style/Stylesheet.h>

#include <string_view>

namespace lumora::style
{
    /// Parse CSS into a Stylesheet. Never throws: malformed rules are skipped (LOG_WARN) and parsing recovers at the next
    /// `}` / `;`. Nesting is desugared; `@media`/`@keyframes` captured; `:root` custom properties collected.
    [[nodiscard]] LUMORA_API Stylesheet ParseStylesheet(std::string_view source);

    /// Append `addend` onto `into`, continuing the source-order counter so later sheets win cascade ties.
    LUMORA_API void MergeStylesheet(Stylesheet& into, Stylesheet&& addend);
}
