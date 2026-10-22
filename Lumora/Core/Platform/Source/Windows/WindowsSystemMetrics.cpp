#include <lumora/platform/SystemMetrics.h>

#include "WindowsHeaders.h"

#include <psapi.h>

#include <algorithm>
#include <string>

namespace lumora::platform
{
    namespace
    {
        [[nodiscard]] uint64_t ToTicks(const FILETIME& ft) noexcept
        {
            ULARGE_INTEGER u{};
            u.LowPart  = ft.dwLowDateTime;
            u.HighPart = ft.dwHighDateTime;
            return u.QuadPart; // 100ns units
        }

        // System Fonts directory from the real Windows dir, not a hardcoded drive letter.
        std::filesystem::path FontsDir()
        {
            wchar_t    win[MAX_PATH]{};
            const UINT n = ::GetWindowsDirectoryW(win, MAX_PATH);
            const std::filesystem::path base =
                (n > 0 && n < MAX_PATH) ? std::filesystem::path(win) : std::filesystem::path(L"C:\\Windows");
            return base / L"Fonts";
        }

        // Looks up a Fonts-registry value (e.g. "Segoe UI (TrueType)") and returns its file path; empty on any miss.
        std::filesystem::path ResolveRegisteredFont(const std::wstring&          valueName,
                                                    const std::filesystem::path& fontsDir)
        {
            HKEY key{};
            if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
                                KEY_READ, &key) != ERROR_SUCCESS)
                return {};

            wchar_t       data[MAX_PATH]{};
            DWORD         size = sizeof(data);
            DWORD         type = 0;
            const LSTATUS st =
                ::RegQueryValueExW(key, valueName.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(data), &size);
            ::RegCloseKey(key);
            if (st != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};

            const std::filesystem::path file(data);
            return file.is_absolute() ? file : fontsDir / file;
        }
    } // namespace

    ProcessMonitor::ProcessMonitor()
    {
        SYSTEM_INFO si{};
        ::GetSystemInfo(&si);
        m_logicalCores = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1u;
    }

    ProcessMetrics ProcessMonitor::Sample()
    {
        ProcessMetrics out{};

        const HANDLE self = ::GetCurrentProcess();

        PROCESS_MEMORY_COUNTERS pmc{};
        if (::GetProcessMemoryInfo(self, &pmc, sizeof(pmc)))
        {
            out.ramBytes = static_cast<uint64_t>(pmc.WorkingSetSize);
        }

        FILETIME creation{}, exit{}, kernel{}, user{}, now{};
        ::GetProcessTimes(self, &creation, &exit, &kernel, &user);
        ::GetSystemTimeAsFileTime(&now);

        const uint64_t wall   = ToTicks(now);
        const uint64_t kTicks = ToTicks(kernel);
        const uint64_t uTicks = ToTicks(user);

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
        const std::filesystem::path fontsDir = FontsDir();

        // The OS UI font face (Segoe UI on Win10/11) is the non-client message font.
        std::wstring      face = L"Segoe UI";
        NONCLIENTMETRICSW ncm{};
        ncm.cbSize = sizeof(ncm);
        if (::SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0))
            face = ncm.lfMessageFont.lfFaceName;

        DefaultFontFiles out;
        out.regular = ResolveRegisteredFont(face + L" (TrueType)", fontsDir);
        out.bold    = ResolveRegisteredFont(face + L" Bold (TrueType)", fontsDir);

        // Fallbacks when the face is not a plain TrueType registry entry (renamed / collection / etc.).
        if (out.regular.empty()) out.regular = fontsDir / L"segoeui.ttf";
        if (out.bold.empty()) out.bold = fontsDir / L"segoeuib.ttf";
        return out;
    }
} // namespace lumora::platform
