// Dock.cpp - docking model (DockTree edits), its projection onto widgets, and persistence.
// Handlers may edit the model directly: Dispatch defers the Each reconcile to the next Tick.

#include <lumora/Dock/Dock.h>

#include <lumora/Foundation/Geometry.h>
#include <lumora/Paint/Paint2D.h>
#include <lumora/Widgets/Box.h>
#include <lumora/Widgets/ControlFlow.h>
#include <lumora/Widgets/Layouts.h>
#include <lumora/Widgets/Misc.h>
#include <lumora/Widgets/Painter.h>
#include <lumora/Widgets/Text.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lumora
{
    struct DockState : RefCounted
    {
        Signal<DockTree> model;
        DockRegistry     registry;
        DockTheme        theme;

        enum class Drag : std::uint8_t { None, Tab, Float, Resize };
        Signal<bool> dragging{false};
        Drag         mode = Drag::None;
        PanelId      tabPanel{};
        int          floatIndex = -1;
        Vec2         grabOffset{};
        Vec2         dragSize{280.f, 200.f};
        Rect         resizePreview{};

        /// The dock area in surface pixels, refreshed each paint by the workspace reporter layer.
        Rect workspace{};

        /// Bumped per snapshot so the single-row Each always reconciles to a fresh key (full re-projection).
        std::uint64_t rev = 0;

        /// Refreshed every paint by per-leaf reporter Painters; the drop hit-test reads it.
        struct Target { std::vector<int> path; Rect rect; };
        std::unordered_map<std::string, Target> targets;

        DockDetachHooks                    detach;
        DockBehavior                       behavior;
        bool                               outside = false;
        bool                               external = false;
        std::optional<Vec2>                externalPointer;
        Signal<std::uint64_t>              overlayRev{0};

        explicit DockState(DockTree t) : model(std::move(t)) {}
    };

    namespace
    {

        [[nodiscard]] DockNode* NodeAt(DockNode& root, const std::vector<int>& path)
        {
            DockNode* cur = &root;
            for (const int i : path)
            {
                if (!cur->IsSplit() || i < 0 || i >= static_cast<int>(cur->children.size())) return nullptr;
                cur = &cur->children[static_cast<std::size_t>(i)];
            }
            return cur;
        }

        /// Path of the first Tabs leaf containing `panel` (DFS), or false if absent.
        bool FindLeaf(const DockNode& n, PanelId panel, std::vector<int>& out)
        {
            if (n.IsTabs())
                return std::find(n.panels.begin(), n.panels.end(), panel) != n.panels.end();
            for (int i = 0; i < static_cast<int>(n.children.size()); ++i)
            {
                out.push_back(i);
                if (FindLeaf(n.children[static_cast<std::size_t>(i)], panel, out)) return true;
                out.pop_back();
            }
            return false;
        }

        /// Path of the first Tabs leaf in document order (used as a fallback insertion site).
        bool FindFirstLeaf(const DockNode& n, std::vector<int>& out)
        {
            if (n.IsTabs()) return true;
            for (int i = 0; i < static_cast<int>(n.children.size()); ++i)
            {
                out.push_back(i);
                if (FindFirstLeaf(n.children[static_cast<std::size_t>(i)], out)) return true;
                out.pop_back();
            }
            return false;
        }

        /// Remove `panel` from a subtree (leaves any resulting empties for Normalise to collapse).
        bool RemoveFromNode(DockNode& n, PanelId panel)
        {
            if (n.IsTabs())
            {
                const auto it = std::find(n.panels.begin(), n.panels.end(), panel);
                if (it == n.panels.end()) return false;
                const int removed = static_cast<int>(it - n.panels.begin());
                n.panels.erase(it);
                if (n.active > removed) --n.active;
                n.active = std::clamp(n.active, 0, std::max(0, static_cast<int>(n.panels.size()) - 1));
                return true;
            }
            for (auto& c : n.children)
                if (RemoveFromNode(c, panel)) return true;
            return false;
        }

        /// Canonicalise a node: drop empty children, splice same-axis child splits, collapse a
        /// single-child split into its child. Returns true if the node is now empty.
        bool Normalize(DockNode& n)
        {
            if (n.IsTabs()) return n.panels.empty();

            std::vector<DockNode> kids;
            std::vector<float>    ws;
            kids.reserve(n.children.size());
            ws.reserve(n.children.size());

            for (std::size_t i = 0; i < n.children.size(); ++i)
            {
                DockNode&   c = n.children[i];
                const float w = i < n.weights.size() ? n.weights[i] : 1.f;
                if (Normalize(c)) continue;

                if (c.IsSplit() && c.axis == n.axis)
                {
                    float total = 0.f;
                    for (const float sw : c.weights) total += sw;
                    if (total <= 0.f) total = static_cast<float>(c.children.size());
                    for (std::size_t j = 0; j < c.children.size(); ++j)
                    {
                        const float cw = j < c.weights.size() ? c.weights[j] : 1.f;
                        kids.push_back(std::move(c.children[j]));
                        ws.push_back(w * (cw / total));
                    }
                }
                else
                {
                    kids.push_back(std::move(c));
                    ws.push_back(w);
                }
            }

            n.children = std::move(kids);
            n.weights  = std::move(ws);
            if (n.children.empty()) return true;
            if (n.children.size() == 1)
            {
                DockNode only = std::move(n.children[0]);
                n = std::move(only);
            }
            return false;
        }

        void NormalizeTree(DockTree& t)
        {
            if (Normalize(t.root)) t.root = DockNode::Tabs({});
            for (auto it = t.floats.begin(); it != t.floats.end();)
            {
                if (Normalize(it->root)) it = t.floats.erase(it);
                else ++it;
            }
        }

        bool RemovePanel(DockTree& t, PanelId panel)
        {
            if (RemoveFromNode(t.root, panel)) return true;
            for (auto& f : t.floats)
                if (RemoveFromNode(f.root, panel)) return true;
            return false;
        }

        void TabifyInto(DockTree& t, const std::vector<int>& q, const std::vector<PanelId>& add)
        {
            DockNode* leaf = NodeAt(t.root, q);
            if (!leaf || !leaf->IsTabs()) return;
            const int first = static_cast<int>(leaf->panels.size());
            leaf->panels.insert(leaf->panels.end(), add.begin(), add.end());
            leaf->active = first;
        }

        /// Insert `ins` as a sibling of the node at `q`, splitting along the side's axis.
        void SplitInsert(DockTree& t, const std::vector<int>& q, DockNode ins, DockSide side)
        {
            const DockAxis axis = (side == DockSide::Left || side == DockSide::Right)
                                      ? DockAxis::Horizontal
                                      : DockAxis::Vertical;
            const bool before = (side == DockSide::Left || side == DockSide::Top);

            auto wrap = [&](DockNode& target)
            {
                DockNode old = std::move(target);
                std::vector<DockNode> kids;
                if (before) { kids.push_back(std::move(ins)); kids.push_back(std::move(old)); }
                else        { kids.push_back(std::move(old)); kids.push_back(std::move(ins)); }
                target = DockNode::Split(axis, std::move(kids), {1.f, 1.f});
            };

            if (q.empty()) { wrap(t.root); return; }

            const std::vector<int> pp(q.begin(), q.end() - 1);
            DockNode* parent = NodeAt(t.root, pp);
            const int idx = q.back();

            if (parent && parent->IsSplit() && parent->axis == axis)
            {
                const int at = before ? idx : idx + 1;
                float avg = 0.f;
                for (const float w : parent->weights) avg += w;
                avg = parent->weights.empty() ? 1.f : avg / static_cast<float>(parent->weights.size());
                parent->children.insert(parent->children.begin() + at, std::move(ins));
                parent->weights.insert(parent->weights.begin() + at, avg);
            }
            else if (DockNode* target = NodeAt(t.root, q))
            {
                wrap(*target);
            }
        }

        /// A stable panel in the target leaf, to re-locate it after a removal shifts paths; empty if none.
        [[nodiscard]] PanelId AnchorAt(const DockTree& t, const std::vector<int>& path, PanelId exclude)
        {
            const DockNode* cur = &t.root;
            for (const int i : path)
            {
                if (!cur->IsSplit() || i < 0 || i >= static_cast<int>(cur->children.size())) return {};
                cur = &cur->children[static_cast<std::size_t>(i)];
            }
            if (!cur->IsTabs()) return {};
            for (const PanelId p : cur->panels)
                if (!(p == exclude)) return p;
            return {};
        }

        /// Move `panel` next to `anchor`'s leaf (or into it, for Center). Robust to the removal having
        /// collapsed the tree.
        void MovePanelTo(DockTree& t, PanelId panel, PanelId anchor, DockSide side)
        {
            RemovePanel(t, panel);
            NormalizeTree(t);
            if (anchor == PanelId{})
            {
                if (t.root.IsTabs() && t.root.panels.empty()) { t.root = DockNode::Tabs({panel}); return; }
                std::vector<int> q;
                if (FindFirstLeaf(t.root, q)) TabifyInto(t, q, {panel});
                return;
            }
            std::vector<int> q;
            if (!FindLeaf(t.root, anchor, q)) return;
            if (side == DockSide::Center) TabifyInto(t, q, {panel});
            else                          SplitInsert(t, q, DockNode::Tabs({panel}), side);
            NormalizeTree(t);
        }

        /// Dock a whole detached subtree (e.g. a float's contents) next to `anchor`'s leaf.
        void MoveNodeTo(DockTree& t, DockNode sub, PanelId anchor, DockSide side)
        {
            std::vector<int> q;
            if (!(anchor == PanelId{}) && FindLeaf(t.root, anchor, q))
            {
                if (side == DockSide::Center && sub.IsTabs())
                    TabifyInto(t, q, sub.panels);
                else
                    SplitInsert(t, q, std::move(sub), side);
            }
            else if (t.root.IsTabs() && t.root.panels.empty())
            {
                t.root = std::move(sub);
            }
            else
            {
                SplitInsert(t, {}, std::move(sub), side);
            }
            NormalizeTree(t);
        }

        /// Clamp a float frame to the theme minimum and ~workspace size; always keeps the grab strip reachable.
        [[nodiscard]] Rect ClampFloatFrame(const DockState& st, Rect f)
        {
            const DockTheme& th = st.theme;
            const Rect&      ws = st.workspace;
            if (ws.width > 0.f && ws.height > 0.f)
            {
                f.width  = std::min(f.width, ws.width * 0.9f);
                f.height = std::min(f.height, ws.height * 0.9f);
            }
            f.width  = std::max(f.width, th.FloatMinW);
            f.height = std::max(f.height, th.FloatMinH);
            if (ws.width > 0.f && ws.height > 0.f)
            {
                const float loX = ws.x - f.width + 48.f;
                f.x = std::clamp(f.x, loX, std::max(loX, ws.Right() - 48.f));
                f.y = std::clamp(f.y, ws.y, std::max(ws.y, ws.Bottom() - th.TabBarHeight));
            }
            return f;
        }


        [[nodiscard]] float GuideSize(const Rect& r)
        {
            return std::min({34.f, r.width * 0.22f, r.height * 0.22f});
        }

        [[nodiscard]] Rect GuideRect(const Rect& r, DockSide side)
        {
            const float size = GuideSize(r);
            const float step = size + 4.f;
            Vec2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
            switch (side)
            {
                case DockSide::Left:   c.x -= step; break;
                case DockSide::Right:  c.x += step; break;
                case DockSide::Top:    c.y -= step; break;
                case DockSide::Bottom: c.y += step; break;
                case DockSide::Center: break;
            }
            return {c.x - size * 0.5f, c.y - size * 0.5f, size, size};
        }

        [[nodiscard]] std::optional<DockSide> GuideZoneAt(const Rect& r, Vec2 m)
        {
            constexpr DockSide sides[] = {DockSide::Center, DockSide::Left, DockSide::Right,
                                          DockSide::Top, DockSide::Bottom};
            for (const DockSide s : sides)
                if (GuideRect(r, s).Contains(m)) return s;
            return std::nullopt;
        }

        struct DropResult
        {
            std::vector<int>        path;
            Rect                    rect;
            std::optional<DockSide> side;
            PanelId                 anchor{};
        };

        /// The smallest (deepest) leaf whose reported rect contains `m`, with its guide zone (if any)
        /// and a re-locate anchor.
        [[nodiscard]] std::optional<DropResult> ResolveDrop(const DockState& st, Vec2 m, PanelId exclude)
        {
            const DockState::Target* best = nullptr;
            float bestArea = std::numeric_limits<float>::max();
            for (const auto& [key, t] : st.targets)
            {
                if (!t.rect.Contains(m)) continue;
                const float area = t.rect.width * t.rect.height;
                if (area < bestArea) { bestArea = area; best = &t; }
            }
            if (!best) return std::nullopt;

            DropResult r;
            r.path   = best->path;
            r.rect   = best->rect;
            r.side   = GuideZoneAt(best->rect, m);
            r.anchor = AnchorAt(st.model.Peek(), best->path, exclude);
            return r;
        }

        [[nodiscard]] Rect ZonePreview(const Rect& r, DockSide side)
        {
            switch (side)
            {
                case DockSide::Left:   return {r.x, r.y, r.width * 0.5f, r.height};
                case DockSide::Right:  return {r.x + r.width * 0.5f, r.y, r.width * 0.5f, r.height};
                case DockSide::Top:    return {r.x, r.y, r.width, r.height * 0.5f};
                case DockSide::Bottom: return {r.x, r.y + r.height * 0.5f, r.width, r.height * 0.5f};
                case DockSide::Center: break;
            }
            return r;
        }

        void Collect(const DockNode& n, std::vector<PanelId>& out)
        {
            out.insert(out.end(), n.panels.begin(), n.panels.end());
            for (const DockNode& c : n.children) Collect(c, out);
        }


        using Locator = std::function<DockNode*(DockTree&)>;

        [[nodiscard]] std::string PathKey(const std::vector<int>& path)
        {
            std::string s;
            for (const int i : path) { s += std::to_string(i); s += '.'; }
            return s;
        }

        // Per-Painter drag bookkeeping (tabs + float title bars): press origin, whether a drag has
        // started, and the last painted rect (so click-on-close hit-tests without Bounds in handlers).
        struct DragGesture : RefCounted
        {
            Vec2 start{};
            Rect rect{};
            bool pressed = false;
            bool started = false;
        };

        Elem ProjectNode(const Ref<DockState>& st, const DockNode& node, Locator loc,
                         std::optional<std::vector<int>> dropPath);

        /// A pointer-transparent overlay leaf that publishes its solved rect as a drop target each paint.
        Elem Reporter(const Ref<DockState>& st, std::vector<int> path)
        {
            const std::string key = PathKey(path);
            return Painter({
                .width   = Expand,
                .height  = Expand,
                .onPaint = [st, path, key](Paint2D& p) { st->targets[key] = {path, p.Bounds()}; },
            });
        }

        /// Solved size of the pane `panel` lives in; a drag-out opens its float at this size.
        [[nodiscard]] Vec2 SourcePaneSize(const DockState& st, PanelId panel)
        {
            const DockTree& t = st.model.Peek();
            if (std::vector<int> path; FindLeaf(t.root, panel, path))
            {
                const auto it = st.targets.find(PathKey(path));
                if (it != st.targets.end() && it->second.rect.width > 0.f)
                    return {it->second.rect.width, it->second.rect.height};
            }
            for (const FloatWindow& f : t.floats)
            {
                std::vector<int> path;
                if (FindLeaf(f.root, panel, path)) return {f.frame.width, f.frame.height};
            }
            return {280.f, 200.f};
        }

        Elem TabChip(const Ref<DockState>& st, Locator loc, int tabIndex, PanelId pid, std::string title,
                     bool active, bool closable)
        {
            const DockTheme th = st->theme;
            auto gd = MakeRef<DragGesture>();

            const float width = std::clamp(24.f + static_cast<float>(title.size()) * 7.2f
                                               + (closable ? 18.f : 0.f),
                                           72.f, 220.f);
            const Color textCol = active ? th.Text : th.TextDim;

            return Painter({
                .width  = Fixed(width),
                .height = Fixed(th.TabBarHeight),
                .onPaint = [=](Paint2D& p)
                {
                    const Rect r = p.Bounds();
                    gd->rect = r;
                    if (active)
                    {
                        p.FillRect(r, th.TabActive);
                        p.FillRect({r.x, r.y, r.width, 2.f}, th.Accent);
                    }
                    else
                    {
                        if (p.Hovered())            p.FillRect(r, th.TabHover);
                        else if (th.TabIdle.a > 0)  p.FillRect(r, th.TabIdle);
                        p.DrawLine({r.Right() - 0.5f, r.y + 8.f}, {r.Right() - 0.5f, r.Bottom() - 8.f},
                                   th.Border, 1.f);
                    }
                    p.DrawString(title, {r.x + 12.f, r.y + (r.height - 13.f) * 0.5f}, textCol, 13.f);
                    if (closable && (p.Hovered() || active))
                    {
                        const Vec2 c{r.Right() - 14.f, r.y + r.height * 0.5f};
                        if ((p.Mouse() - c).Length() < 8.f) p.FillCircle(c, 8.f, th.TabHover);
                        p.DrawLine({c.x - 3.f, c.y - 3.f}, {c.x + 3.f, c.y + 3.f}, textCol, 1.2f);
                        p.DrawLine({c.x - 3.f, c.y + 3.f}, {c.x + 3.f, c.y - 3.f}, textCol, 1.2f);
                    }
                },
                .on = {
                    .onPress = [gd](PointerInfo pi)
                    {
                        gd->pressed = true;
                        gd->started = false;
                        gd->start = pi.position;
                    },
                    .onRelease = [st, gd, loc, pid, closable, tabIndex](PointerInfo pi)
                    {
                        const Vec2 pos = pi.position;
                        if (gd->started)
                        {
                            const PanelId dragged = pid;
                            if (st->outside && st->detach.onDragOutside) st->detach.onDragOutside(dragged, std::nullopt);
                            st->outside = false;

                            const DockBehavior behavior = st->behavior;
                            const bool         inside   = st->workspace.Contains(pos);
                            std::optional<DropResult> drop;
                            if (inside && behavior.acceptsDrops) drop = ResolveDrop(*st, pos, dragged);

                            if (drop && drop->side)
                            {
                                st->model.Update([&](DockTree& t)
                                {
                                    if (drop->anchor == PanelId{}) return;
                                    MovePanelTo(t, dragged, drop->anchor, *drop->side);
                                });
                            }
                            else
                            {
                                const bool wantsHost = !inside || behavior.floats == DockFloatPolicy::Detach
                                                    || !behavior.acceptsDrops;
                                if (wantsHost && st->detach.onDetach && st->detach.onDetach(dragged, pos, st->dragSize))
                                {
                                    st->model.Update([&](DockTree& t) { RemovePanel(t, dragged); NormalizeTree(t); });
                                }
                                else if (behavior.floats == DockFloatPolicy::InWindow && behavior.acceptsDrops)
                                {
                                    st->model.Update([&](DockTree& t)
                                    {
                                        RemovePanel(t, dragged);
                                        NormalizeTree(t);
                                        const Rect frame{pos.x - 60.f, pos.y - st->theme.TabBarHeight * 0.5f,
                                                         st->dragSize.x, st->dragSize.y};
                                        t.floats.push_back({DockNode::Tabs({dragged}), ClampFloatFrame(*st, frame)});
                                    });
                                }
                            }
                            st->mode = DockState::Drag::None;
                            st->dragging = false;
                        }
                        else if (gd->pressed)
                        {
                            const bool onClose = closable && pos.x > gd->rect.Right() - 22.f;
                            if (onClose)
                            {
                                st->model.Update([&](DockTree& t) { RemovePanel(t, pid); NormalizeTree(t); });
                            }
                            else
                            {
                                st->model.Update([&](DockTree& t)
                                {
                                    if (DockNode* leaf = loc(t); leaf && leaf->IsTabs())
                                        leaf->active = std::clamp(tabIndex, 0,
                                                                  std::max(0, static_cast<int>(leaf->panels.size()) - 1));
                                });
                            }
                        }
                        gd->pressed = false;
                        gd->started = false;
                    },
                    .onMove = [st, gd, pid](PointerInfo pi)
                    {
                        const Vec2 pos = pi.position;
                        if (gd->pressed && !gd->started && (pos - gd->start).Length() > 5.f)
                        {
                            gd->started  = true;
                            st->mode     = DockState::Drag::Tab;
                            st->tabPanel = pid;
                            st->dragSize = SourcePaneSize(*st, pid);
                            st->dragging = true;
                        }
                        if (gd->started && st->detach.onDragOutside)
                        {
                            const bool outside = !st->workspace.Contains(pos);
                            if (outside) st->detach.onDragOutside(pid, pos);
                            else if (st->outside) st->detach.onDragOutside(pid, std::nullopt);
                            st->outside = outside;
                        }
                    },
                },
            });
        }

        /// The tab strip of a Tabs leaf: flush chips on the bar, plus an optional trailing element
        /// filling the remainder (the float chrome passes its window-drag surface there).
        Elem TabStrip(const Ref<DockState>& st, const DockNode& node, Locator loc, Elem trailing)
        {
            const DockTheme th = st->theme;

            std::vector<Elem> cells;
            cells.reserve(node.panels.size() + 1);
            for (int i = 0; i < static_cast<int>(node.panels.size()); ++i)
            {
                const PanelId    pid = node.panels[static_cast<std::size_t>(i)];
                const DockPanel* dp  = st->registry.Find(pid);
                cells.push_back(TabChip(st, loc, i, pid, dp ? dp->title : std::string("?"),
                                        i == node.active, dp ? dp->closable : true));
            }
            if (trailing) cells.push_back(std::move(trailing));

            return Box({.layout = {.width = Expand, .height = Fixed(th.TabBarHeight), .cross = Align::Stretch},
                        .style = {.fill = th.TabBar}},
                       Row({.layout = {.width = Expand, .height = Fixed(th.TabBarHeight),
                                       .cross = Align::Stretch, .gap = 0.f}},
                           cells));
        }

        Elem ProjectTabs(const Ref<DockState>& st, const DockNode& node, Locator loc,
                         std::optional<std::vector<int>> dropPath, Elem trailing = {})
        {
            const DockTheme th = st->theme;

            Elem bar = TabStrip(st, node, loc, std::move(trailing));

            Elem content;
            if (!node.panels.empty())
            {
                const int        idx = std::clamp(node.active, 0, static_cast<int>(node.panels.size()) - 1);
                const DockPanel* dp  = st->registry.Find(node.panels[static_cast<std::size_t>(idx)]);
                content = (dp && dp->build) ? dp->build()
                                            : Box({.layout = {.width = Expand, .height = Expand}});
            }
            else
            {
                content = Box({.layout = {.width = Expand, .height = Expand, .main = Align::Center,
                                          .cross = Align::Center}},
                              Text("Drop a panel here", {.size = 13.f, .color = th.TextDim}));
            }
            Elem contentBox = Box({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch},
                                   .view = {.clip = true}},
                                  std::move(content));

            Elem group = Column({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch}},
                                std::move(bar), std::move(contentBox));

            if (!dropPath) return group;
            return Stack({.layout = {.width = Expand, .height = Expand}}, Reporter(st, *dropPath), std::move(group));
        }

        Elem ProjectSplit(const Ref<DockState>& st, const DockNode& node, Locator loc,
                          std::optional<std::vector<int>> dropPath)
        {
            const DockTheme th    = st->theme;
            const bool      horiz = node.axis == DockAxis::Horizontal;

            // Live per-split weights: the splitter writes them on drag and panes read them reactively
            // (no rebuild); release commits them to the model.
            Signal<std::vector<float>> weightsSig{node.weights};

            std::vector<Elem> kids;
            for (int i = 0; i < static_cast<int>(node.children.size()); ++i)
            {
                if (i > 0)
                {
                    const int   gap = i;
                    const float w0  = node.weights[static_cast<std::size_t>(gap) - 1]
                                    + node.weights[static_cast<std::size_t>(gap)];

                    // Convert prospective pane pixel sizes to weights, preserving the pair's combined share (w0).
                    const auto applyWeights = [gap, w0](std::vector<float>& w, float aPx, float bPx)
                    {
                        const float total = aPx + bPx;
                        if (total <= 0.f || gap < 1 || gap >= static_cast<int>(w.size())) return;
                        w[static_cast<std::size_t>(gap) - 1] = w0 * aPx / total;
                        w[static_cast<std::size_t>(gap)]     = w0 * bPx / total;
                    };

                    kids.push_back(Splitter({
                        .axis      = node.axis,
                        .thickness = th.SplitterSize,
                        .minPanePx = th.MinPanePx,
                        .color     = th.Splitter,
                        .onDrag    = [weightsSig, applyWeights](float aPx, float bPx)
                        {
                            weightsSig.Update([&](std::vector<float>& w) { applyWeights(w, aPx, bPx); });
                        },
                        .onCommit  = [st, loc, applyWeights](float aPx, float bPx)
                        {
                            st->model.Update([&](DockTree& t)
                            {
                                if (DockNode* s = loc(t); s && s->IsSplit()) applyWeights(s->weights, aPx, bPx);
                            });
                        },
                    }));
                }

                auto sizeFor = [weightsSig, i]() -> SizeHint
                {
                    const std::vector<float>& w = weightsSig();
                    return Grow(i < static_cast<int>(w.size()) ? w[static_cast<std::size_t>(i)] : 1.f);
                };

                Locator childLoc = [loc, i](DockTree& t) -> DockNode*
                {
                    DockNode* p = loc(t);
                    return (p && p->IsSplit() && i < static_cast<int>(p->children.size()))
                               ? &p->children[static_cast<std::size_t>(i)]
                               : nullptr;
                };
                std::optional<std::vector<int>> childDrop;
                if (dropPath) { auto cp = *dropPath; cp.push_back(i); childDrop = std::move(cp); }

                Elem pane = Box({.layout = {.width  = horiz ? Reactive<SizeHint>(sizeFor) : Reactive<SizeHint>(Expand),
                                            .height = horiz ? Reactive<SizeHint>(Expand) : Reactive<SizeHint>(sizeFor),
                                            .cross  = Align::Stretch}},
                                ProjectNode(st, node.children[static_cast<std::size_t>(i)],
                                            std::move(childLoc), std::move(childDrop)));
                kids.push_back(std::move(pane));
            }

            return horiz
                       ? Row({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch}}, kids)
                       : Column({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch}}, kids);
        }

        Elem ProjectNode(const Ref<DockState>& st, const DockNode& node, Locator loc,
                         std::optional<std::vector<int>> dropPath)
        {
            return node.IsSplit() ? ProjectSplit(st, node, std::move(loc), std::move(dropPath))
                                  : ProjectTabs(st, node, std::move(loc), std::move(dropPath));
        }

        /// Strip that drags a floating window: it rides the cursor via `livePos` (no rebuild per move).
        /// Release over a guide square docks the subtree; elsewhere it relocates and raises.
        Elem FloatDragArea(const Ref<DockState>& st, int index, Signal<Vec2> livePos)
        {
            const DockTheme th = st->theme;
            auto gd = MakeRef<DragGesture>();
            return Painter({
                .width  = Expand,
                .height = Fixed(th.TabBarHeight),
                .onPaint = [](Paint2D&) {},
                .on = {
                    .onPress = [gd](PointerInfo pi)
                    {
                        gd->pressed = true;
                        gd->started = false;
                        gd->start = pi.position;
                    },
                    .onRelease = [st, gd, index](PointerInfo pi)
                    {
                        const Vec2 pos = pi.position;
                        const auto raise = [](DockTree& t, int i)
                        {
                            if (i >= 0 && i + 1 < static_cast<int>(t.floats.size()))
                                std::rotate(t.floats.begin() + i, t.floats.begin() + i + 1, t.floats.end());
                        };
                        if (gd->started)
                        {
                            const Vec2 grab = st->grabOffset;
                            auto drop = ResolveDrop(*st, pos, PanelId{});
                            st->model.Update([&](DockTree& t)
                            {
                                if (index < 0 || index >= static_cast<int>(t.floats.size())) return;
                                if (!drop || !drop->side)
                                {
                                    Rect f = t.floats[static_cast<std::size_t>(index)].frame;
                                    f.x = pos.x - grab.x;
                                    f.y = pos.y - grab.y;
                                    t.floats[static_cast<std::size_t>(index)].frame = ClampFloatFrame(*st, f);
                                    raise(t, index);
                                    return;
                                }
                                DockNode sub = std::move(t.floats[static_cast<std::size_t>(index)].root);
                                t.floats.erase(t.floats.begin() + index);
                                MoveNodeTo(t, std::move(sub), drop->anchor, *drop->side);
                            });
                            st->mode = DockState::Drag::None;
                            st->dragging = false;
                        }
                        else if (gd->pressed)
                        {
                            if (index + 1 < static_cast<int>(st->model.Peek().floats.size()))
                                st->model.Update([&](DockTree& t) { raise(t, index); });
                        }
                        gd->pressed = false;
                        gd->started = false;
                    },
                    .onMove = [st, gd, index, livePos](PointerInfo pi)
                    {
                        const Vec2 pos = pi.position;
                        if (gd->pressed && !gd->started && (pos - gd->start).Length() > 5.f)
                        {
                            gd->started    = true;
                            st->mode       = DockState::Drag::Float;
                            st->floatIndex = index;
                            const DockTree& t = st->model.Peek();
                            const Rect fr = index < static_cast<int>(t.floats.size())
                                                ? t.floats[static_cast<std::size_t>(index)].frame
                                                : Rect{};
                            st->grabOffset = {pos.x - fr.x, pos.y - fr.y};
                            st->dragging   = true;
                        }
                        if (gd->started)
                            livePos = Vec2{pos.x - st->grabOffset.x, pos.y - st->grabOffset.y};
                    },
                },
            });
        }

        /// The dragged frame after applying a resize delta to the grabbed edge(s), clamped.
        [[nodiscard]] Rect ResizedFrame(const DockState& st, Rect base, Vec2 d, bool right, bool bottom)
        {
            if (right)  base.width  += d.x;
            if (bottom) base.height += d.y;
            return ClampFloatFrame(st, base);
        }

        /// A resize handle: a thin absolute-inset grab strip on the right/bottom edge or corner (not a
        /// covering Spacer, which would block content). Commits the size on release.
        Elem ResizeHandle(const Ref<DockState>& st, int index, bool right, bool bottom)
        {
            const DockTheme th = st->theme;
            auto gd = MakeRef<DragGesture>();
            const bool corner = right && bottom;

            InteractProps on{
                .onPress = [st, gd, index](PointerInfo pi)
                {
                    const DockTree& t = st->model.Peek();
                    if (index < 0 || index >= static_cast<int>(t.floats.size())) return;
                    gd->pressed = true;
                    gd->started = false;
                    gd->start   = pi.position;
                    gd->rect    = t.floats[static_cast<std::size_t>(index)].frame;
                },
                .onRelease = [st, gd, index, right, bottom](PointerInfo pi)
                {
                    if (gd->started)
                    {
                        const Rect nf = ResizedFrame(*st, gd->rect, pi.position - gd->start, right, bottom);
                        st->mode = DockState::Drag::None;
                        st->dragging = false;
                        st->model.Update([&](DockTree& t)
                        {
                            if (index >= 0 && index < static_cast<int>(t.floats.size()))
                                t.floats[static_cast<std::size_t>(index)].frame = nf;
                        });
                    }
                    gd->pressed = false;
                    gd->started = false;
                },
                .onMove = [st, gd, right, bottom](PointerInfo pi)
                {
                    if (gd->pressed && !gd->started && (pi.position - gd->start).Length() > 2.f)
                    {
                        gd->started = true;
                        st->mode = DockState::Drag::Resize;
                        st->dragging = true;
                    }
                    if (gd->started)
                        st->resizePreview = ResizedFrame(*st, gd->rect, pi.position - gd->start, right, bottom);
                },
            };

            if (corner)
            {
                return Box({.layout   = {.width = Fixed(16.f), .height = Fixed(16.f)},
                            .position = {.position = Position::Absolute, .inset = {.right = 0.f, .bottom = 0.f}},
                            .on       = std::move(on)},
                           Painter({.width = Expand, .height = Expand,
                                    .onPaint = [th](Paint2D& p)
                                    {
                                        const Rect r = p.Bounds();
                                        p.DrawLine({r.Right() - 3.f, r.Bottom() - 9.f}, {r.Right() - 9.f, r.Bottom() - 3.f},
                                                   th.TextDim, 1.2f);
                                        p.DrawLine({r.Right() - 3.f, r.Bottom() - 5.f}, {r.Right() - 5.f, r.Bottom() - 3.f},
                                                   th.TextDim, 1.2f);
                                    }}));
            }
            if (right)
            {
                return Box({.layout   = {.width = Fixed(6.f)},
                            .position = {.position = Position::Absolute,
                                         .inset = {.top = 0.f, .right = 0.f, .bottom = 0.f}},
                            .on       = std::move(on)});
            }
            return Box({.layout   = {.height = Fixed(6.f)},
                        .position = {.position = Position::Absolute,
                                     .inset = {.left = 0.f, .right = 0.f, .bottom = 0.f}},
                        .on       = std::move(on)});
        }

        /// A float's inner tree - chrome strip + content. A Tabs root merges chrome and tab strip into
        /// one bar (the strip remainder drags the window); a Split root gets a plain strip.
        Elem FloatBody(const Ref<DockState>& st, const FloatWindow& f, int index, Signal<Vec2> livePos)
        {
            const DockTheme th = st->theme;

            Locator floc = [index](DockTree& t) -> DockNode*
            {
                return index < static_cast<int>(t.floats.size())
                           ? &t.floats[static_cast<std::size_t>(index)].root
                           : nullptr;
            };

            if (f.root.IsTabs())
                return ProjectTabs(st, f.root, floc, std::nullopt, FloatDragArea(st, index, livePos));

            Elem strip = Box({.layout = {.width = Expand, .height = Fixed(th.TabBarHeight), .cross = Align::Stretch},
                              .style = {.fill = th.TabBar}},
                             FloatDragArea(st, index, livePos));
            return Column({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch}},
                          std::move(strip),
                          Box({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch},
                               .view = {.clip = true}},
                              ProjectNode(st, f.root, floc, std::nullopt)));
        }

        Elem ProjectFloat(const Ref<DockState>& st, const FloatWindow& f, int index)
        {
            const DockTheme th    = st->theme;
            const Rect      frame = f.frame;

            // Seeded from the model frame each projection; the drag area writes it per move (no model edit).
            Signal<Vec2> livePos{Vec2{frame.x, frame.y}};

            return Box({.layout = {.width = Fixed(frame.width), .height = Fixed(frame.height),
                                   .cross = Align::Stretch},
                        .style = {.fill = th.FloatBg, .radius = 6.f, .border = {.width = 1.f, .color = th.Border}},
                        .view = {.clip = true},
                        .position = {.position = Position::Absolute,
                                     .insetLeft = [livePos] { return livePos().x; },
                                     .insetTop = [livePos] { return livePos().y; }}},
                       Stack({.layout = {.width = Expand, .height = Expand}},
                             FloatBody(st, f, index, livePos),
                             ResizeHandle(st, index, true, false),
                             ResizeHandle(st, index, false, true),
                             ResizeHandle(st, index, true, true)));
        }

        /// The transient drag overlay: while a tab/float drags it paints the guide cross of the hovered
        /// leaf plus the landing preview; while a float resizes it outlines the prospective frame.
        Elem DropOverlay(const Ref<DockState>& st)
        {
            const DockTheme th = st->theme;
            return Painter({
                .width  = Expand,
                .height = Expand,
                .onPaint = [st, th](Paint2D& p)
                {
                    const Vec2 m = st->externalPointer.value_or(p.Mouse());
                    if (st->mode == DockState::Drag::Resize)
                    {
                        p.StrokeRect(st->resizePreview, th.Accent, 1.5f, 6.f);
                        return;
                    }
                    if (auto drop = st->behavior.acceptsDrops ? ResolveDrop(*st, m, PanelId{}) : std::nullopt)
                    {
                        const Rect& r = drop->rect;

                        if (drop->side)
                        {
                            const Rect preview = ZonePreview(r, *drop->side);
                            p.FillRect(preview, th.DropZone, 4.f);
                            p.StrokeRect(preview, th.DropZoneHot, 1.f, 4.f);
                        }

                        const auto square = [&](DockSide s)
                        {
                            const Rect q   = GuideRect(r, s);
                            const bool hot = drop->side == s;
                            p.FillRect(q, hot ? th.DropZoneHot : th.GuideBg, 5.f);
                            p.StrokeRect(q, hot ? th.Accent : th.Border, 1.f, 5.f);

                            const Vec2  c{q.x + q.width * 0.5f, q.y + q.height * 0.5f};
                            const float a   = q.width * 0.16f;
                            const Color col = hot ? th.Text : th.TextDim;
                            switch (s)
                            {
                                case DockSide::Left:
                                    p.FillTriangle({c.x - a, c.y}, {c.x + a, c.y - a}, {c.x + a, c.y + a}, col);
                                    break;
                                case DockSide::Right:
                                    p.FillTriangle({c.x + a, c.y}, {c.x - a, c.y + a}, {c.x - a, c.y - a}, col);
                                    break;
                                case DockSide::Top:
                                    p.FillTriangle({c.x, c.y - a}, {c.x + a, c.y + a}, {c.x - a, c.y + a}, col);
                                    break;
                                case DockSide::Bottom:
                                    p.FillTriangle({c.x, c.y + a}, {c.x - a, c.y - a}, {c.x + a, c.y - a}, col);
                                    break;
                                case DockSide::Center:
                                {
                                    const Rect w{c.x - a, c.y - a, a * 2.f, a * 2.f};
                                    p.StrokeRect(w, col, 1.2f, 1.f);
                                    p.DrawLine({w.x, w.y + 3.f}, {w.x + w.width, w.y + 3.f}, col, 1.2f);
                                    break;
                                }
                            }
                        };
                        square(DockSide::Center);
                        square(DockSide::Left);
                        square(DockSide::Right);
                        square(DockSide::Top);
                        square(DockSide::Bottom);
                    }

                    if (st->mode == DockState::Drag::Tab)
                    {
                        std::string label = "Move";
                        if (const DockPanel* dp = st->registry.Find(st->tabPanel)) label = dp->title;
                        const float gw = std::clamp(20.f + static_cast<float>(label.size()) * 7.f, 52.f, 220.f);
                        p.FillRect({m.x + 12.f, m.y + 12.f, gw, 22.f}, th.DropZoneHot, 4.f);
                        p.DrawString(label, {m.x + 20.f, m.y + 16.f}, Color::White, 12.f);
                    }
                },
                .on = {.hitThrough = true},
                .repaint = [st] { return st->overlayRev(); },
            });
        }

        Elem ProjectRoot(const Ref<DockState>& st)
        {
            st->targets.clear();
            const DockTree& t = st->model.Peek();

            std::vector<Elem> layers;
            layers.reserve(t.floats.size() + 3);

            layers.push_back(Painter({.width = Expand, .height = Expand,
                                      .onPaint = [st](Paint2D& p) { st->workspace = p.Bounds(); }}));

            Locator rootLoc = [](DockTree& tr) -> DockNode* { return &tr.root; };
            layers.push_back(Box({.layout = {.width = Expand, .height = Expand, .cross = Align::Stretch}},
                                 ProjectNode(st, t.root, std::move(rootLoc), std::vector<int>{})));

            for (int i = 0; i < static_cast<int>(t.floats.size()); ++i)
                layers.push_back(ProjectFloat(st, t.floats[static_cast<std::size_t>(i)], i));

            // NOTE: the drag overlay is a sibling of the projection (see Dock()), not a layer here, so a
            // same-release model edit cannot free its Show node mid-reconcile.
            return Stack({.layout = {.width = Expand, .height = Expand}}, layers);
        }

        // Binary serialize / restore (magic "LDK1").

        void PutU8(std::vector<std::byte>& o, std::uint8_t v) { o.push_back(static_cast<std::byte>(v)); }
        void PutU32(std::vector<std::byte>& o, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i) o.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
        }
        void PutF32(std::vector<std::byte>& o, float v)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &v, 4);
            PutU32(o, bits);
        }
        void PutStr(std::vector<std::byte>& o, std::string_view s)
        {
            PutU32(o, static_cast<std::uint32_t>(s.size()));
            for (const char c : s) o.push_back(static_cast<std::byte>(c));
        }

        void WriteNode(std::vector<std::byte>& o, const DockRegistry& reg, const DockNode& n)
        {
            if (n.IsTabs())
            {
                PutU8(o, 0);
                PutU32(o, static_cast<std::uint32_t>(n.panels.size()));
                for (const PanelId p : n.panels) PutStr(o, reg.SourceOf(p));
                PutU32(o, static_cast<std::uint32_t>(std::max(0, n.active)));
            }
            else
            {
                PutU8(o, 1);
                PutU8(o, static_cast<std::uint8_t>(n.axis));
                PutU32(o, static_cast<std::uint32_t>(n.children.size()));
                for (std::size_t i = 0; i < n.children.size(); ++i)
                {
                    PutF32(o, i < n.weights.size() ? n.weights[i] : 1.f);
                    WriteNode(o, reg, n.children[i]);
                }
            }
        }

        struct Reader
        {
            std::span<const std::byte> d;
            std::size_t pos = 0;
            bool ok = true;

            [[nodiscard]] bool Need(std::size_t n) { if (pos + n > d.size()) ok = false; return ok; }
            std::uint8_t U8() { if (!Need(1)) return 0; return static_cast<std::uint8_t>(d[pos++]); }
            std::uint32_t U32()
            {
                if (!Need(4)) return 0;
                std::uint32_t v = 0;
                for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(d[pos++]) << (8 * i);
                return v;
            }
            float F32()
            {
                const std::uint32_t b = U32();
                float v;
                std::memcpy(&v, &b, 4);
                return v;
            }
            std::string Str()
            {
                const std::uint32_t n = U32();
                if (n == 0 || !Need(n)) return {};
                std::string s(reinterpret_cast<const char*>(&d[pos]), n);
                pos += n;
                return s;
            }
        };

        DockNode ReadNode(Reader& r, const DockRegistry& reg)
        {
            const std::uint8_t kind = r.U8();
            if (!r.ok) return DockNode::Tabs({});
            if (kind == 0)
            {
                const std::uint32_t count = r.U32();
                std::vector<PanelId> panels;
                for (std::uint32_t i = 0; i < count && r.ok; ++i)
                {
                    const std::string src = r.Str();
                    const PanelId id(src);
                    if (reg.Find(id)) panels.push_back(id);
                }
                const int active = static_cast<int>(r.U32());
                return DockNode::Tabs(std::move(panels), active);
            }
            const auto axis = static_cast<DockAxis>(r.U8());
            const std::uint32_t count = r.U32();
            std::vector<DockNode> kids;
            std::vector<float>    ws;
            for (std::uint32_t i = 0; i < count && r.ok; ++i)
            {
                ws.push_back(r.F32());
                kids.push_back(ReadNode(r, reg));
            }
            return DockNode::Split(axis, std::move(kids), std::move(ws));
        }


        DockNode BuildNode(DockRegistry& reg, const DockDecl& d)
        {
            if (!d.isSplit)
            {
                std::vector<PanelId> panels;
                panels.reserve(d.panels.size());
                for (const PanelDecl& p : d.panels)
                    panels.push_back(reg.Register(p.id, DockPanel{p.title, p.build, p.closable}));
                return DockNode::Tabs(std::move(panels));
            }
            std::vector<DockNode> kids;
            kids.reserve(d.children.size());
            for (const DockDecl& c : d.children) kids.push_back(BuildNode(reg, c));
            return DockNode::Split(d.axis, std::move(kids), d.weights);
        }

        [[nodiscard]] bool ModelIsEmpty(const DockTree& t)
        {
            return t.root.IsTabs() && t.root.panels.empty() && t.floats.empty();
        }

        void SeedIfEmpty(DockController& controller, const DockDecl& root, const DockCfg& cfg)
        {
            if (!ModelIsEmpty(controller.Model().Peek())) return;
            controller.Theme() = cfg.theme;
            DockTree t;
            t.root = BuildNode(controller.Registry(), root);
            controller.Model() = std::move(t);
        }

        /// The internally-owned-controller component for the zero-boilerplate declarative form.
        struct DockView
        {
            DockController controller;
            DockDecl       root;
            DockCfg        cfg;

            Elem Render()
            {
                SeedIfEmpty(controller, root, cfg);
                return Dock(controller);
            }
        };
    } // namespace


    PanelId DockRegistry::Register(std::string_view id, DockPanel panel)
    {
        const PanelId sid(id);
        m_sources[sid.Value()] = std::string(id);
        m_panels[sid.Value()]  = std::move(panel);
        return sid;
    }

    const DockPanel* DockRegistry::Find(PanelId id) const
    {
        const auto it = m_panels.find(id.Value());
        return it == m_panels.end() ? nullptr : &it->second;
    }

    std::string_view DockRegistry::SourceOf(PanelId id) const
    {
        const auto it = m_sources.find(id.Value());
        return it == m_sources.end() ? std::string_view{} : std::string_view{it->second};
    }


    DockController::DockController() : m_state(MakeRef<DockState>(DockTree{})) {}
    DockController::DockController(DockTree initial) : m_state(MakeRef<DockState>(std::move(initial))) {}
    DockController::~DockController() = default;
    DockController::DockController(const DockController&) = default;
    DockController::DockController(DockController&&) noexcept = default;
    DockController& DockController::operator=(const DockController&) = default;
    DockController& DockController::operator=(DockController&&) noexcept = default;

    Signal<DockTree>& DockController::Model() { return m_state->model; }
    DockRegistry&     DockController::Registry() { return m_state->registry; }
    DockTheme&        DockController::Theme() { return m_state->theme; }

    PanelId DockController::Panel(std::string_view id, std::string title, std::function<Elem()> build, bool closable)
    {
        return m_state->registry.Register(id, DockPanel{std::move(title), std::move(build), closable});
    }

    void DockController::DockTo(PanelId panel, std::vector<int> targetPath, DockSide side)
    {
        m_state->model.Update([&](DockTree& t)
        {
            const PanelId anchor = AnchorAt(t, targetPath, panel);
            MovePanelTo(t, panel, anchor, side);
        });
    }

    void DockController::Tabify(PanelId panel, std::vector<int> targetPath)
    {
        DockTo(panel, std::move(targetPath), DockSide::Center);
    }

    void DockController::Float(PanelId panel, Rect frame)
    {
        m_state->model.Update([&](DockTree& t)
        {
            RemovePanel(t, panel);
            NormalizeTree(t);
            t.floats.push_back({DockNode::Tabs({panel}), ClampFloatFrame(*m_state, frame)});
        });
    }

    void DockController::Close(PanelId panel)
    {
        m_state->model.Update([&](DockTree& t) { RemovePanel(t, panel); NormalizeTree(t); });
    }

    void DockController::SetActive(std::vector<int> tabsPath, int index)
    {
        m_state->model.Update([&](DockTree& t)
        {
            if (DockNode* leaf = NodeAt(t.root, tabsPath); leaf && leaf->IsTabs())
                leaf->active = std::clamp(index, 0, std::max(0, static_cast<int>(leaf->panels.size()) - 1));
        });
    }

    void DockController::Attach(DockNode subtree, DockSide side)
    {
        m_state->model.Update([&](DockTree& t) { MoveNodeTo(t, std::move(subtree), PanelId{}, side); });
    }

    std::vector<PanelId> CollectPanels(const DockTree& tree)
    {
        std::vector<PanelId> out;
        Collect(tree.root, out);
        for (const FloatWindow& f : tree.floats) Collect(f.root, out);
        return out;
    }

    void DockController::SetDetachHooks(DockDetachHooks hooks) { m_state->detach = std::move(hooks); }

    void DockController::SetBehavior(const DockBehavior behavior) { m_state->behavior = behavior; }

    const DockBehavior& DockController::Behavior() const { return m_state->behavior; }

    bool DockController::CanAcceptExternal(const Vec2 position) const
    {
        if (!m_state->behavior.acceptsDrops) return false;
        const auto drop = ResolveDrop(*m_state, position, PanelId{});
        return drop && drop->side;
    }

    bool DockController::AcceptExternal(const PanelId panel, const Vec2 position)
    {
        DockState& st = *m_state;
        if (!st.behavior.acceptsDrops) return false;
        const auto drop = ResolveDrop(st, position, panel);
        if (!drop || !drop->side) return false;
        PreviewExternal(panel, std::nullopt);
        st.model.Update([&](DockTree& t) { MovePanelTo(t, panel, drop->anchor, *drop->side); });
        return true;
    }

    void DockController::PreviewExternal(const PanelId panel, const std::optional<Vec2> position)
    {
        DockState& st = *m_state;
        if (position)
        {
            if (!st.behavior.acceptsDrops || (st.dragging.Peek() && !st.external)) return;
            if (st.external && st.externalPointer == position && st.tabPanel == panel) return;
            st.external        = true;
            st.externalPointer = position;
            st.tabPanel        = panel;
            st.mode            = DockState::Drag::Tab;
            st.dragging        = true;
        }
        else
        {
            if (!st.external) return;
            st.external = false;
            st.externalPointer.reset();
            st.mode     = DockState::Drag::None;
            st.dragging = false;
        }
        st.overlayRev.Update([](std::uint64_t& rev) { ++rev; });
    }

    std::vector<std::byte> DockController::Serialize() const
    {
        std::vector<std::byte> out;
        PutU8(out, 'L'); PutU8(out, 'D'); PutU8(out, 'K'); PutU8(out, '1');
        const DockTree& t = m_state->model.Peek();
        WriteNode(out, m_state->registry, t.root);
        PutU32(out, static_cast<std::uint32_t>(t.floats.size()));
        for (const FloatWindow& f : t.floats)
        {
            PutF32(out, f.frame.x); PutF32(out, f.frame.y);
            PutF32(out, f.frame.width); PutF32(out, f.frame.height);
            WriteNode(out, m_state->registry, f.root);
        }
        return out;
    }

    bool DockController::Restore(std::span<const std::byte> data)
    {
        Reader r{data};
        if (r.U8() != 'L' || r.U8() != 'D' || r.U8() != 'K' || r.U8() != '1') return false;

        DockTree t;
        t.root = ReadNode(r, m_state->registry);
        const std::uint32_t floatCount = r.U32();
        for (std::uint32_t i = 0; i < floatCount && r.ok; ++i)
        {
            FloatWindow f;
            f.frame.x = r.F32(); f.frame.y = r.F32();
            f.frame.width = r.F32(); f.frame.height = r.F32();
            f.root = ReadNode(r, m_state->registry);
            t.floats.push_back(std::move(f));
        }
        if (!r.ok) return false;

        NormalizeTree(t);
        m_state->model = std::move(t);
        return true;
    }


    Elem Dock(DockController& controller)
    {
        Ref<DockState> st = controller.m_state;
        detail::EachConfig cfg;
        cfg.snapshot = [st]() -> std::vector<detail::EachRow>
        {
            (void)st->model();                 // subscribe the controlling effect to model changes
            const std::uint64_t key = ++st->rev; // fresh key per snapshot
            return {detail::EachRow{key, [st]() -> Elem { return ProjectRoot(st); }}};
        };
        Elem projection = Elem(detail::BuildEach(std::move(cfg)));

        // The overlay is a sibling of the projection, not inside it: the Each rebuilds the projection
        // on every edit and a drop flips `dragging` in the same handler.
        Elem overlay = Show(Reactive<bool>(st->dragging), [st] { return DropOverlay(st); });

        return Stack({.layout = {.width = Expand, .height = Expand}}, std::move(projection), std::move(overlay));
    }

    Elem Dock(DockController& controller, DockDecl root, DockCfg cfg)
    {
        SeedIfEmpty(controller, root, cfg);
        return Dock(controller);
    }

    Elem Dock(DockCfg cfg, DockDecl root)
    {
        return Elem(DockView{DockController{}, std::move(root), std::move(cfg)});
    }
} // namespace lumora
