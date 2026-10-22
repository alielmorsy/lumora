#pragma once

#include <lumora/Debug/FrameCounters.h>
#include <lumora/Foundation/NodeId.h>
#include <lumora/Text/TextMeasurer.h>

namespace lumora {

class NodeArena;
class ImageProvider;

/// Threaded into every measure callback: the injected measurer plus the arena the callback
/// reads its payload from.
struct MeasureContext {
    const TextMeasurer* text     = nullptr;
    NodeArena*          arena    = nullptr;
    ImageProvider*      images   = nullptr;
    FrameCounters*      counters = nullptr; ///< null outside a MountedApp frame

    /// Measure through the seam and count the call. Every measure inside layout goes through here, so
    /// `textMeasureCalls` is the whole shaping bill the layout pass hands the font system.
    [[nodiscard]] Vec2 MeasureText(std::string_view content, const FontProps& font, float size,
                                   const TextFlowProps& flow, AvailableSpace width) const
    {
        if (!text) {
            return {};
        }
        if (counters) {
            ++counters->textMeasureCalls;
        }
        return text->Measure(content, font, size, flow, width);
    }
};

} // namespace lumora
