#pragma once

#include <cstdint>

namespace lumora
{
    /// How much of the previous frame's paint a mounted app may keep.
    ///  * Off     - re-emit the whole tree every frame (bisect lever for a suspected reuse bug).
    ///  * Frame   - retain a display list; a frame with nothing repaint-dirty re-presents it (or the canvas's own geometry).
    ///  * Subtree - as Frame, plus a partially dirty frame walks only dirty nodes and republishes clean subtrees' bytes.
    enum class PaintReuse : std::uint8_t { Off, Frame, Subtree };
}
