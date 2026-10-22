#pragma once

#include "Style/ComputedStyle.h"

#include <lumora/Foundation/Vec2.h>

namespace lumora::style
{
    struct ComputeContext
    {
        float fontSize = 16.f;     ///< this node's resolved font-size (px), for `em`
        float rootFontSize = 16.f; ///< the app root font-size (px), for `rem`
        Vec2 viewport{};           ///< surface size (px), for `vw`/`vh`
    };

    /// Substitute every `var(--x[, fallback])` in the declaration values against `vars`
    /// (custom-property lookups, recursive, cycle-guarded). Unresolved vars become invalid values.
    void ResolveVars(ComputedStyle& cs, const VarMap& vars);

    /// Resolve this node's own font-size to px in place (declared em/rem/percent against the parent /
    /// root font-size, else inherited), returning it so `em` in other properties resolves against it.
    [[nodiscard]] float ResolveFontSize(ComputedStyle& cs, const InheritedStyle* parent, float rootFontSize);

    /// Rewrite `em`/`rem`/`vw`/`vh` length magnitudes to px in place; `px` and `%` are left symbolic
    /// (percent flows to Masharif / paint-space). Recurses into list/function values.
    void ComputeUnits(ComputedStyle& cs, const ComputeContext& ctx);

    /// True if any value still carries `vw`/`vh` (the resolver then subscribes to the viewport signal).
    [[nodiscard]] bool UsesViewportUnits(const ComputedStyle& cs);

    /// Apply `inherit`/`initial`, then inject the parent's value for undeclared inherited properties. Returns the
    /// InheritedStyle to publish.
    [[nodiscard]] InheritedStyle ApplyInheritance(ComputedStyle& cs, const InheritedStyle* parent, const VarMap& vars);
} // namespace lumora::style
