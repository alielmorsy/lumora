#pragma once

#include <lumora/Api.h>

namespace lumora::log
{
    /// Routes the global sink to spdlog. Call once at startup, before any logging.
    LUMORA_API void InstallSpdlog();
}
