#pragma once

#include "Style/ComputedStyle.h"
#include "Style/Keyframes.h"
#include "Style/Transition.h"

#include <lumora/StringId.h>
#include <lumora/Reactive/Signal.h>

#include <cstdint>
#include <vector>

namespace lumora::style
{
    /// Pseudo-class states as signals; the matcher reads them (tracked) so a state change re-resolves only nodes whose
    /// selectors test it.
    struct PseudoSignals
    {
        Signal<bool> hover{false};
        Signal<bool> active{false};
        Signal<bool> focus{false};
        Signal<bool> disabled{false};
        Signal<bool> checked{false};
        Signal<bool> dropActive{false};
    };

    struct CssNodeState
    {
        /// True once a widget wired a style-resolution effect. Dock, Expanded, Splitter, Painter and Svg do not, so
        /// descendants must look past them for the var map.
        bool participates = false;

        Signal<uint32_t> classEpoch{0u};
        Signal<uint32_t> inheritEpoch{0u}; ///< bumped when inheritedOut changes; descendants subscribe
        std::vector<StringId> classes;
        PseudoSignals pseudo;
        ComputedStyle current;
        InheritedStyle inheritedOut;
        TransitionRuntime transitions;
        AnimationRuntime anim;
    };
} // namespace lumora::style
