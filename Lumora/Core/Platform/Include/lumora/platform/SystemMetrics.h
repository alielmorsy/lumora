#pragma once

#include <lumora/Api.h>

#include <cstdint>
#include <filesystem>

namespace lumora::platform
{
    /// Absolute paths of the OS default UI font (Segoe UI on Windows). `bold` is empty when no bold face is installed.
    struct DefaultFontFiles
    {
        std::filesystem::path regular;
        std::filesystem::path bold;
    };

    /// Resolves the OS default UI font; never throws, falls back to Segoe UI under the system Fonts directory. Cheap; call once at startup.
    [[nodiscard]] LUMORA_API DefaultFontFiles GetDefaultUiFont();

    struct ProcessMetrics
    {
        float    cpuPercent = 0.f; // 0..100, this process's CPU time as a share of total capacity (all cores)
        uint64_t ramBytes   = 0;   // resident working-set size, bytes
    };

    /// Samples this process's CPU and memory. CPU is a rate computed between successive Sample() calls (call once per
    /// frame/interval); the first call primes the baseline and reports 0%. Not thread-safe.
    class LUMORA_API ProcessMonitor
    {
    public:
        ProcessMonitor();

        [[nodiscard]] ProcessMetrics Sample();

    private:
        uint64_t m_lastWall100ns   = 0;
        uint64_t m_lastKernel100ns = 0;
        uint64_t m_lastUser100ns   = 0;
        uint32_t m_logicalCores    = 1;
        bool     m_primed          = false;
    };
} // namespace lumora::platform
