#pragma once

// Ready-made CustomCell factories; the value lives in the host (`get` every paint, `set` on edit). Edits are
// bracketed by onEdit(Begin)...onEdit(Commit) so a host can fold them into one undo step. Set `pin` to an input
// slot and the field sits on that row, hiding while linked. Numbers scrub on horizontal drag (Shift = fine).

#include <lumora/Api.h>
#include <lumora/Graph/Types.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace lumora::nodegraph
{
    enum class EditPhase : std::uint8_t { Begin, Commit };

    struct FloatFieldCfg
    {
        std::string                                 label;
        std::uint16_t                               pin = NoPin;
        std::function<float(GraphNodeId)>           get;
        std::function<void(GraphNodeId, float)>     set;
        std::function<void(GraphNodeId, EditPhase)> onEdit;
        /// Value change per screen pixel of scrub.
        float                                       speed = 0.01f;
        float                                       min = std::numeric_limits<float>::lowest();
        float                                       max = std::numeric_limits<float>::max();
        int                                         decimals = 3;
    };

    struct VecFieldCfg
    {
        std::string                                      label;
        std::uint16_t                                    pin = NoPin;
        std::function<float(GraphNodeId, int)>           get;
        std::function<void(GraphNodeId, int, float)>     set;
        std::function<void(GraphNodeId, EditPhase)>      onEdit;
        float                                            speed = 0.01f;
        float                                            min = std::numeric_limits<float>::lowest();
        float                                            max = std::numeric_limits<float>::max();
        int                                              decimals = 2;
    };

    /// Paints the colour; a click hands the host the node and the surface point to open its own picker.
    struct ColorSwatchCfg
    {
        std::string                             label;
        std::uint16_t                           pin = NoPin;
        std::function<Color(GraphNodeId)>       get;
        std::function<void(GraphNodeId, Vec2)>  onEdit;
    };

    /// Click opens a filterable pick list.
    struct EnumFieldCfg
    {
        std::string                                 label;
        std::uint16_t                               pin = NoPin;
        std::vector<std::string>                    options;
        std::function<int(GraphNodeId)>             get;
        std::function<void(GraphNodeId, int)>       set;
        std::function<void(GraphNodeId, EditPhase)> onEdit;
    };

    struct ToggleFieldCfg
    {
        std::string                                 label;
        std::uint16_t                               pin = NoPin;
        std::function<bool(GraphNodeId)>            get;
        std::function<void(GraphNodeId, bool)>      set;
        std::function<void(GraphNodeId, EditPhase)> onEdit;
    };

    [[nodiscard]] LUMORA_API Cell FloatField(FloatFieldCfg cfg);
    /// `components` is clamped to 1..4.
    [[nodiscard]] LUMORA_API Cell VecField(int components, VecFieldCfg cfg);
    [[nodiscard]] LUMORA_API Cell ColorSwatch(ColorSwatchCfg cfg);
    [[nodiscard]] LUMORA_API Cell EnumField(EnumFieldCfg cfg);
    [[nodiscard]] LUMORA_API Cell ToggleField(ToggleFieldCfg cfg);
}
