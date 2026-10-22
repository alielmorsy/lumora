#include "GlyphAtlas.h"

#include <algorithm>

namespace lumora
{
    constexpr int kPadding = 1;

    GlyphAtlas::GlyphAtlas(AtlasFormat format, int width, int initialHeight)
        : _format(format),
          _bpp(format == AtlasFormat::R8 ? 1 : 4),
          _width(width),
          _height(initialHeight),
          _pixels(static_cast<std::size_t>(width) * initialHeight * _bpp, 0)
    {
    }

    const GlyphAtlas::Slot* GlyphAtlas::Find(std::uint64_t key) const
    {
        const auto it = _slots.find(key);
        return it == _slots.end() ? nullptr : &it->second;
    }

    bool GlyphAtlas::Reserve(int w, int h, Slot& out)
    {
        if (w > _width)
        {
            return false; // a single glyph wider than the whole plane: give up (v1)
        }
        if (_penX + w > _width)
        {
            _penY += _rowHeight; // next shelf
            _penX = 0;
            _rowHeight = 0;
        }
        while (_penY + h > _height)
        {
            _height *= 2;
            _pixels.resize(static_cast<std::size_t>(_width) * _height * _bpp, 0); // append zeroed rows
            ++_version; // size changed → re-upload
        }
        out = {_penX, _penY, w, h};
        _penX += w + kPadding;
        _rowHeight = std::max(_rowHeight, h + kPadding);
        return true;
    }

    const GlyphAtlas::Slot* GlyphAtlas::Add(std::uint64_t key, const std::uint8_t* pixels, int w, int h,
                                            int srcPitch)
    {
        if (const Slot* existing = Find(key))
        {
            return existing;
        }
        Slot slot{};
        if (w <= 0 || h <= 0)
        {
            slot = {_penX, _penY, 0, 0}; // empty glyph (space): a valid zero-area slot
            return &_slots.emplace(key, slot).first->second;
        }
        if (!Reserve(w, h, slot))
        {
            return nullptr;
        }

        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* src = pixels + (static_cast<std::size_t>(row) * srcPitch);
            std::uint8_t* dst = _pixels.data() +
                ((static_cast<std::size_t>(slot.y + row) * _width + slot.x) * _bpp);
            std::copy_n(src, static_cast<std::size_t>(w) * _bpp, dst);
        }
        MarkDirty(slot.y, slot.y + h);
        ++_version;
        return &_slots.emplace(key, slot).first->second;
    }

    void GlyphAtlas::MarkDirty(int y0, int y1)
    {
        if (_dirtyY1 <= _dirtyY0) // currently empty
        {
            _dirtyY0 = y0;
            _dirtyY1 = y1;
        }
        else
        {
            _dirtyY0 = std::min(_dirtyY0, y0);
            _dirtyY1 = std::max(_dirtyY1, y1);
        }
    }

    void GlyphAtlas::Reset()
    {
        _slots.clear();
        _penX = 0;
        _penY = 0;
        _rowHeight = 0;
        std::fill(_pixels.begin(), _pixels.end(), std::uint8_t{0});
        ++_version;
        MarkDirty(0, _height);
    }

    AtlasView GlyphAtlas::View() const
    {
        AtlasView v;
        v.pixels = _pixels.data();
        v.width = _width;
        v.height = _height;
        v.format = _format;
        v.version = _version;
        v.dirtyY0 = _dirtyY0;
        v.dirtyY1 = std::min(_dirtyY1, _height);
        _dirtyY0 = 0; // consumed
        _dirtyY1 = 0;
        return v;
    }
} // namespace lumora
