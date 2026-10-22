#pragma once

#include <lumora/Elem.h>

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

namespace lumora {

/// Deferred, parameterised content owned by the callee: a component stores a Slot and calls it with values it owns; the Elem
/// instantiates under the component's scope. The mirror of Children, which passes already-built values.
template<typename... Ts>
class Slot {
public:
    Slot() = default;

    template<typename F>
    requires std::invocable<F, Ts...>
          && std::convertible_to<std::invoke_result_t<F, Ts...>, Elem>
          && (!std::same_as<std::remove_cvref_t<F>, Slot>)
    Slot(F fn) : m_fn(std::move(fn)) {} // NOLINT(*-explicit-constructor)

    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_fn); }

    Elem operator()(Ts... args) const {
        return m_fn ? m_fn(std::forward<Ts>(args)...) : Elem{};
    }

private:
    std::function<Elem(Ts...)> m_fn;
};

}
