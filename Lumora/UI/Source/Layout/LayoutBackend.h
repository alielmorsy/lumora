#pragma once

#include <lumora/Foundation/Rect.h>
#include <lumora/Foundation/Vec2.h>

#include "Node/LayoutStyle.h"

#include <concepts>
#include <cstdint>
#include <span>

namespace lumora {

/// Per-node inputs the bridge hands the backend: resolved style plus the effective parent's main axis, computed by the
/// kind-agnostic driver so the backend never inspects the live tree.
struct SolverInput {
    const LayoutStyle& style;
    FlexAxis           parentAxis = FlexAxis::Column;
    bool               hasParent  = false;
};

/// Compile-time layout-backend trait (no virtuals). A backend wraps an external flexbox solver behind opaque LayoutHandles
/// and is the only code that names that solver.
template<typename B>
concept LayoutBackend = requires(B b, const B cb, LayoutHandle h, const SolverInput& in,
                                 std::span<const LayoutHandle> children, Vec2 size, float extent,
                                 bool axisFlag) {
    { b.Create() } -> std::same_as<LayoutHandle>;
    { b.Destroy(h) };
    { b.ApplyStyle(h, in) };
    { b.SetChildren(h, children) };
    { b.PinContentSize(h, size, axisFlag, axisFlag) };
    { b.PinSurface(h, extent, extent) };
    { b.Solve(h, extent, extent) };
    { cb.ReadRect(h) } -> std::same_as<Rect>;
    { cb.SolveCount(h) } -> std::same_as<std::uint32_t>;
};

} // namespace lumora
