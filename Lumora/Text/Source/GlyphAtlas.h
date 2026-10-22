#pragma once

#include <lumora/Text/Glyphs.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lumora
{
    /// A CPU atlas plane: shelf-packed pixel buffer keyed by glyph. Width is fixed; height doubles when a shelf overflows.
    /// Slots store PIXEL rects, so a grow never invalidates entries.
    /// The painter mirrors `View()` to a texture and re-uploads when `version` changes (new glyph or grow); `View()` also
    /// reports and consumes the dirty row span.
    class GlyphAtlas
    {
    public:
        struct Slot
        {
            int x = 0;
            int y = 0;
            int w = 0;
            int h = 0;
        };

        GlyphAtlas(AtlasFormat format, int width, int initialHeight);

        [[nodiscard]] const Slot* Find(std::uint64_t key) const;

        /// Packs a `w`x`h` glyph (`bpp`-byte texels, stride `srcPitch`) under `key`. Returns the slot, or nullptr if it
        /// cannot fit even after growing.
        const Slot* Add(std::uint64_t key, const std::uint8_t* pixels, int w, int h, int srcPitch);

        /// The live plane size. Non-mutating, unlike `View()`, so normalising callers do not consume the dirty span.
        [[nodiscard]] int Width() const noexcept { return _width; }
        [[nodiscard]] int Height() const noexcept { return _height; }

        /// Snapshot for the painter's upload. **Consumes** the dirty row span: call it once per sync.
        [[nodiscard]] AtlasView View() const;

        /// Drops every glyph and zeroes the plane at its current size; bumps `version` and marks it all dirty. Used when the
        /// content scale changes.
        void Reset();

    private:
        bool Reserve(int w, int h, Slot& out);
        void MarkDirty(int y0, int y1);

        AtlasFormat _format;
        int _bpp;
        int _width;
        int _height;
        int _penX = 0;
        int _penY = 0;
        int _rowHeight = 0;
        std::vector<std::uint8_t> _pixels;
        std::unordered_map<std::uint64_t, Slot> _slots;
        std::uint64_t _version = 1;
        mutable int _dirtyY0 = 0; // [_dirtyY0, _dirtyY1) changed since last View(); empty
        mutable int _dirtyY1 = 0; // when _dirtyY1 <= _dirtyY0. Consumed (reset) by View().
    };
} // namespace lumora
