#pragma once

#include <lumora/Api.h>

#include <string>
#include <string_view>

namespace lumora::script
{
    /// Test seam: runs js in a fresh Hermes runtime and returns String(result), or "Error: <message>".
    [[nodiscard]] LUMORA_API std::string EvalToString(std::string_view js);
}
