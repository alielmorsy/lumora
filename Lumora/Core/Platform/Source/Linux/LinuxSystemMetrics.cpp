#include <lumora/platform/SystemMetrics.h>

#include "LinuxTools.h"

#include "lumora/os/Process.h"

#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace lumora::platform
{
    namespace
    {
        [[nodiscard]] uint64_t Ticks(const timeval& tv) noexcept
        {
            return (static_cast<uint64_t>(tv.tv_sec) * 1000000ull + static_cast<uint64_t>(tv.tv_usec)) * 10ull; // 100ns units
        }

        [[nodiscard]] uint64_t WallTicks() noexcept
        {
            timespec ts{};
            ::clock_gettime(CLOCK_MONOTONIC, &ts);
            return (static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec)) / 100ull;
        }

        std::filesystem::path MatchFont(const std::string& pattern)
        {
            const auto tool = FindTool({"fc-match"});
            if (!tool) return {};

            const os::ProcessResult result = os::Run(*tool, {"-f", "%{file}", pattern});
            if (!result.launched || result.exitCode != 0 || result.out.empty()) return {};

            std::filesystem::path file(result.out);
            std::error_code       ec;
            return std::filesystem::exists(file, ec) ? file : std::filesystem::path{};
        }

        std::filesystem::path FirstExisting(std::initializer_list<const char*> candidates)
        {
            for (const char* candidate : candidates)
            {
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec)) return candidate;
            }
            return {};
        }
    }

    ProcessMonitor::ProcessMonitor()
    {
        const long cores = ::sysconf(_SC_NPROCESSORS_ONLN);
        m_logicalCores   = cores > 0 ? static_cast<uint32_t>(cores) : 1u;
    }

    ProcessMetrics ProcessMonitor::Sample()
    {
        ProcessMetrics out{};
        out.ramBytes = os::CurrentProcessMemory().workingSetBytes;

        rusage usage{};
        ::getrusage(RUSAGE_SELF, &usage);

        const uint64_t wall   = WallTicks();
        const uint64_t kTicks = Ticks(usage.ru_stime);
        const uint64_t uTicks = Ticks(usage.ru_utime);

        if (m_primed)
        {
            const uint64_t dWall = wall - m_lastWall100ns;
            const uint64_t dCpu  = (kTicks - m_lastKernel100ns) + (uTicks - m_lastUser100ns);
            if (dWall > 0)
            {
                const double frac =
                    static_cast<double>(dCpu) / (static_cast<double>(dWall) * static_cast<double>(m_logicalCores));
                out.cpuPercent = static_cast<float>(std::clamp(frac * 100.0, 0.0, 100.0));
            }
        }

        m_lastWall100ns   = wall;
        m_lastKernel100ns = kTicks;
        m_lastUser100ns   = uTicks;
        m_primed          = true;

        return out;
    }

    DefaultFontFiles GetDefaultUiFont()
    {
        DefaultFontFiles out;
        out.regular = MatchFont("sans-serif");
        out.bold    = MatchFont("sans-serif:bold");

        if (out.regular.empty())
        {
            out.regular = FirstExisting({"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                         "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                                         "/usr/share/fonts/TTF/DejaVuSans.ttf",
                                         "/usr/share/fonts/liberation/LiberationSans-Regular.ttf"});
        }
        if (out.bold.empty())
        {
            out.bold = FirstExisting({"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                                      "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
                                      "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                                      "/usr/share/fonts/liberation/LiberationSans-Bold.ttf"});
        }
        return out;
    }
}
