#pragma once

#include <lumora/Widgets/Grid.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

// Pure grid math (no arena, no solver): px/fr track sizing and row-major auto-placement. See LayoutBridge.h::PlaceGrids.

namespace lumora::detail
{
    /// A grid item's requested placement, read from the header (col/row = -1 means auto on that axis).
    struct GridItemReq
    {
        int col = -1;
        int row = -1;
        int colSpan = 1;
        int rowSpan = 1;
    };

    struct GridPlacement
    {
        int col = 0;
        int row = 0;
        int colSpan = 1;
        int rowSpan = 1;
    };

    /// Resolve one axis's track sizes (px) from the content extent. Fr tracks split the leftover after
    /// px tracks and inter-track gaps; with no definite extent (a Wrap axis) every fr track is 0.
    [[nodiscard]] inline std::vector<float> ResolveTrackSizes(std::span<const TrackSize> tracks, float content,
                                                              float gap, bool definite)
    {
        std::vector<float> sizes(tracks.size(), 0.f);
        float fixed = 0.f;
        float frTotal = 0.f;
        for (const TrackSize& t : tracks)
        {
            if (t.kind == TrackSize::Kind::Px)
            {
                fixed += std::max(0.f, t.value);
            }
            else if (definite)
            {
                frTotal += std::max(0.f, t.value);
            }
        }
        const float totalGap = tracks.empty() ? 0.f : gap * static_cast<float>(tracks.size() - 1);
        const float leftover = std::max(0.f, content - fixed - totalGap);
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const TrackSize& t = tracks[i];
            sizes[i] = t.kind == TrackSize::Kind::Px
                           ? std::max(0.f, t.value)
                           : (definite && frTotal > 0.f ? leftover * (std::max(0.f, t.value) / frTotal) : 0.f);
        }
        return sizes;
    }

    [[nodiscard]] inline std::vector<float> TrackOffsets(std::span<const float> sizes, float gap)
    {
        std::vector<float> off(sizes.size(), 0.f);
        float acc = 0.f;
        for (std::size_t i = 0; i < sizes.size(); ++i)
        {
            off[i] = acc;
            acc += sizes[i] + gap;
        }
        return off;
    }

    [[nodiscard]] inline float SpanExtent(std::span<const float> sizes, int start, int span, float gap)
    {
        const int count = static_cast<int>(sizes.size());
        float total = 0.f;
        int taken = 0;
        for (int k = start; k < start + span && k < count; ++k)
        {
            total += sizes[k];
            ++taken;
        }
        if (taken > 1)
        {
            total += gap * static_cast<float>(taken - 1);
        }
        return total;
    }

    /// Resolve every item's concrete cell: explicit placements reserve their footprint first, then auto
    /// items take the next free row-major run. `out` is aligned with `items`; `outRowCount` is the total
    /// rows used (>= the explicit row template count, grown for overflow).
    inline void PlaceGridItems(int colCount, int explicitRows, std::span<const GridItemReq> items,
                               std::vector<GridPlacement>& out, int& outRowCount)
    {
        colCount = std::max(1, colCount);
        out.assign(items.size(), GridPlacement{});

        int rows = std::max(1, explicitRows);
        std::vector<std::uint8_t> occ(static_cast<std::size_t>(rows) * colCount, 0);

        const auto idx = [&](int r, int c) { return static_cast<std::size_t>(r) * colCount + c; };
        const auto ensureRows = [&](int need)
        {
            if (need > rows)
            {
                rows = need;
                occ.resize(static_cast<std::size_t>(rows) * colCount, 0);
            }
        };
        const auto isFree = [&](int r, int c, int cs, int rs) -> bool
        {
            if (c < 0 || c + cs > colCount || r < 0)
            {
                return false;
            }
            for (int rr = r; rr < r + rs; ++rr)
            {
                if (rr >= rows)
                {
                    continue; // beyond the allocated rows is implicitly free
                }
                for (int cc = c; cc < c + cs; ++cc)
                {
                    if (occ[idx(rr, cc)])
                    {
                        return false;
                    }
                }
            }
            return true;
        };
        const auto mark = [&](int r, int c, int cs, int rs)
        {
            ensureRows(r + rs);
            for (int rr = r; rr < r + rs; ++rr)
            {
                for (int cc = c; cc < c + cs; ++cc)
                {
                    occ[idx(rr, cc)] = 1;
                }
            }
        };

        // Pass 1: fully-explicit items reserve their footprint.
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const GridItemReq& it = items[i];
            if (it.col >= 0 && it.row >= 0)
            {
                const int c = std::min(it.col, colCount - 1);
                const int cs = std::clamp(std::max(1, it.colSpan), 1, colCount - c);
                const int r = std::max(0, it.row);
                const int rs = std::max(1, it.rowSpan);
                mark(r, c, cs, rs);
                out[i] = {c, r, cs, rs};
            }
        }

        // Pass 2: auto + partially-explicit items fill the next free run (row-major).
        int curR = 0;
        int curC = 0;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            const GridItemReq& it = items[i];
            if (it.col >= 0 && it.row >= 0)
            {
                continue; // already placed
            }
            const int rs = std::max(1, it.rowSpan);

            if (it.col >= 0) // explicit column, auto row: scan downward
            {
                const int c = std::min(std::max(0, it.col), colCount - 1);
                const int cs = std::clamp(std::max(1, it.colSpan), 1, colCount - c);
                int r = 0;
                while (true)
                {
                    ensureRows(r + rs);
                    if (isFree(r, c, cs, rs))
                    {
                        break;
                    }
                    ++r;
                }
                mark(r, c, cs, rs);
                out[i] = {c, r, cs, rs};
                continue;
            }

            const int cs = std::clamp(std::max(1, it.colSpan), 1, colCount);
            if (it.row >= 0) // explicit row, auto column: scan across
            {
                const int r = std::max(0, it.row);
                int c = 0;
                while (c <= colCount - cs && !isFree(r, c, cs, rs))
                {
                    ++c;
                }
                if (c > colCount - cs)
                {
                    c = 0;
                }
                mark(r, c, cs, rs);
                out[i] = {c, r, cs, rs};
                continue;
            }

            // Fully auto: advance the row-major cursor to the next free cs-wide run.
            while (true)
            {
                if (curC + cs > colCount)
                {
                    curC = 0;
                    ++curR;
                    continue;
                }
                ensureRows(curR + rs);
                if (isFree(curR, curC, cs, rs))
                {
                    out[i] = {curC, curR, cs, rs};
                    mark(curR, curC, cs, rs);
                    curC += cs;
                    break;
                }
                ++curC;
            }
        }

        outRowCount = std::max(1, rows);
    }
} // namespace lumora::detail
