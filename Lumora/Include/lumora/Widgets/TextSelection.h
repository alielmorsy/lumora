#pragma once

#include <lumora/Ref.h>
#include <lumora/Foundation/Color.h>

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace lumora
{
    /// A caret position in a selection group: the text leaf (`key`, caller-assigned, in on-screen reading order) and a byte
    /// offset into its UTF-8 text. The key is logical, not a node id, so a selection survives a virtualized list unmounting rows.
    struct TextPos
    {
        std::uint64_t key    = 0;
        std::size_t   offset = 0;

        [[nodiscard]] friend constexpr auto operator<=>(const TextPos&, const TextPos&) = default;
    };

    /// Selection shared by a group of selectable Text leaves, one per TextSelectionScope, held by the caller. Pure model (no
    /// nodes, fonts or clipboard), so it is unit-testable.
    class TextSelection : public RefCounted
    {
    public:
        /// Painted behind selected glyphs; lives here so a leaf needs no second lookup at paint time.
        Color color = Color::Rgba(54, 110, 200, 130);

        [[nodiscard]] bool Active() const noexcept { return m_active; }
        [[nodiscard]] bool HasSelection() const noexcept { return m_active && m_anchor != m_focus; }

        [[nodiscard]] TextPos Anchor() const noexcept { return m_anchor; }
        [[nodiscard]] TextPos Focus() const noexcept { return m_focus; }
        [[nodiscard]] TextPos Min() const noexcept { return m_anchor < m_focus ? m_anchor : m_focus; }
        [[nodiscard]] TextPos Max() const noexcept { return m_anchor < m_focus ? m_focus : m_anchor; }

        /// Drop the anchor at `p` and collapse onto it (a fresh click).
        void Begin(TextPos p)
        {
            m_anchor = p;
            m_focus  = p;
            m_active = true;
            ++m_epoch;
        }

        /// Move the focus end, keeping the anchor (drag or Shift-click). Extending before an anchor was dropped begins there, so a
        /// Shift-click into a fresh scope is not a no-op.
        void ExtendTo(TextPos p)
        {
            if (!m_active)
            {
                Begin(p);
                return;
            }
            if (m_focus == p)
            {
                return;
            }
            m_focus = p;
            ++m_epoch;
        }

        /// Set both ends at once - word/line expansion and Select All.
        void Set(TextPos anchor, TextPos focus)
        {
            m_anchor = anchor;
            m_focus  = focus;
            m_active = true;
            ++m_epoch;
        }

        void Clear()
        {
            if (!m_active)
            {
                return;
            }
            m_anchor = {};
            m_focus  = {};
            m_active = false;
            ++m_epoch;
        }

        /// The byte range of leaf `key` (text `length` bytes) the selection covers, or nullopt. Offsets are clamped to `length` so a
        /// changed leaf never hands a stale offset to the shaper.
        [[nodiscard]] std::optional<std::pair<std::size_t, std::size_t>> RangeIn(std::uint64_t key,
                                                                                 std::size_t length) const
        {
            if (!HasSelection())
            {
                return std::nullopt;
            }
            const TextPos lo = Min();
            const TextPos hi = Max();
            if (key < lo.key || key > hi.key)
            {
                return std::nullopt;
            }
            const std::size_t begin = key == lo.key ? std::min(lo.offset, length) : 0;
            const std::size_t end   = key == hi.key ? std::min(hi.offset, length) : length;
            if (begin >= end)
            {
                return std::nullopt;
            }
            return std::pair{begin, end};
        }

        /// Bumped by every mutation; the scope repaints off it and tests can assert on it.
        [[nodiscard]] std::uint64_t Epoch() const noexcept { return m_epoch; }

    private:
        TextPos       m_anchor{};
        TextPos       m_focus{};
        bool          m_active = false;
        std::uint64_t m_epoch  = 0;
    };

    using TextSelectionRef = Ref<TextSelection>;

    [[nodiscard]] inline TextSelectionRef MakeTextSelection() { return MakeRef<TextSelection>(); }
}
