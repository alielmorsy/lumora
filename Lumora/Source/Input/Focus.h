#pragma once

#include <lumora/Widgets/Props.h>

namespace lumora
{
    /// Backs Widget::WantsFocus: focusable unless disabled or policy None.
    [[nodiscard]] inline bool IsFocusable(FocusPolicy policy, bool disabled) noexcept
    {
        return !disabled && policy != FocusPolicy::None;
    }

    /// Backs Widget::TabPolicy: a Tab stop only with policy Tab and no negative tabIndex (negative = reachable by click/code only).
    [[nodiscard]] inline bool IsTabStop(FocusPolicy policy, int tabIndex, bool disabled) noexcept
    {
        return !disabled && policy == FocusPolicy::Tab && tabIndex >= 0;
    }
} // namespace lumora
