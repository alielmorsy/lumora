#include <lumora/Lumora.h>

#include "Node/Kind.h"
#include "Node/KindDescriptor.h"
#include "Node/LiveNode.h"
#include "Node/NodeArena.h"
#include "Runtime/TreeDump.h"
#include "Widgets/Widget.h"

#include <lumora/log/Log.h>

#include <format>
#include <string>

LUMORA_LOG_CATEGORY("Lumora")

namespace lumora {

std::string_view Version() noexcept {
    return "0.1.0";
}

namespace {

[[nodiscard]] const char* AxisName(FlexAxis a) {
    switch (a) {
        case FlexAxis::Row:    return "Row";
        case FlexAxis::Column: return "Col";
        case FlexAxis::Stack:  return "Stk";
        case FlexAxis::Grid:   return "Grd";
    }
    return "?";
}

[[nodiscard]] char AlignCh(Align a) {
    switch (a) {
        case Align::Start:   return 'S';
        case Align::Center:  return 'C';
        case Align::End:     return 'E';
        case Align::Stretch: return 'X';
        case Align::Between: return 'B';
        case Align::Around:  return 'A';
        case Align::Evenly:  return 'V';
        case Align::Auto:    return '-';
    }
    return '?';
}

[[nodiscard]] std::string SizeStr(const SizeHint& h) {
    switch (h.kind) {
        case SizeHint::Kind::Fixed:   return std::format("{:.0f}", h.value);
        case SizeHint::Kind::Percent: return std::format("{:.0f}%", h.value);
        case SizeHint::Kind::Expand:  return "grow";
        case SizeHint::Kind::Wrap:    return "wrap";
    }
    return "?";
}

[[nodiscard]] std::string DirtyStr(Dirty d) {
    if (d == Dirty::None) {
        return {};
    }
    std::string s = "  dirty[";
    if (HasFlag(d, Dirty::Style))   s += 'S';
    if (HasFlag(d, Dirty::Layout))  s += 'L';
    if (HasFlag(d, Dirty::Repaint)) s += 'R';
    s += ']';
    return s;
}

void AppendNode(std::string& out, const NodeArena& arena, NodeId id, int depth) {
    if (!id.Valid()) {
        return;
    }
    const LiveNode& n = arena.Resolve(id);

    const char* name = "?";
    if (Widget* w = KindRegistry::For(n.kind)) {
        name = w->Name();
    }

    out.append(static_cast<std::size_t>(depth) * 2u, ' ');
    out += std::format("{} #{}  [{:.0f} {:.0f} {:.0f} {:.0f}]  {} {}/{} gap {:.0f}  {} x {}  ch {}{}\n", name,
                       n.id.value, n.solved.x, n.solved.y, n.solved.width, n.solved.height,
                       AxisName(n.style.axis), AlignCh(n.style.main), AlignCh(n.style.cross), n.style.gap,
                       SizeStr(n.style.width), SizeStr(n.style.height), n.childCount, DirtyStr(n.dirty));

    for (NodeId c = n.firstChild; c.Valid(); c = arena.Resolve(c).nextSibling) {
        AppendNode(out, arena, c, depth + 1);
    }
}

} // namespace

void LogLiveTree(const NodeArena& arena, NodeId root) {
    if (!root.Valid()) {
        return;
    }
    std::string out;
    AppendNode(out, arena, root, 0);
    LOG_INFO("Lumora tree re-rendered (dirty):\n{}", out);
}

} // namespace lumora
