#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Owner.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Overlay.h>
#include <lumora/Widgets/VirtualList.h>

#include "Node/Payloads.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace lumora {

class OverlayRoot;

// Out-of-line state for the transparent control-flow hosts: the erased blueprint config plus live bookkeeping (child scope(s), spliced subtree root(s)).
// Each branch/row owns exactly one root node; disposing its scope frees the subtree and runs its cleanups. Scopes parent to the control node's enclosing owner.

struct ShowState {
    detail::ShowConfig     config;
    std::unique_ptr<Owner> scope;            ///< current branch's scope, null when nothing mounted
    NodeId                 mounted;           ///< root of the mounted branch subtree
    bool                   showing    = false; ///< which branch the last reconcile mounted
    bool                   hasMounted = false; ///< false until the first reconcile, forcing a build
};

struct EachRowEntry {
    std::uint64_t          key = 0;
    std::unique_ptr<Owner> scope;
    NodeId                 root;
};

struct EachState {
    detail::EachConfig        config;
    std::vector<EachRowEntry> rows; ///< live rows in current child order
};

struct MemoState {
    detail::MemoConfig     config;
    std::unique_ptr<Owner> scope;
    NodeId                 mounted;
};

// VirtualList is an opaque scrolling viewport AND a host splicing a runtime-managed row set: it carries the container groups, ScrollCore fields and Each-style bookkeeping.
// `content` is a Column holding the rows so the sub-row scroll remainder rides its TransformState (no Scroll special cases in Paint/Dispatch).
struct VirtualRow {
    std::uint64_t          key   = 0;
    int                    index = 0;
    std::unique_ptr<Owner> scope;
    NodeId                 root;
};

struct VirtualListState {
    VirtualListCfg config;

    NodeId                  content;      ///< the inner Column the rows are spliced into
    std::unique_ptr<Owner>  contentScope; ///< disposed with the list, freeing the column subtree
    std::vector<VirtualRow> rows;         ///< live rows, in window order
    int           first   = 0;            ///< first materialized index
    int           last    = -1;           ///< last materialized index (inclusive); -1 == nothing yet
    int           count   = 0;            ///< last count read from the reactive source
    std::uint64_t version = 0;            ///< last version read; a change rebuilds every live row
    bool          rebuild = false;        ///< a version change is pending, so drop what is mounted

    ScrollAxis axis = ScrollAxis::Vertical;
    float offsetX = 0.f, offsetY = 0.f;
    float widest  = 0.f;        ///< Both: widest row reach seen since the rows last could have narrowed
    bool  forgetWidest = false; ///< the mounted rows predate a shrink/version bump: do not learn from them
    bool  draggingX = false, draggingY = false;
    float grabOffset = 0.f;
    Vec2  grabCursor{};
    bool  hovered = false, hoverX = false, hoverY = false;

    StyleState     style;
    ViewState      view{.visible = true, .clip = true}; ///< always clips: it is a viewport
    TransformState transform;
    InteractState  on;
};

// Portal mirrors Memo (build once; open/close lives in the wrapping Show/Presence), but its content is spliced under `layerNode` (an overlay layer);
// the content's scope still parents to the declaring owner.
struct PortalState {
    detail::PortalConfig   config;
    std::unique_ptr<Owner> scope;            ///< content subtree's scope (disposed on unmount)
    NodeId                 content;           ///< spliced content root, lives under layerNode
    NodeId                 layerNode;         ///< the overlay layer node we spliced into
    std::uint64_t          entryId = 0;       ///< registration id in the OverlayRoot
    OverlayRoot*           overlay = nullptr; ///< the app's overlay registry (null in headless/test)
};

} // namespace lumora
