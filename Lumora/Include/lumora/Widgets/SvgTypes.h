#pragma once

#include <cstdint>

namespace lumora
{
    /// viewBox alignment in the laid-out box (SVG preserveAspectRatio subset): `None` stretches non-uniformly; the rest pin to a
    /// corner/edge/centre; `XMidYMid` is the SVG default.
    enum class SvgAlign : std::uint8_t
    {
        None,
        XMinYMin, XMidYMin, XMaxYMin,
        XMinYMid, XMidYMid, XMaxYMid,
        XMinYMax, XMidYMax, XMaxYMax,
    };

    /// Meet scales so the whole viewBox fits inside the box (letterbox); Slice scales to cover it (crop).
    enum class SvgMeetOrSlice : std::uint8_t { Meet, Slice };
}
