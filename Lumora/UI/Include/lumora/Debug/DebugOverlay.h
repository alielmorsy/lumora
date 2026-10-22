#pragma once

#include <lumora/Api.h>
#include <lumora/Foundation/Rect.h>

#include <cstdint>

namespace lumora
{
    /// Debug overlay layers (bitmask). Compiled only in debug builds; `Default` is what the runtime enables.
    enum class DebugOverlayFlags : std::uint32_t
    {
        None = 0,
        Boxes = 1u << 0, ///< thin depth-tinted skeleton outline on every node
        PaddingMargin = 1u << 1, ///< shade padding + margin regions on EVERY node (dense)
        Gaps = 1u << 2, ///< shade flex-gap strips on EVERY container (dense)
        AxisHints = 1u << 3, ///< axis + main/cross alignment tag on EVERY container (dense)
        ClipRegions = 1u << 4, ///< distinct outline on Scroll / clipping nodes
        GlyphBorders = 1u << 5, ///< a border around every laid-out text glyph (dense)
        HoverInspector = 1u << 6,
        ///< full box model, spacing values and glyph borders for the hovered node only
        Legend = 1u << 7, ///< compact colour-key HUD

        /// Low-noise view: faint skeleton plus clip markers, full detail only for the hovered node.
        Default = Boxes | ClipRegions | HoverInspector | Legend,

        /// Everything on every node at once; dense.
        All = Boxes | PaddingMargin | Gaps | AxisHints | ClipRegions | GlyphBorders | HoverInspector
        | Legend,
    };

    [[nodiscard]] constexpr DebugOverlayFlags operator|(DebugOverlayFlags a, DebugOverlayFlags b) noexcept
    {
        return static_cast<DebugOverlayFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }

    [[nodiscard]] constexpr DebugOverlayFlags operator&(DebugOverlayFlags a, DebugOverlayFlags b) noexcept
    {
        return static_cast<DebugOverlayFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }

    constexpr DebugOverlayFlags& operator|=(DebugOverlayFlags& a, DebugOverlayFlags b) noexcept
    {
        return a = a | b;
    }

    [[nodiscard]] constexpr DebugOverlayFlags operator~(DebugOverlayFlags a) noexcept
    {
        return static_cast<DebugOverlayFlags>(~static_cast<std::uint32_t>(a));
    }

    [[nodiscard]] constexpr DebugOverlayFlags operator^(DebugOverlayFlags a, DebugOverlayFlags b) noexcept
    {
        return static_cast<DebugOverlayFlags>(static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
    }

    constexpr DebugOverlayFlags& operator^=(DebugOverlayFlags& a, DebugOverlayFlags b) noexcept
    {
        return a = a ^ b;
    }

    [[nodiscard]] constexpr bool HasFlag(DebugOverlayFlags set, DebugOverlayFlags bit) noexcept
    {
        return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(bit)) != 0;
    }

    class Canvas;
    class TextMeasurer;


    /// Whole-panel bounds in surface space, so the host can swallow clicks that land on the chrome.
    [[nodiscard]] LUMORA_API Rect DebugControlsBounds(Vec2 surfaceSize) noexcept;

    /// The single layer toggled by a click at `point`, or `None` when no row is under it.
    [[nodiscard]] LUMORA_API DebugOverlayFlags HitTestDebugControls(Vec2 surfaceSize, Vec2 point) noexcept;

    /// Draw the panel (one checkbox row per layer); `hovered` is HitTestDebugControls' result. Null `fonts` draws no text.
    LUMORA_API void PaintDebugControls(Canvas& canvas, const TextMeasurer* fonts, Vec2 surfaceSize,
                            DebugOverlayFlags flags, DebugOverlayFlags hovered);

    /// Snapshot of this process's resource usage, sampled by the host (lumora::platform::ProcessMonitor).
    struct PerfStats
    {
        float         cpuPercent = 0.f; // 0..100, share of total CPU capacity used by this process
        std::uint64_t ramBytes   = 0;   // resident working-set size, bytes
        float         styleMs    = 0.f; // last frame's FrameCounters::styleMs, summed across mounted apps
        float         measureMs  = 0.f; // last frame's FrameCounters::measureMs, summed across mounted apps
        float         paintMs    = 0.f; // last frame's FrameCounters::paintMs, summed across mounted apps
    };

    /// CPU / RAM panel at the top-right, drawn on top of the emitted UI. Null `fonts` draws no text. Toggled by F11.
    LUMORA_API void PaintPerfOverlay(Canvas& canvas, const TextMeasurer* fonts, Vec2 surfaceSize, const PerfStats& stats);
}
