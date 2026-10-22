#pragma once

#include <lumora/Reactive/Reactive.h>

#include <string>
#include <string_view>

namespace lumora {

/// Reactive<std::string> that also absorbs string literals and string_views.
class Str : public Reactive<std::string> {
public:
    using Reactive<std::string>::Reactive;

    Str() : Reactive(std::string{}) {}
    Str(const char* text) : Reactive(std::string(text ? text : "")) {}
    Str(std::string_view text) : Reactive(std::string(text)) {}
    Str(Reactive<std::string> value) : Reactive(std::move(value)) {}
};

}
