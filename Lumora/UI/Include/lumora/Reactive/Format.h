#pragma once

#include <lumora/Reactive/Str.h>

#include <format>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace lumora {

/// Reactive std::format: recomputed (and re-bound into the prop) when any reactive argument changes. Signal/Computed
/// arguments are unwrapped for you. The format string is a runtime string_view, so there is no compile-time check.
///
///     Text(Format("{}. Settings", m_search));
///     Text(Format("{} users · {:.1f}ms", m_users, m_latency));
template<typename... Args>
[[nodiscard]] Str Format(std::string_view fmt, Args... args) {
    return Str([fmt = std::string(fmt), args = std::tuple<Args...>(std::move(args)...)] {
        return std::apply(
            [&fmt](const auto&... a) {
                // The reads happen here, inside the binding effect, so each reactive arg subscribes.
                auto values = std::make_tuple(detail::ReadReactive(a)...);
                return std::apply(
                    [&fmt](auto&... v) { return std::vformat(fmt, std::make_format_args(v...)); },
                    values);
            },
            args);
    });
}

}
