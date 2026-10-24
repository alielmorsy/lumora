#pragma once

#include <lumora/Foundation/NodeId.h>
#include <lumora/Widgets/ControlFlow.h>

#include <functional>
#include <memory>
#include <span>

namespace lumora {

struct AppCtx;
class NodeArena;
class Owner;

/// Registers the Show/Each/Memo kinds in the descriptor table (idempotent).
void EnsureControlKinds();

/// Drives a Memo's argument cache directly: rebuilds the cached subtree (disposing the old body) only when
/// `next.args` differs from the stored args by `==`. Returns true if it rebuilt.
bool MemoReconcile(AppCtx& ctx, NodeId memoNode, detail::MemoConfig next);

// Splicing primitives for runtime-managed child sets (control-flow hosts, VirtualList): they edit the child list in place and never allocate.

/// A freshly built body: its scope and instantiated root (invalid if the builder produced nothing); disposing the scope frees the subtree.
struct MountedBody {
    std::unique_ptr<Owner> scope;
    NodeId                 root;
};

/// Builds `builder` under a child scope of `host`'s owner (or of `scopeParent` when given) and instantiates the result. Does NOT link the
/// root into `host`: the caller splices it.
[[nodiscard]] MountedBody MountBody(AppCtx& ctx, NodeId host, const std::function<Elem()>& builder,
                                    Owner* scopeParent = nullptr);

void UnlinkChild(NodeArena& arena, NodeId parentId, NodeId childId);

/// Links an unlinked `child` into `parent` right before `before` in O(1); an invalid `before` appends.
void InsertChildBefore(NodeArena& arena, NodeId parent, NodeId child, NodeId before);

/// Rewrites the parent's child list to exactly `order`; surviving rows keep their solver nodes (a keyed reorder is a move).
void SetChildOrder(NodeArena& arena, NodeId parentId, std::span<const NodeId> order);

} // namespace lumora
