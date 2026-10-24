#pragma once

#include "Style/ComputedStyle.h"
#include "Style/Keyframes.h"
#include "Style/Transition.h"

#include <lumora/StringId.h>
#include <lumora/Reactive/Signal.h>
#include <lumora/Style/Stylesheet.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lumora::style
{
    /// A signal allocated on its first tracked read: until something subscribes, a write only updates the plain value. Most
    /// nodes are never read this way (leaves have no children reading their epochs; few selectors test pseudo-state).
    template <typename T>
    class LazySignal
    {
    public:
        explicit LazySignal(T initial = T{}) : m_value(std::move(initial)) {}

        [[nodiscard]] T Read()
        {
            if (!m_signal)
            {
                m_signal.emplace(m_value);
            }
            return (*m_signal)();
        }
        [[nodiscard]] const T& Peek() const noexcept { return m_value; }
        void Write(T value)
        {
            m_value = value;
            if (m_signal)
            {
                *m_signal = std::move(value);
            }
        }

    private:
        T m_value;
        std::optional<Signal<T>> m_signal;
    };

    struct CssNodeState
    {
        /// True once a widget wired a style-resolution effect. Dock, Expanded, Splitter, Painter and Svg do not, so
        /// descendants must look past them for the var map.
        bool participates = false;

        LazySignal<uint32_t> classEpoch{0u};
        LazySignal<uint32_t> inheritEpoch{0u}; ///< bumped when inheritedOut changes; descendants subscribe
        std::vector<StringId> classes;
        /// The inline `style` string last seen and its parse, so an unrelated re-resolve does not re-parse it.
        std::string inlineSource;
        std::vector<Declaration> inlineDecls;
        /// Pseudo-class states as one bit per Pseudo; the matcher reads it (tracked) so a state change re-resolves only nodes
        /// whose selectors test pseudo-state. One signal, as the matcher always reads every state at once.
        LazySignal<std::uint8_t> pseudo{std::uint8_t{0}};
        ComputedStyle current;
        InheritedStyle inheritedOut;
        bool resolved = false; ///< the resolver has applied at least once

        /// Transition and @keyframes state, allocated by the first resolve that declares either: most nodes never animate.
        struct Motion
        {
            TransitionRuntime transitions;
            AnimationRuntime anim;
        };
        std::unique_ptr<Motion> motion;
    };
} // namespace lumora::style
