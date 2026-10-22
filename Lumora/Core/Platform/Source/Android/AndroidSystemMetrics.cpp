#include <lumora/platform/SystemMetrics.h>

#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <fstream>

namespace lumora::platform
{
    DefaultFontFiles GetDefaultUiFont()
    {
        DefaultFontFiles fonts{};
        const std::filesystem::path regular("/system/fonts/Roboto-Regular.ttf");
        const std::filesystem::path bold("/system/fonts/Roboto-Bold.ttf");

        std::error_code ec;
        if (std::filesystem::exists(regular, ec) && !ec)
        {
            fonts.regular = regular;
        }
        if (std::filesystem::exists(bold, ec) && !ec)
        {
            fonts.bold = bold;
        }
        return fonts;
    }

    ProcessMonitor::ProcessMonitor()
    {
        const long cores = ::sysconf(_SC_NPROCESSORS_ONLN);
        m_logicalCores = cores > 0 ? static_cast<uint32_t>(cores) : 1u;
    }

    ProcessMetrics ProcessMonitor::Sample()
    {
        ProcessMetrics out{};

        std::ifstream statm("/proc/self/statm");
        if (statm)
        {
            uint64_t sizePages = 0;
            uint64_t residentPages = 0;
            if (statm >> sizePages >> residentPages)
            {
                const long pageSize = ::sysconf(_SC_PAGESIZE);
                if (pageSize > 0)
                {
                    out.ramBytes = residentPages * static_cast<uint64_t>(pageSize);
                }
            }
        }

        struct rusage usage{};
        ::getrusage(RUSAGE_SELF, &usage);

        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);

        const uint64_t wall = static_cast<uint64_t>(ts.tv_sec) * 10000000ULL +
                              static_cast<uint64_t>(ts.tv_nsec) / 100ULL;
        const uint64_t uTicks = static_cast<uint64_t>(usage.ru_utime.tv_sec) * 10000000ULL +
                                static_cast<uint64_t>(usage.ru_utime.tv_usec) * 10ULL;
        const uint64_t kTicks = static_cast<uint64_t>(usage.ru_stime.tv_sec) * 10000000ULL +
                                static_cast<uint64_t>(usage.ru_stime.tv_usec) * 10ULL;

        if (m_primed)
        {
            const uint64_t dWall = wall - m_lastWall100ns;
            const uint64_t dCpu = (kTicks - m_lastKernel100ns) + (uTicks - m_lastUser100ns);
            if (dWall > 0)
            {
                const double frac =
                    static_cast<double>(dCpu) / (static_cast<double>(dWall) * static_cast<double>(m_logicalCores));
                out.cpuPercent = static_cast<float>(std::clamp(frac * 100.0, 0.0, 100.0));
            }
        }

        m_lastWall100ns = wall;
        m_lastKernel100ns = kTicks;
        m_lastUser100ns = uTicks;
        m_primed = true;

        return out;
    }
}
