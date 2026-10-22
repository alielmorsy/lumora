#pragma once

#include <lumora/AssetLoader.h>
#include <lumora/Debug/FrameCounters.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>

#include <cstdint>
#include <vector>

namespace lumora
{
    class NodeArena;
    class ReactiveGraph;
    class LayoutTree;
    class TextMeasurer;
    class OverlayRoot;
    class ImageProvider;

    namespace style
    {
        class StyleEngine;
    }

    /// Per-app context threaded through the build walk instead of thread-globals.
    /// `layout`, `measurer` and `overlay` are null in headless/pre-layout contexts.
    struct AppCtx
    {
        NodeArena& arena;
        ReactiveGraph& graph;
        LayoutTree* layout = nullptr;
        const TextMeasurer* measurer = nullptr;
        OverlayRoot* overlay = nullptr;

        /// File/asset reader; null in a bare AppCtx (widgets then fall back to DefaultFileLoad).
        AssetLoader assets = nullptr;

        /// Image decoder/uploader; null in a bare AppCtx (Image then draws nothing).
        ImageProvider* images = nullptr;

        /// Per-app CSS engine; null when no stylesheet is wired. When set, it is the sole writer of styleable props.
        style::StyleEngine* style = nullptr;

        /// Cursor in surface pixels, mirrored from InteractionState each paint.
        Vec2 pointerPos{};

        /// OS DPI scale (logical -> physical); layout/paint coordinates stay logical.
        float contentScale = 1.f;

        /// Per-frame counters owned by MountedApp; null in a bare AppCtx.
        FrameCounters* counters = nullptr;

        /// True once an immediate-mode Painter is mounted. It may read the live cursor, so a mouse move must invalidate the retained recording.
        bool hasImmediatePainters = false;

        /// Node that requested autofocus during Build; focused after the first layout, then cleared. Last writer wins.
        NodeId pendingAutofocus{};
        std::vector<NodeId> pendingAutofocusList{};

        /// Live VirtualList nodes (generation-pinned); their windows are recomputed between the first solve and the re-solve.
        struct TrackedNode
        {
            NodeId        id{};
            std::uint32_t gen = 0;
        };
        std::vector<TrackedNode> virtualLists;
    };
} // namespace lumora
