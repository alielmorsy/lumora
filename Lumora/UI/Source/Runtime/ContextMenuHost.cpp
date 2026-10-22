#include "Runtime/ContextMenuHost.h"

#include "Input/Interaction.h"

#include <algorithm>
#include <utility>

namespace lumora
{
    ContextMenuHost::Extent ContextMenuHost::EstimateExtent(const std::vector<ContextMenuItem>& items) const
    {
        float height = style.panelPad * 2.f;
        float widest = 0.f;
        for (const ContextMenuItem& it : items)
        {
            if (it.separator)
            {
                height += style.sepHeight;
                continue;
            }
            height += style.itemHeight;
            const float label    = static_cast<float>(it.label.size()) * style.fontSize * 0.60f;
            const float hint     = it.submenu.empty()
                                       ? static_cast<float>(it.shortcut.size()) * style.fontSize * 0.52f
                                       : style.fontSize;
            const bool  hasRight = !it.submenu.empty() || !it.shortcut.empty();
            widest = std::max(widest, label + (hasRight ? hint + style.gap : 0.f));
        }
        return {std::max(style.minWidth, widest + style.itemPadH * 2.f), height};
    }

    Vec2 ContextMenuHost::SubAnchor(const MenuLevel& parent, int index, Extent child) const
    {
        float y = parent.anchor.y + style.panelPad;
        for (int k = 0; k < index && k < static_cast<int>(parent.items.size()); ++k)
        {
            y += parent.items[k].separator ? style.sepHeight : style.itemHeight;
        }
        float x = parent.anchor.x + parent.width;
        if (surface.x > 0.f && x + child.width > surface.x)
        {
            x = parent.anchor.x - child.width;
        }
        if (surface.y > 0.f && y + child.height > surface.y)
        {
            y = std::max(0.f, surface.y - child.height);
        }
        return {std::max(0.f, x), std::max(0.f, y)};
    }

    void ContextMenuHost::Open(Vec2 anchor, std::vector<ContextMenuItem> items)
    {
        if (items.empty())
        {
            levels.Update([](std::vector<MenuLevel>& lv) { lv.clear(); });
            return;
        }

        MenuLevel root;
        root.id    = m_nextId++;
        root.items = std::move(items);
        const Extent e = EstimateExtent(root.items);
        root.width = e.width;

        Vec2 pos = anchor;
        if (surface.x > 0.f && surface.y > 0.f)
        {
            if (pos.x + e.width > surface.x)
            {
                pos.x = surface.x - e.width;
            }
            if (pos.y + e.height > surface.y)
            {
                pos.y = anchor.y - e.height;
            }
            pos.x = std::clamp(pos.x, 0.f, std::max(0.f, surface.x - e.width));
            pos.y = std::max(0.f, pos.y);
        }
        root.anchor = pos;

        levels.Update([&root](std::vector<MenuLevel>& lv) {
            lv.clear();
            lv.push_back(std::move(root));
        });
    }

    void ContextMenuHost::HoverItem(int depth, int index)
    {
        levels.Update([this, depth, index](std::vector<MenuLevel>& lv) {
            if (depth < 0 || depth >= static_cast<int>(lv.size()))
            {
                return;
            }
            if (index < 0 || index >= static_cast<int>(lv[depth].items.size()))
            {
                return;
            }
            if (static_cast<int>(lv.size()) == depth + 2 && lv[depth + 1].parentIndex == index)
            {
                return;
            }
            if (static_cast<int>(lv.size()) > depth + 1)
            {
                lv.resize(depth + 1);
            }
            const ContextMenuItem& it = lv[depth].items[index];
            if (!it.enabled || it.submenu.empty())
            {
                return;
            }
            MenuLevel child;
            child.id          = m_nextId++;
            child.parentIndex = index;
            child.items       = it.submenu;
            const Extent e    = EstimateExtent(child.items);
            child.width       = e.width;
            child.anchor      = SubAnchor(lv[depth], index, e);
            lv.push_back(std::move(child));
        });
    }

    void ContextMenuHost::Select(std::function<void()> action)
    {
        m_pending  = std::move(action);
        m_closeReq = true;
    }

    void ContextMenuHost::RequestClose()
    {
        m_closeReq = true;
    }

    void ContextMenuHost::Drain()
    {
        if (m_closeReq)
        {
            m_closeReq = false;
            levels.Update([](std::vector<MenuLevel>& lv) { lv.clear(); });
        }
        if (m_pending)
        {
            std::function<void()> fn = std::move(m_pending);
            m_pending = nullptr;
            fn();
        }
    }

    void OpenContextMenu(InteractionState* state, Vec2 anchor, std::vector<ContextMenuItem> items)
    {
        if (state && state->menu)
        {
            state->menu->Open(anchor, std::move(items));
        }
    }
} // namespace lumora
