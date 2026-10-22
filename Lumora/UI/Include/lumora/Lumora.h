#pragma once

#include <lumora/Api.h>

#include <string_view>

namespace lumora
{
    // Threading: the reactive graph is synchronous and single-threaded. All signal writes must happen on the thread
    // that ticks the app; async producers must marshal onto it first.

    [[nodiscard]] LUMORA_API std::string_view Version() noexcept;
}
