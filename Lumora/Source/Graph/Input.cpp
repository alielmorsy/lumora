#include "Graph/Input.h"
#include "Graph/Search.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <variant>

namespace lumora::nodegraph
{
    namespace
    {
        constexpr float kZoomPerNotch = 1.15f;
        constexpr Vec2  kPasteOffset{24.f, 24.f};

        /// How far past the outermost node the view may scroll, in canvas units.
        constexpr float kPanMargin = 300.f;

        /// Clamp one axis so the visible span's CENTRE stays inside [lo, hi]. Constraining the centre (not the span)
        /// needs no fits-on-screen branch and never snaps the view mid-zoom.
        [[nodiscard]] float ClampAxis(float pan, float lo, float hi, float extent) noexcept
        {
            const float half = extent * 0.5f;
            return std::clamp(pan, lo - half, hi - half);
        }

        /// Keep the view within the content's bounds plus kPanMargin; applied after both pan and zoom.
        void ClampPan(State& state)
        {
            if (state.model.NodeCount() == 0)
            {
                // An empty graph has no limit; clamping to the degenerate origin Bounds() would drag a fresh canvas to the origin.
                return;
            }

            state.EnsureLayout();
            const Rect content = state.layout.Bounds();
            const Rect visible = state.camera.VisibleCanvas(state.viewport);

            Vec2 pan = state.camera.Pan();
            pan.x = ClampAxis(pan.x, content.x - kPanMargin, content.Right() + kPanMargin,
                              visible.width);
            pan.y = ClampAxis(pan.y, content.y - kPanMargin, content.Bottom() + kPanMargin,
                              visible.height);
            state.camera.SetPan(pan);
        }

        [[nodiscard]] Rect Normalised(Vec2 a, Vec2 b) noexcept
        {
            const float x = std::min(a.x, b.x);
            const float y = std::min(a.y, b.y);
            return Rect{x, y, std::fabs(b.x - a.x), std::fabs(b.y - a.y)};
        }

        void EndPending(State& state)
        {
            state.tool = Tool::Idle;
            state.pendingFrom = PinRef{};
            state.pendingLegal = false;
            state.dragNodes.clear();
            state.selectionBeforeBox.clear();
            state.resizeNode = GraphNodeId{};
            state.resizeGroup = -1;
            state.resizeEdges = ResizeEdges{};
            state.followFrames.clear();
            state.activeFrame = FrameId{};
            state.captureCell = nullptr;
            state.captureNode = GraphNodeId{};
            // Disarm the space latch here too: InteractProps has no key-up hook, so OnKeyUp alone would latch forever.
            state.spaceHeld = false;
            state.Touch();
        }

        [[nodiscard]] CellInputCtx CellCtx(Controller& controller, State& state, CellEvent event,
                                           Vec2 layerPos, Vec2 layerDelta, KeyMods mods)
        {
            const NodeGeom* geom = state.layout.Of(state.captureNode);
            return CellInputCtx{.node = state.captureNode,
                                .canvasPos = state.ToCanvas(layerPos),
                                .pointer = PointerInfo{.position = layerPos,
                                                       .delta = layerDelta,
                                                       .button = 0,
                                                       .pressed = event != CellEvent::Release,
                                                       .modifiers = mods},
                                .rect = state.captureRect,
                                .event = event,
                                .zoom = state.camera.Zoom(),
                                .controller = &controller,
                                .surfacePos = layerPos - state.surfaceToLayer,
                                .compact = geom != nullptr && geom->compact};
        }

        /// The live custom cell under a Cell hit, armed into the capture fields; null when the hit is
        /// not one.
        const CustomCell* ArmCell(State& state, const Layout::Hit& hit)
        {
            if (hit.what != Layout::Hit::What::Cell)
            {
                return nullptr;
            }
            const NodeGeom* geom = state.layout.Of(hit.node);
            if (geom == nullptr || hit.cellIndex < 0
                || hit.cellIndex >= static_cast<int>(geom->cells.size()))
            {
                return nullptr;
            }
            const CellGeom& cellGeom = geom->cells[hit.cellIndex];
            const auto*     custom = std::get_if<CustomCell>(cellGeom.cell);
            if (custom == nullptr || !state.CellLive(hit.node, *custom))
            {
                return nullptr;
            }
            state.captureCell = custom;
            state.captureNode = hit.node;
            state.captureRect = cellGeom.rect;
            return custom;
        }

        void RebuildRows(State& state)
        {
            Popup& popup = state.popup;
            popup.rows.clear();
            if (popup.Cascading())
            {
                popup.path.assign(1, 0);
                popup.columnScroll.assign(1, 0);
                popup.highlight = -1;
                popup.scroll = 0;
                return;
            }
            if (popup.query.empty())
            {
                std::vector<std::string> categories;
                for (const PopupItem& item : popup.items)
                {
                    if (std::ranges::find(categories, item.category) == categories.end())
                    {
                        categories.push_back(item.category);
                    }
                }
                std::stable_partition(categories.begin(), categories.end(),
                                      [](const std::string& c) { return !c.empty(); });
                const bool headers = categories.size() > 1
                                  || (categories.size() == 1 && !categories.front().empty());
                for (const std::string& category : categories)
                {
                    if (headers)
                    {
                        popup.rows.push_back(
                            PopupRow{.item = -1, .header = category.empty() ? "Other" : category});
                    }
                    for (int i = 0; i < static_cast<int>(popup.items.size()); ++i)
                    {
                        if (popup.items[i].category == category)
                        {
                            popup.rows.push_back(PopupRow{.item = i});
                        }
                    }
                }
            }
            else
            {
                std::vector<std::pair<int, int>> scored;
                for (int i = 0; i < static_cast<int>(popup.items.size()); ++i)
                {
                    const PopupItem& item = popup.items[i];
                    if (const int score =
                            ScoreFields(popup.query, item.title, item.category, item.keywords);
                        score >= 0)
                    {
                        scored.emplace_back(score, i);
                    }
                }
                std::ranges::stable_sort(scored, std::greater{},
                                         [](const auto& entry) { return entry.first; });
                for (const auto& [score, index] : scored)
                {
                    popup.rows.push_back(PopupRow{.item = index});
                }
            }

            popup.scroll = 0;
            popup.highlight = -1;
            for (int i = 0; i < static_cast<int>(popup.rows.size()); ++i)
            {
                if (popup.rows[i].item >= 0)
                {
                    popup.highlight = i;
                    break;
                }
            }
        }

        void KeepHighlightVisible(Popup& popup)
        {
            if (popup.highlight < 0)
            {
                return;
            }
            // Pull a category header above the first item into view with it.
            const int top = popup.highlight > 0 && popup.rows[popup.highlight - 1].item < 0
                              ? popup.highlight - 1
                              : popup.highlight;
            if (top < popup.scroll)
            {
                popup.scroll = top;
            }
            if (popup.highlight >= popup.scroll + kPopupVisibleRows)
            {
                popup.scroll = popup.highlight - kPopupVisibleRows + 1;
            }
        }

        void MoveHighlight(Popup& popup, int steps)
        {
            const int direction = steps < 0 ? -1 : 1;
            for (int remaining = std::abs(steps); remaining > 0; --remaining)
            {
                int next = popup.highlight + direction;
                while (next >= 0 && next < static_cast<int>(popup.rows.size())
                       && popup.rows[next].item < 0)
                {
                    next += direction;
                }
                if (next < 0 || next >= static_cast<int>(popup.rows.size()))
                {
                    break;
                }
                popup.highlight = next;
            }
            KeepHighlightVisible(popup);
        }

        void Pick(Controller& controller, int item)
        {
            State&                         state = detail::Access(controller);
            const std::function<void(int)> pick = std::move(state.popup.onPick);
            controller.ClosePopup();
            if (item >= 0 && pick)
            {
                pick(item);
            }
        }

        void PickHighlighted(Controller& controller)
        {
            State& state = detail::Access(controller);
            Popup& popup = state.popup;
            if (popup.highlight < 0 || popup.highlight >= static_cast<int>(popup.rows.size()))
            {
                return;
            }
            Pick(controller, popup.rows[popup.highlight].item);
        }

        void ResetCascade(Popup& popup)
        {
            popup.path.assign(1, 0);
            popup.columnScroll.assign(1, 0);
        }

        [[nodiscard]] int ActiveEntry(const Popup& popup)
        {
            return popup.path.empty() ? -1 : popup.path.back();
        }

        void KeepEntryVisible(Popup& popup, int column)
        {
            const int entry = popup.path[column];
            int&      scroll = popup.columnScroll[column];
            if (entry < 0)
            {
                return;
            }
            scroll = std::clamp(scroll, entry - kCascadeRows + 1, entry);
        }

        /// Highlight `entry` in `column`, closing every column right of it, and open the sub-category
        /// column (with nothing highlighted) when `open` and the entry is a category.
        void FocusEntry(Popup& popup, int column, int entry, bool open)
        {
            popup.path.resize(static_cast<std::size_t>(column) + 1);
            popup.columnScroll.resize(static_cast<std::size_t>(column) + 1);
            popup.path[column] = entry;
            KeepEntryVisible(popup, column);
            const PaletteTree& node = CascadeNode(popup, column);
            if (open && entry >= 0 && entry < static_cast<int>(node.children.size()))
            {
                popup.path.push_back(-1);
                popup.columnScroll.push_back(0);
            }
        }

        void MoveCascade(Popup& popup, int steps)
        {
            const int column = static_cast<int>(popup.path.size()) - 1;
            const int count = CascadeEntries(CascadeNode(popup, column));
            if (count == 0)
            {
                return;
            }
            const int from = popup.path.back() < 0 ? (steps > 0 ? -1 : count) : popup.path.back();
            FocusEntry(popup, column, std::clamp(from + steps, 0, count - 1), false);
        }

        void ActivateCascade(Controller& controller)
        {
            Popup&             popup = detail::Access(controller).popup;
            const int          column = static_cast<int>(popup.path.size()) - 1;
            const int          entry = ActiveEntry(popup);
            const PaletteTree& node = CascadeNode(popup, column);
            const int          categories = static_cast<int>(node.children.size());
            if (entry < 0 || entry >= CascadeEntries(node))
            {
                return;
            }
            if (entry < categories)
            {
                FocusEntry(popup, column, entry, true);
                popup.path.back() = CascadeEntries(CascadeNode(popup, column + 1)) > 0 ? 0 : -1;
                return;
            }
            Pick(controller, node.items[entry - categories]);
        }

        void PopCodePoint(std::string& text)
        {
            while (!text.empty())
            {
                const auto byte = static_cast<unsigned char>(text.back());
                text.pop_back();
                if ((byte & 0xC0u) != 0x80u)
                {
                    break;
                }
            }
        }

        void EditFrameTitle(Controller& controller, FrameId id)
        {
            State&       state = detail::Access(controller);
            const Frame* frame = state.model.GetFrame(id);
            if (frame == nullptr)
            {
                return;
            }
            const Rect header{frame->rect.x, frame->rect.y, frame->rect.width,
                              state.theme.FrameHeaderHeight};
            controller.EditText(header, frame->title, [&controller, id](std::string_view title)
                                { controller.SetFrameTitle(id, title); });
        }

        [[nodiscard]] std::vector<ContextMenuItem> AddMenu(Controller& controller, const PaletteTree& node,
                                                           const std::vector<PopupItem>& items, Vec2 canvas)
        {
            std::vector<ContextMenuItem> entries;
            for (const PaletteTree& child : node.children)
            {
                ContextMenuItem sub;
                sub.label = child.name;
                sub.submenu = AddMenu(controller, child, items, canvas);
                entries.push_back(std::move(sub));
            }
            for (const int index : node.items)
            {
                ContextMenuItem entry;
                entry.label = items[index].title;
                entry.onClick = [&controller, type = items[index].type, canvas] { controller.AddNode(type, canvas); };
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        [[nodiscard]] std::string_view Trim(std::string_view text) noexcept
        {
            const auto first = text.find_first_not_of(" \t");
            if (first == std::string_view::npos)
            {
                return {};
            }
            return text.substr(first, text.find_last_not_of(" \t") - first + 1);
        }
    } // namespace

    PaletteTree BuildCategoryTree(std::span<const PopupItem> items)
    {
        PaletteTree root;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            PaletteTree*     node = &root;
            std::string_view rest = items[i].category;
            while (!rest.empty())
            {
                const std::size_t      cut = rest.find('/');
                const std::string_view part = Trim(rest.substr(0, cut));
                rest = cut == std::string_view::npos ? std::string_view{} : rest.substr(cut + 1);
                if (part.empty())
                {
                    continue;
                }
                auto it = std::ranges::find(node->children, part, &PaletteTree::name);
                if (it == node->children.end())
                {
                    node->children.push_back(PaletteTree{.name = std::string(part)});
                    it = std::prev(node->children.end());
                }
                node = &*it;
            }
            node->items.push_back(i);
        }
        return root;
    }

    const PaletteTree& CascadeNode(const Popup& popup, int column)
    {
        const PaletteTree* node = &popup.tree;
        for (int k = 0; k < column && k < static_cast<int>(popup.path.size()); ++k)
        {
            const int entry = popup.path[k];
            if (entry < 0 || entry >= static_cast<int>(node->children.size()))
            {
                break;
            }
            node = &node->children[entry];
        }
        return *node;
    }

    int CascadeEntries(const PaletteTree& node)
    {
        return static_cast<int>(node.children.size() + node.items.size());
    }

    Rect CascadeColumnRect(const State& state, int column)
    {
        const Popup& popup = state.popup;
        const Rect&  vp = state.viewport;
        const int    columns = std::max(static_cast<int>(popup.path.size()), 1);

        float tallest = 0.f;
        for (int k = 0; k < columns; ++k)
        {
            const int rows = std::clamp(CascadeEntries(CascadeNode(popup, k)), 1, kCascadeRows);
            tallest = std::max(tallest, kPopupRowHeight * static_cast<float>(rows + (k == 0 ? 1 : 0)) + 4.f);
        }
        const float x0 = std::max(std::min(popup.layerPos.x, vp.Right() - kCascadeWidth * static_cast<float>(columns)), vp.x);
        const float y0 = std::max(std::min(popup.layerPos.y, vp.Bottom() - tallest - kPopupRowHeight), vp.y);

        const int   rows = std::clamp(CascadeEntries(CascadeNode(popup, column)), 1, kCascadeRows);
        const float x = x0 + kCascadeWidth * static_cast<float>(column);
        if (column == 0)
        {
            return Rect{x, y0, kCascadeWidth, kPopupRowHeight * static_cast<float>(rows + 1) + 4.f};
        }
        return Rect{x, y0 + kPopupRowHeight, kCascadeWidth, kPopupRowHeight * static_cast<float>(rows) + 4.f};
    }

    CascadeHit CascadeAt(const State& state, Vec2 layerPos)
    {
        const Popup& popup = state.popup;
        for (int column = static_cast<int>(popup.path.size()); column-- > 0;)
        {
            const Rect rect = CascadeColumnRect(state, column);
            if (!rect.Contains(layerPos))
            {
                continue;
            }
            const float top = rect.y + 2.f + (column == 0 ? kPopupRowHeight : 0.f);
            const int   line = layerPos.y < top ? -1 : static_cast<int>((layerPos.y - top) / kPopupRowHeight);
            const int   entry = line < 0 || line >= kCascadeRows ? -1 : popup.columnScroll[column] + line;
            return CascadeHit{.column = column,
                              .entry = entry < CascadeEntries(CascadeNode(popup, column)) ? entry : -1};
        }
        return {};
    }

    void CommitText(State& state)
    {
        if (!state.textEdit.active)
        {
            return;
        }
        TextEdit edit = std::move(state.textEdit);
        state.textEdit = TextEdit{};
        state.editOpen = false;
        state.Touch();
        if (edit.onCommit)
        {
            edit.onCommit(state.editValue.Peek());
        }
    }

    void CancelText(State& state)
    {
        if (!state.textEdit.active)
        {
            return;
        }
        TextEdit edit = std::move(state.textEdit);
        state.textEdit = TextEdit{};
        state.editOpen = false;
        state.Touch();
        if (edit.onCancel)
        {
            edit.onCancel();
        }
    }

    Rect PopupRect(const State& state)
    {
        const Popup& popup = state.popup;
        const int    rows = std::clamp(static_cast<int>(popup.rows.size()), 1, kPopupVisibleRows);
        const float  height = kPopupRowHeight * static_cast<float>(rows + 1) + 4.f;
        const Rect&  vp = state.viewport;
        const float  x = std::max(std::min(popup.layerPos.x, vp.Right() - kPopupWidth), vp.x);
        const float  y = std::max(std::min(popup.layerPos.y, vp.Bottom() - height), vp.y);
        return Rect{x, y, kPopupWidth, height};
    }

    int PopupRowAt(const State& state, Vec2 layerPos)
    {
        const Rect rect = PopupRect(state);
        if (!rect.Contains(layerPos))
        {
            return -1;
        }
        const int line = static_cast<int>((layerPos.y - rect.y) / kPopupRowHeight) - 1;
        const int row = state.popup.scroll + line;
        if (line < 0 || line >= kPopupVisibleRows || row >= static_cast<int>(state.popup.rows.size()))
        {
            return -1;
        }
        return row;
    }

    FrameHit ProbeFrame(const State& state, Vec2 canvasPos)
    {
        const float zoom = state.camera.Zoom() > 0.f ? state.camera.Zoom() : 1.f;
        const float grab = 10.f / zoom;
        const std::span<const Frame> frames = state.model.Frames();
        for (auto it = frames.rbegin(); it != frames.rend(); ++it)
        {
            const Rect& r = it->rect;
            if (std::fabs(canvasPos.x - r.Right()) <= grab && std::fabs(canvasPos.y - r.Bottom()) <= grab)
            {
                return FrameHit{.part = FramePart::Corner, .frame = it->id};
            }
            if (Rect{r.x, r.y, r.width, state.theme.FrameHeaderHeight}.Contains(canvasPos))
            {
                return FrameHit{.part = FramePart::Header, .frame = it->id};
            }
        }
        return {};
    }

    void OpenPaletteFor(Controller& controller, Vec2 canvasPos, PinRef from)
    {
        State&     state = detail::Access(controller);
        const auto all = state.registry.All();
        std::vector<NodeTypeId> types =
            state.cfg.paletteFilter ? state.cfg.paletteFilter()
                                    : std::vector<NodeTypeId>(all.begin(), all.end());
        if (from.node.Valid())
        {
            types = CompatibleTypes(state.model, state.registry, state.policy, from, types);
        }
        if (state.cfg.paletteAccepts)
        {
            std::erase_if(types, [&state, from](NodeTypeId type)
                          { return !state.cfg.paletteAccepts(type, from); });
        }

        Popup popup;
        for (const NodeTypeId type : types)
        {
            if (const NodeTypeDesc* desc = state.registry.Find(type))
            {
                popup.items.push_back(PopupItem{.title = desc->title,
                                                .category = desc->category,
                                                .keywords = desc->keywords,
                                                .type = type});
            }
        }
        popup.open = true;
        popup.palette = true;
        popup.tree = BuildCategoryTree(popup.items);
        ResetCascade(popup);
        popup.canvasPos = canvasPos;
        popup.layerPos = state.camera.ToLayer(canvasPos, state.viewport);
        popup.from = from;
        std::vector<NodeTypeId> picked;
        for (const PopupItem& item : popup.items)
        {
            picked.push_back(item.type);
        }
        popup.onPick = [&controller, picked = std::move(picked), canvasPos, from](int item)
        {
            State& s = detail::Access(controller);
            if (item < 0 || item >= static_cast<int>(picked.size()))
            {
                return;
            }
            const NodeTypeId type = picked[item];
            const std::uint16_t slot =
                from.node.Valid() ? FirstCompatiblePin(s.model, s.registry, s.policy, from, type)
                                  : NoPin;
            const bool owned = !s.history.Open();
            if (owned)
            {
                controller.Begin("Add node");
            }
            const GraphNodeId added = controller.AddNode(type, canvasPos);
            if (slot != NoPin)
            {
                from.dir == PinDir::Output
                    ? controller.Connect(from, PinRef{added, slot, PinDir::Input})
                    : controller.Connect(PinRef{added, slot, PinDir::Output}, from);
            }
            if (owned)
            {
                controller.Commit();
            }
            const GraphNodeId one[1] = {added};
            controller.Select(one);
        };

        CommitText(state);
        state.popup = std::move(popup);
        RebuildRows(state);
        state.Touch();
    }

    void Controller::OpenPalette(Vec2 canvasPos) { OpenPaletteFor(*this, canvasPos, PinRef{}); }

    void Controller::ClosePopup()
    {
        State& state = *m_state;
        state.popup = Popup{};
        state.Touch();
    }

    bool Controller::PopupOpen() const { return m_state->popup.open; }

    void Controller::EditText(const Rect& canvasRect, std::string initial,
                              std::function<void(std::string_view)> onCommit,
                              std::function<void()> onCancel)
    {
        State& state = *m_state;
        CommitText(state);
        state.popup = Popup{};
        state.textEdit = TextEdit{.active = true,
                                  .canvasRect = canvasRect,
                                  .onCommit = std::move(onCommit),
                                  .onCancel = std::move(onCancel)};
        state.editValue = std::move(initial);
        state.editRect = CanvasToSurface(canvasRect);
        state.editZoom = state.camera.Zoom();
        state.editOpen = true;
        state.Touch();
    }

    bool Controller::EditingText() const { return m_state->textEdit.active; }

    void Controller::OpenChoices(const Rect& canvasAnchor, std::vector<std::string> options,
                                 std::function<void(int)> onPick)
    {
        State& state = *m_state;
        Popup  popup;
        for (std::string& option : options)
        {
            popup.items.push_back(PopupItem{.title = std::move(option)});
        }
        popup.open = true;
        popup.canvasPos = Vec2{canvasAnchor.x, canvasAnchor.Bottom()};
        popup.layerPos = state.camera.ToLayer(popup.canvasPos, state.viewport);
        popup.onPick = std::move(onPick);
        CommitText(state);
        state.popup = std::move(popup);
        RebuildRows(state);
        state.Touch();
    }

    void OnPress(Controller& controller, Vec2 layerPos, KeyMods mods, bool middleButton)
    {
        State& state = detail::Access(controller);
        state.EnsureLayout();
        state.lastPointerLayer = layerPos;

        const Vec2 canvas = state.ToCanvas(layerPos);
        state.dragOriginCanvas = canvas;

        if (state.textEdit.active)
        {
            CommitText(state);
            return;
        }
        if (state.popup.open && state.popup.Cascading())
        {
            const CascadeHit at = CascadeAt(state, layerPos);
            if (at.column < 0)
            {
                controller.ClosePopup();
                return;
            }
            if (at.entry < 0)
            {
                return;
            }
            const PaletteTree& node = CascadeNode(state.popup, at.column);
            const int          categories = static_cast<int>(node.children.size());
            if (at.entry < categories)
            {
                FocusEntry(state.popup, at.column, at.entry, true);
                state.Touch();
                return;
            }
            Pick(controller, node.items[at.entry - categories]);
            return;
        }
        if (state.popup.open)
        {
            const int row = PopupRowAt(state, layerPos);
            if (row >= 0 && state.popup.rows[row].item >= 0)
            {
                state.popup.highlight = row;
                PickHighlighted(controller);
            }
            else if (!PopupRect(state).Contains(layerPos))
            {
                controller.ClosePopup();
            }
            return;
        }

        if (middleButton || Has(mods, KeyMods::Alt) || state.spaceHeld)
        {
            state.tool = Tool::PanCamera;
            return;
        }

        const Layout::Hit hit =
            state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom());

        switch (hit.what)
        {
        case Layout::Hit::What::Pin:
        {
            if (hit.pinDir == PinDir::Input)
            {
                // Re-drag the free end of whatever occupies this input.
                const LinkId occupant =
                    state.model.LinkOnInput(PinRef{hit.node, hit.pinSlot, PinDir::Input});
                if (occupant.Valid())
                {
                    const PinRef source = state.model.Get(occupant)->from;
                    controller.Begin("Reconnect");
                    controller.Disconnect(occupant);
                    state.pendingFrom = source;
                    state.pendingToCanvas = canvas;
                    state.pendingLegal = false;
                    state.tool = Tool::DragLink;
                    state.Touch();
                    return;
                }
                EndPending(state);
                return;
            }
            state.pendingFrom = PinRef{hit.node, hit.pinSlot, hit.pinDir};
            state.pendingToCanvas = canvas;
            state.pendingLegal = false;
            state.tool = Tool::DragLink;
            state.Touch();
            return;
        }
        case Layout::Hit::What::GroupCaret:
        {
            const Node* node = state.model.Get(hit.node);
            const bool  open = hit.groupIndex < 32
                           && (node->openGroups & (1u << hit.groupIndex)) != 0u;
            controller.SetGroupOpen(hit.node, hit.groupIndex, !open);
            state.tool = Tool::Idle;
            return;
        }
        case Layout::Hit::What::GroupResize:
        {
            state.tool = Tool::ResizeGroup;
            state.resizeNode = hit.node;
            state.resizeGroup = hit.groupIndex;
            const NodeGeom* geom = state.layout.Of(hit.node);
            state.resizeOrigin =
                hit.cellIndex >= 0 && geom != nullptr
                    ? geom->rect.Bottom() - geom->cells[hit.cellIndex].rect.Bottom()
                    : 0.f;
            controller.Begin("Resize group");
            return;
        }
        case Layout::Hit::What::NodeResize:
        {
            state.tool = Tool::ResizeNode;
            state.resizeNode = hit.node;
            state.resizeEdges = hit.resize;
            const NodeGeom* geom = state.layout.Of(hit.node);
            state.resizeStart = geom != nullptr ? Vec2{geom->rect.width, geom->rect.height} : Vec2{};
            const GraphNodeId one[1] = {hit.node};
            CaptureFrames(state, one);
            controller.Begin("Resize node");
            return;
        }
        case Layout::Hit::What::Header:
        case Layout::Hit::What::Cell:
        case Layout::Hit::What::Body:
        {
            if (const CustomCell* cell = ArmCell(state, hit);
                cell != nullptr && !Has(mods, KeyMods::Ctrl))
            {
                CellInputCtx ctx = CellCtx(controller, state, CellEvent::Press, layerPos, Vec2{}, mods);
                if (cell->input(ctx))
                {
                    if (!controller.IsSelected(hit.node))
                    {
                        const GraphNodeId one[1] = {hit.node};
                        controller.Select(one);
                    }
                    state.tool = Tool::CellCapture;
                    return;
                }
                state.captureCell = nullptr;
            }
            if (Has(mods, KeyMods::Ctrl))
            {
                controller.ToggleSelected(hit.node);
                state.tool = Tool::Idle;
                return;
            }
            if (!controller.IsSelected(hit.node))
            {
                const GraphNodeId one[1] = {hit.node};
                controller.Select(one, Has(mods, KeyMods::Shift));
            }
            state.dragNodes.assign(controller.Selection().begin(), controller.Selection().end());
            CaptureFrames(state, state.dragNodes);
            state.dragCarry = Vec2{};
            state.tool = Tool::DragNodes;
            controller.Begin("Move");
            return;
        }
        case Layout::Hit::What::Link:
            controller.SelectLink(hit.link);
            state.tool = Tool::Idle;
            return;
        case Layout::Hit::What::None:
        default:
            if (const FrameHit frameHit = ProbeFrame(state, canvas); frameHit.part != FramePart::None)
            {
                state.activeFrame = frameHit.frame;
                state.frameOrigin = state.model.GetFrame(frameHit.frame)->rect;
                if (frameHit.part == FramePart::Corner)
                {
                    state.tool = Tool::ResizeFrame;
                    controller.Begin("Resize frame");
                }
                else
                {
                    state.tool = Tool::DragFrame;
                    controller.Begin("Move frame");
                }
                return;
            }
            // Captured before the gesture clears anything so Escape can restore it; do not clear the capture here.
            state.selectionBeforeBox.assign(controller.Selection().begin(),
                                            controller.Selection().end());
            if (!Has(mods, KeyMods::Shift))
            {
                controller.ClearSelection();
            }
            state.boxSelect = Rect{canvas.x, canvas.y, 0.f, 0.f};
            state.tool = Tool::BoxSelect;
            return;
        }
    }

    void OnMove(Controller& controller, Vec2 layerPos, Vec2 layerDelta, KeyMods mods)
    {
        State& state = detail::Access(controller);
        state.lastPointerLayer = layerPos;
        const Vec2 canvas = state.ToCanvas(layerPos);

        if (state.popup.open && state.tool == Tool::Idle && state.popup.Cascading())
        {
            const CascadeHit at = CascadeAt(state, layerPos);
            if (at.column < 0 || at.entry < 0)
            {
                return;
            }
            Popup&             popup = state.popup;
            const PaletteTree& node = CascadeNode(popup, at.column);
            const bool         category = at.entry < static_cast<int>(node.children.size());
            const bool         same = popup.path[at.column] == at.entry
                               && static_cast<int>(popup.path.size()) == at.column + (category ? 2 : 1);
            if (!same)
            {
                FocusEntry(popup, at.column, at.entry, true);
                state.Touch();
            }
            return;
        }
        if (state.popup.open && state.tool == Tool::Idle)
        {
            const int row = PopupRowAt(state, layerPos);
            if (row >= 0 && state.popup.rows[row].item >= 0 && row != state.popup.highlight)
            {
                state.popup.highlight = row;
                state.Touch();
            }
            return;
        }

        switch (state.tool)
        {
        case Tool::DragFrame:
            controller.MoveFrame(state.activeFrame, layerDelta / state.camera.Zoom());
            break;
        case Tool::ResizeFrame:
        {
            const Rect& origin = state.frameOrigin;
            const float minHeight = state.theme.FrameHeaderHeight + 24.f;
            controller.SetFrameRect(
                state.activeFrame,
                Rect{origin.x, origin.y,
                     std::max(origin.width + canvas.x - state.dragOriginCanvas.x, 80.f),
                     std::max(origin.height + canvas.y - state.dragOriginCanvas.y, minHeight)});
            break;
        }
        case Tool::CellCapture:
            if (state.captureCell != nullptr)
            {
                CellInputCtx ctx =
                    CellCtx(controller, state, CellEvent::Drag, layerPos, layerDelta, mods);
                state.captureCell->input(ctx);
            }
            break;
        case Tool::PanCamera:
            state.camera.PanBy(layerDelta);
            ClampPan(state);
            state.Touch();
            break;
        case Tool::DragNodes:
            // By reported delta, not (canvas - last), like PanCamera: avoids double-counting when a caller derives its
            // position from geometry it just moved.
        {
            state.dragCarry += layerDelta / state.camera.Zoom();
            Vec2 step = state.dragCarry;
            if (const float grid = state.theme.SnapSize; grid > 0.f)
                step = Vec2{std::trunc(step.x / grid) * grid, std::trunc(step.y / grid) * grid};
            if (step.x != 0.f || step.y != 0.f)
            {
                controller.MoveNodes(state.dragNodes, step);
                state.dragCarry -= step;
            }
            FollowFrames(state, Has(mods, KeyMods::Alt));
            break;
        }
        case Tool::DragLink:
        {
            state.pendingToCanvas = canvas;
            state.EnsureLayout();
            const Layout::Hit hit =
                state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom());
            state.pendingLegal =
                hit.what == Layout::Hit::What::Pin
                && state.model.CanConnect(state.registry, state.policy, state.pendingFrom,
                                          PinRef{hit.node, hit.pinSlot, hit.pinDir});
            state.Touch();
            break;
        }
        case Tool::BoxSelect:
            state.boxSelect = Normalised(state.dragOriginCanvas, canvas);
            state.Touch();
            break;
        case Tool::ResizeNode:
        {
            const Node* node = state.model.Get(state.resizeNode);
            if (node == nullptr)
            {
                break;
            }
            const Vec2 drag = canvas - state.dragOriginCanvas;
            if (state.resizeEdges.height)
            {
                controller.SetNodeSize(state.resizeNode,
                                       Vec2{state.resizeEdges.width ? state.resizeStart.x + drag.x : node->width,
                                            state.resizeStart.y + drag.y});
            }
            else
            {
                controller.SetNodeWidth(state.resizeNode, state.resizeStart.x + drag.x);
            }
            FollowFrames(state, false);
            break;
        }
        case Tool::ResizeGroup:
        {
            const NodeGeom* geom = state.layout.Of(state.resizeNode);
            if (geom == nullptr)
            {
                break;
            }
            const float target = canvas.y - state.dragOriginCanvas.y;
            const Node* node = state.model.Get(state.resizeNode);
            const float current =
                state.resizeGroup >= 0 && state.resizeGroup < 4 && node != nullptr
                    ? node->groupHeight[state.resizeGroup]
                    : 0.f;
            controller.SetGroupHeight(state.resizeNode, state.resizeGroup,
                                      std::max(current + target, 8.f));
            state.dragOriginCanvas = canvas;
            break;
        }
        case Tool::Idle:
        default:
            break;
        }
    }

    void OnRelease(Controller& controller, Vec2 layerPos, KeyMods mods)
    {
        State& state = detail::Access(controller);
        state.EnsureLayout();
        const Vec2 canvas = state.ToCanvas(layerPos);

        switch (state.tool)
        {
        case Tool::DragLink:
        {
            const Layout::Hit hit =
                state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom());
            if (hit.what == Layout::Hit::What::Pin)
            {
                controller.Connect(state.pendingFrom, PinRef{hit.node, hit.pinSlot, hit.pinDir});
            }
            // A Reconnect transaction is open when the drag started on a connected input; commit it
            // either way so the detach is undoable as one step.
            controller.Commit();
            if (hit.what == Layout::Hit::What::None && state.cfg.searchPalette
                && state.pendingFrom.node.Valid())
            {
                const PinRef from = state.pendingFrom;
                EndPending(state);
                OpenPaletteFor(controller, canvas, from);
                return;
            }
            break;
        }
        case Tool::DragFrame:
            controller.Commit();
            break;
        case Tool::ResizeFrame:
            controller.RefreshFrameMembers(state.activeFrame);
            controller.Commit();
            break;
        case Tool::CellCapture:
            if (state.captureCell != nullptr)
            {
                CellInputCtx ctx =
                    CellCtx(controller, state, CellEvent::Release, layerPos, Vec2{}, mods);
                state.captureCell->input(ctx);
            }
            break;
        case Tool::BoxSelect:
        {
            std::vector<GraphNodeId> caught;
            for (std::uint32_t slot = 0; slot < state.model.SlotCount(); ++slot)
            {
                const GraphNodeId id = state.model.At(slot);
                const NodeGeom*   geom = state.layout.Of(id);
                if (geom != nullptr && geom->rect.Intersects(state.boxSelect))
                {
                    caught.push_back(id);
                }
            }
            controller.Select(caught, Has(mods, KeyMods::Shift));
            state.boxSelect = Rect{};
            break;
        }
        case Tool::DragNodes:
            FollowFrames(state, Has(mods, KeyMods::Alt));
            SettleFrames(state, state.dragNodes, Has(mods, KeyMods::Alt));
            controller.Commit();
            break;
        case Tool::ResizeNode:
        {
            const GraphNodeId resized = state.resizeNode;
            const NodeGeom* geom = state.layout.Of(resized);
            const bool      changed = geom != nullptr
                                && (geom->rect.width != state.resizeStart.x || geom->rect.height != state.resizeStart.y);
            controller.Commit();
            if (changed && controller.onNodeResized)
            {
                controller.onNodeResized(resized);
            }
            break;
        }
        case Tool::ResizeGroup:
            controller.Commit();
            break;
        case Tool::PanCamera:
        case Tool::Idle:
        default:
            break;
        }

        EndPending(state);
    }

    void OnScroll(Controller& controller, float notches)
    {
        State& state = detail::Access(controller);
        if (notches == 0.f)
        {
            return;
        }
        CommitText(state);
        if (state.popup.open && state.popup.Cascading())
        {
            Popup&          popup = state.popup;
            const CascadeHit at = CascadeAt(state, state.lastPointerLayer);
            const int       column = at.column >= 0 ? at.column : static_cast<int>(popup.path.size()) - 1;
            const int       last = std::max(CascadeEntries(CascadeNode(popup, column)) - kCascadeRows, 0);
            popup.columnScroll[column] = std::clamp(popup.columnScroll[column] + (notches > 0.f ? -1 : 1), 0, last);
            state.Touch();
            return;
        }
        if (state.popup.open)
        {
            const int last =
                std::max(static_cast<int>(state.popup.rows.size()) - kPopupVisibleRows, 0);
            state.popup.scroll = std::clamp(state.popup.scroll + (notches > 0.f ? -1 : 1), 0, last);
            state.Touch();
            return;
        }
        const float factor = std::pow(kZoomPerNotch, notches);
        state.camera.ZoomAt(state.lastPointerLayer, factor, state.viewport);
        ClampPan(state);
        state.Touch();
    }

    bool OnKey(Controller& controller, KeyInfo key)
    {
        State&     state = detail::Access(controller);
        const bool ctrl = Has(key.modifiers, KeyMods::Ctrl);

        if (state.popup.open && state.popup.Cascading())
        {
            Popup& popup = state.popup;
            switch (key.key)
            {
            case Key::Up:
                MoveCascade(popup, -1);
                break;
            case Key::Down:
                MoveCascade(popup, 1);
                break;
            case Key::PageUp:
                MoveCascade(popup, -kCascadeRows);
                break;
            case Key::PageDown:
                MoveCascade(popup, kCascadeRows);
                break;
            case Key::Right:
            {
                const int entry = ActiveEntry(popup);
                if (entry >= 0 && entry < static_cast<int>(CascadeNode(popup, static_cast<int>(popup.path.size()) - 1).children.size()))
                {
                    ActivateCascade(controller);
                }
                break;
            }
            case Key::Left:
                if (popup.path.size() > 1)
                {
                    popup.path.pop_back();
                    popup.columnScroll.pop_back();
                }
                break;
            case Key::Enter:
            case Key::NumEnter:
                ActivateCascade(controller);
                return true;
            case Key::Escape:
                controller.ClosePopup();
                return true;
            default:
                break;
            }
            state.Touch();
            return true;
        }

        if (state.popup.open)
        {
            switch (key.key)
            {
            case Key::Up:
                MoveHighlight(state.popup, -1);
                break;
            case Key::Down:
                MoveHighlight(state.popup, 1);
                break;
            case Key::PageUp:
                MoveHighlight(state.popup, -kPopupVisibleRows);
                break;
            case Key::PageDown:
                MoveHighlight(state.popup, kPopupVisibleRows);
                break;
            case Key::Enter:
            case Key::NumEnter:
                PickHighlighted(controller);
                return true;
            case Key::Escape:
                controller.ClosePopup();
                return true;
            case Key::Backspace:
                PopCodePoint(state.popup.query);
                RebuildRows(state);
                break;
            default:
                break;
            }
            state.Touch();
            return true;
        }

        if (state.cfg.searchPalette && state.tool == Tool::Idle && !ctrl
            && (key.key == Key::Tab || key.key == Key::Space))
        {
            const Vec2 anchor = state.viewport.Contains(state.lastPointerLayer)
                                  ? state.lastPointerLayer
                                  : state.viewport.Center();
            OpenPaletteFor(controller, state.ToCanvas(anchor), PinRef{});
            return true;
        }

        switch (key.key)
        {
        case Key::Space:
            state.spaceHeld = true;
            return true;
        case Key::Delete:
            controller.RemoveSelection();
            return true;
        case Key::Home:
            controller.FitAll();
            return true;
        case Key::End:
            controller.FitSelection();
            return true;
        case Key::Escape:
            if (state.tool == Tool::BoxSelect)
            {
                // Put back whatever was selected before the marquee began. Select() must run before
                // EndPending, which clears the capture.
                controller.Select(state.selectionBeforeBox);
                EndPending(state);
                return true;
            }
            if (state.tool == Tool::CellCapture && state.captureCell != nullptr)
            {
                CellInputCtx ctx = CellCtx(controller, state, CellEvent::Release,
                                           state.lastPointerLayer, Vec2{}, key.modifiers);
                state.captureCell->input(ctx);
                EndPending(state);
                return true;
            }
            if (state.tool == Tool::DragLink || state.tool == Tool::DragNodes
                || state.tool == Tool::ResizeNode || state.tool == Tool::ResizeGroup
                || state.tool == Tool::DragFrame || state.tool == Tool::ResizeFrame)
            {
                controller.Abort();
                EndPending(state);
                return true;
            }
            controller.ClearSelection();
            return true;
        case Key::A:
            if (!ctrl)
            {
                return false;
            }
            {
                std::vector<GraphNodeId> all;
                for (std::uint32_t slot = 0; slot < state.model.SlotCount(); ++slot)
                {
                    const GraphNodeId id = state.model.At(slot);
                    if (id.Valid())
                    {
                        all.push_back(id);
                    }
                }
                controller.Select(all);
            }
            return true;
        case Key::Z:
            if (!ctrl)
            {
                return false;
            }
            controller.Undo();
            return true;
        case Key::Y:
            if (!ctrl)
            {
                return false;
            }
            controller.Redo();
            return true;
        case Key::C:
            if (!ctrl)
            {
                return false;
            }
            state.clipboard.assign(controller.Selection().begin(), controller.Selection().end());
            return true;
        case Key::X:
            if (!ctrl)
            {
                return false;
            }
            state.clipboard.assign(controller.Selection().begin(), controller.Selection().end());
            controller.RemoveSelection();
            return true;
        case Key::V:
        {
            if (!ctrl || state.clipboard.empty())
            {
                return ctrl;
            }
            const std::vector<GraphNodeId> copies = controller.Duplicate(state.clipboard, kPasteOffset);
            controller.Select(copies);
            return true;
        }
        default:
            return false;
        }
    }

    void OnKeyUp(Controller& controller, KeyInfo key)
    {
        if (key.key == Key::Space)
        {
            detail::Access(controller).spaceHeld = false;
        }
    }

    void OnDoubleClick(Controller& controller, Vec2 layerPos)
    {
        State& state = detail::Access(controller);
        state.EnsureLayout();
        const Vec2        canvas = state.ToCanvas(layerPos);
        const Layout::Hit hit =
            state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom());

        if (state.textEdit.active || state.popup.open)
        {
            return;
        }
        if (const CustomCell* cell = ArmCell(state, hit))
        {
            CellInputCtx ctx =
                CellCtx(controller, state, CellEvent::DoubleClick, layerPos, Vec2{}, KeyMods{});
            const bool consumed = cell->input(ctx);
            state.captureCell = nullptr;
            if (consumed)
            {
                return;
            }
        }
        if (hit.what == Layout::Hit::What::None)
        {
            if (const FrameHit frameHit = ProbeFrame(state, canvas);
                frameHit.part == FramePart::Header)
            {
                EditFrameTitle(controller, frameHit.frame);
            }
            return;
        }

        if (hit.what == Layout::Hit::What::Header)
        {
            const Node* node = state.model.Get(hit.node);
            if (node != nullptr)
            {
                controller.SetCollapsed(hit.node, !node->collapsed);
            }
            return;
        }
        if ((hit.what == Layout::Hit::What::Body || hit.what == Layout::Hit::What::Cell)
            && controller.onNodeActivated)
        {
            controller.onNodeActivated(hit.node);
        }
    }

    bool OnText(Controller& controller, std::string_view text)
    {
        State& state = detail::Access(controller);
        if (state.popup.open)
        {
            // The Space or Tab that opened the palette arrives here too; a query never starts blank.
            if (state.popup.query.empty() && text.find_first_not_of(" \t") == std::string_view::npos)
            {
                return true;
            }
            state.popup.query.append(text);
            RebuildRows(state);
            state.Touch();
            return true;
        }
        return false;
    }

    std::vector<ContextMenuItem> BuildMenu(Controller& controller, const ViewCfg& cfg, Vec2 layerPos)
    {
        State&     state = detail::Access(controller);
        const Vec2 canvas = state.ToCanvas(layerPos);

        CommitText(state);
        if (state.popup.open)
        {
            controller.ClosePopup();
        }

        state.EnsureLayout();
        const Layout::Hit hit =
            state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom());
        const FrameHit frameHit =
            hit.what == Layout::Hit::What::None ? ProbeFrame(state, canvas) : FrameHit{};

        if (cfg.frameVerbs && frameHit.part != FramePart::None)
        {
            const FrameId   id = frameHit.frame;
            ContextMenuItem rename;
            rename.label = "Rename frame";
            rename.onClick = [&controller, id] { EditFrameTitle(controller, id); };
            ContextMenuItem remove;
            remove.label = "Delete frame";
            remove.onClick = [&controller, id] { controller.RemoveFrame(id); };
            std::vector<ContextMenuItem> frameItems;
            frameItems.push_back(std::move(rename));
            frameItems.push_back(std::move(remove));
            return frameItems;
        }

        if (cfg.searchPalette && hit.what == Layout::Hit::What::None)
        {
            OpenPaletteFor(controller, canvas, PinRef{});
            return {};
        }

        std::vector<NodeTypeId> offered;
        if (cfg.paletteFilter)
        {
            offered = cfg.paletteFilter();
        }
        else
        {
            const std::span<const NodeTypeId> all = state.registry.All();
            offered.assign(all.begin(), all.end());
        }

        std::vector<PopupItem> offeredItems;
        for (const NodeTypeId type : offered)
        {
            if (const NodeTypeDesc* desc = state.registry.Find(type))
            {
                offeredItems.push_back(PopupItem{.title = desc->title, .category = desc->category, .type = type});
            }
        }
        ContextMenuItem add;
        add.label = "Add node";
        add.submenu = AddMenu(controller, BuildCategoryTree(offeredItems), offeredItems, canvas);

        std::vector<ContextMenuItem> items;
        items.push_back(std::move(add));

        const bool onNode = hit.what == Layout::Hit::What::Header || hit.what == Layout::Hit::What::Body
                         || hit.what == Layout::Hit::What::Cell;
        if (cfg.frameVerbs && onNode)
        {
            std::vector<GraphNodeId> framed;
            if (controller.IsSelected(hit.node))
            {
                framed.assign(controller.Selection().begin(), controller.Selection().end());
            }
            else
            {
                framed.push_back(hit.node);
            }
            std::erase_if(framed, [&state](GraphNodeId id)
                          {
                              return std::ranges::none_of(state.model.Frames(), [id](const Frame& f)
                                                          { return std::ranges::find(f.members, id) != f.members.end(); });
                          });
            if (!framed.empty())
            {
                ContextMenuItem leave;
                leave.label = "Remove from frame";
                leave.onClick = [&controller, framed]
                {
                    controller.Begin("Remove from frame");
                    for (const GraphNodeId id : framed)
                    {
                        controller.RemoveFromFrame(id);
                    }
                    controller.Commit();
                };
                items.push_back(std::move(leave));
            }
        }

        const bool hasSelection = !controller.Selection().empty();
        if (hasSelection)
        {
            ContextMenuItem duplicate;
            duplicate.label = "Duplicate";
            duplicate.onClick = [&controller]
            {
                const std::vector<GraphNodeId> copies =
                    controller.Duplicate(controller.Selection(), kPasteOffset);
                controller.Select(copies);
            };
            items.push_back(std::move(duplicate));

            ContextMenuItem remove;
            remove.label = "Delete";
            remove.onClick = [&controller] { controller.RemoveSelection(); };
            items.push_back(std::move(remove));

            if (cfg.frameVerbs)
            {
                ContextMenuItem frame;
                frame.label = "Add frame";
                frame.onClick = [&controller] { controller.AddFrame(); };
                items.push_back(std::move(frame));
            }
        }

        ContextMenuItem fit;
        fit.label = hasSelection ? "Frame selection" : "Frame all";
        fit.onClick = [&controller, hasSelection]
        { hasSelection ? controller.FitSelection() : controller.FitAll(); };
        items.push_back(std::move(fit));

        return items;
    }

    bool DropAccepts(Controller& controller, const DragPayload& payload)
    {
        State& state = detail::Access(controller);
        return state.cfg.dropAccepts && state.cfg.dropAccepts(payload, state.ToCanvas(state.lastPointerLayer));
    }

    void OnDrop(Controller& controller, const DragPayload& payload, Vec2 layerPos)
    {
        State& state = detail::Access(controller);
        if (!state.cfg.onDrop)
        {
            return;
        }
        state.EnsureLayout();
        state.lastPointerLayer = layerPos;
        const Vec2 canvas = state.ToCanvas(layerPos);
        if (state.layout.Probe(state.model, state.theme, canvas, state.camera.Zoom()).what != Layout::Hit::What::None)
        {
            return;
        }
        state.cfg.onDrop(payload, canvas);
    }
} // namespace lumora::nodegraph
