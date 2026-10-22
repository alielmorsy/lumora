#pragma once

#include <lumora/Foundation/Color.h>
#include <lumora/Foundation/Vec2.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Widgets/ContextMenu.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace lumora
{
    struct InteractionState;

    /// Look of the in-app context menu. The layout constants double as the analytic sizing model used to place menus without solved rects.
    struct MenuStyle
    {
        Color fill = Color::Rgba(40, 44, 52, 255);
        Color text = Color::Rgba(222, 226, 232, 255);
        Color textDim = Color::Rgba(132, 140, 152, 255);
        Color hover = Color::Rgba(55, 62, 74, 255);
        Color border = Color::Rgba(60, 66, 78, 255);
        float radius = 8.f;
        float minWidth = 172.f;
        float itemHeight = 28.f;
        float fontSize = 13.f;
        float panelPad = 6.f;
        float itemPadH = 12.f;
        float gap = 28.f;
        float sepHeight = 9.f;
    };

    /// One open menu panel: `anchor` is its top-left in surface coords (post-clamp), `parentIndex` the item that spawned it.
    struct MenuLevel
    {
        std::uint64_t id = 0;
        int parentIndex = -1;
        std::vector<ContextMenuItem> items;
        Vec2 anchor;
        float width = 0.f;
    };

    /// Per-app context-menu state: a stack of levels (root + one per open submenu); `levels` is the reactive source the overlay renders.
    /// Close/select are deferred to Drain so a menu never frees itself mid-click.
    struct ContextMenuHost
    {
        Signal<std::vector<MenuLevel>> levels;
        Vec2 surface{};
        MenuStyle style{};

        /// An empty list closes any open menu.
        void Open(Vec2 anchor, std::vector<ContextMenuItem> items);

        /// Closes deeper panels; opens the item's submenu if any. Idempotent while it is already showing.
        void HoverItem(int depth, int index);

        /// Defers the action and a close; both run on the next Drain.
        void Select(std::function<void()> action);

        void RequestClose();

        [[nodiscard]] bool IsOpen() const { return !levels.Peek().empty(); }
        void SetSurface(Vec2 s) { surface = s; }

        /// Runs the deferred close + action; called from MountedApp::Tick after the graph tick.
        void Drain();

    private:
        struct Extent
        {
            float width = 0.f;
            float height = 0.f;
        };

        [[nodiscard]] Extent EstimateExtent(const std::vector<ContextMenuItem>& items) const;
        [[nodiscard]] Vec2 SubAnchor(const MenuLevel& parent, int index, Extent child) const;

        std::uint64_t m_nextId = 1;
        std::function<void()> m_pending;
        bool m_closeReq = false;
    };

    /// Indirection so Interaction.h can open a menu without including this header. No-op when no host is installed.
    void OpenContextMenu(InteractionState* state, Vec2 anchor, std::vector<ContextMenuItem> items);
} // namespace lumora
