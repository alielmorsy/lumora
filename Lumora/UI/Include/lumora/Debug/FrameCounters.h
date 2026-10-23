#pragma once

#include <chrono>
#include <cstdint>

namespace lumora
{
    namespace detail
    {
        /// Adds the elapsed time to `*target` on scope exit; a null target is a no-op.
        class ScopedAccumMs
        {
        public:
            explicit ScopedAccumMs(float* target) noexcept
                : m_target(target), m_start(target ? Clock::now() : Clock::time_point{})
            {
            }

            ~ScopedAccumMs()
            {
                if (m_target)
                {
                    *m_target += std::chrono::duration<float, std::milli>(Clock::now() - m_start).count();
                }
            }

        private:
            using Clock = std::chrono::high_resolution_clock;
            float*            m_target;
            Clock::time_point m_start;
        };
    }

    /// Per-frame work counters (counts, not wall time) taken at the choke points; reset at the top of Tick.
    /// Always compiled in: one well-predicted null check per counted event.
    struct FrameCounters
    {
        // layout
        std::uint32_t layoutRuns = 0;        ///< LayoutDriver::Run entries (MountedApp::Layout re-enters up to 3x)
        std::uint32_t layoutIdleBails = 0;   ///< Runs that returned early: nothing dirty, surface unchanged
        std::uint32_t refreshVisited = 0;    ///< nodes walked by Refresh looking for dirt
        std::uint32_t refreshDirty = 0;      ///< nodes actually found Dirty::Layout
        std::uint32_t applyStyleCalls = 0;   ///< LayoutStyle pushed into the solver
        std::uint32_t setChildrenCalls = 0;  ///< child lists re-published to the solver
        std::uint32_t solveCalls = 0;        ///< backend Solve() invocations (the flex passes)
        std::uint32_t readBackVisited = 0;   ///< nodes whose solved rect was read back
        std::uint32_t remeasureVisited = 0;  ///< nodes walked by the wrap-aware re-measure
        std::uint32_t remeasureRepins = 0;   ///< of those, the ones whose content size actually changed
        std::uint32_t gridsVisited = 0;      ///< nodes walked by the grid placement pass
        std::uint32_t gridsPlaced = 0;       ///< grids whose cells were re-placed

        // paint (emit walk)
        std::uint32_t emitVisited = 0;       ///< nodes entered by EmitNode
        std::uint32_t emitCulled = 0;        ///< children skipped by the clip-window test
        std::uint32_t emitHidden = 0;        ///< subtrees skipped because the node is not visible
        std::uint32_t emitLayers = 0;        ///< PushLayer pairs (transform / opacity compositing)
        std::uint32_t emitClips = 0;         ///< PushClip pairs
        std::uint32_t emitZSorts = 0;        ///< sibling lists that needed a z-order sort
        std::uint32_t emitRecorded = 0;      ///< nodes whose visual was actually re-recorded
        std::uint32_t emitReused = 0;        ///< subtrees republished from last frame's recording
        std::uint32_t emitReusedBytes = 0;   ///< bytes of recording those republishes copied
        std::uint32_t paintFrameReused = 0;  ///< 1 when the frame needed no walk at all (nothing dirty)
        std::uint32_t paintCanvasReused = 0; ///< 1 when the Canvas re-presented its own retained geometry
        std::uint32_t paintRecordedBytes = 0;///< size of the display list this frame replays
        std::uint32_t paintReplayed = 0;     ///< records pushed from the display list into the Canvas

        // reactive
        std::uint32_t framePasses = 0;            ///< frame-callback passes taken this Tick (0 or 1)
        std::uint32_t frameCallbacksInvoked = 0;  ///< OnFrame bodies actually called
        std::uint32_t frameCallbacksAlive = 0;    ///< registered callbacks after the reap
        std::uint32_t frameCallbacksReaped = 0;   ///< dead callbacks compacted away this Tick
        std::uint32_t structuralEdits = 0;        ///< deferred splices/disposes drained
        std::uint32_t deferredRun = 0;            ///< Defer() bodies run
        std::uint32_t effectRuns = 0;             ///< reactive bodies executed under tracking
        std::uint32_t effectsFlushed = 0;         ///< queue entries drained by FlushEffects

        // style
        std::uint32_t cascadeCalls = 0;      ///< full cascades (match + specificity + var resolve)
        std::uint32_t pseudoWrites = 0;      ///< SetPseudo calls that reached a styled node

        // text seam
        std::uint32_t textMeasureCalls = 0;  ///< TextMeasurer::Measure through the layout/paint seam
        std::uint32_t glyphCacheCalls = 0;   ///< TextMeasurer::BuildGlyphCache (shape + rasterise)

        // timing (ms): the one exception to counts-only, for DebugOverlay's perf panel
        float styleMs   = 0.f; ///< Refresh: ApplyStyle + SetChildren, pushing resolved style into the solver
        float measureMs = 0.f; ///< Widget::Measure (Refresh + Remeasure) and backend Solve() - sizing + flex math
        float paintMs   = 0.f; ///< EmitNode walk (Paint/Record) - building this frame's display list

        void Reset() noexcept { *this = FrameCounters{}; }
    };
}
