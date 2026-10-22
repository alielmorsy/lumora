#include <lumora/os/FileSystem.h>

#include <fstream>
#include <sstream>

namespace lumora::os
{
    std::optional<std::string> ReadFileText(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) return std::nullopt;
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool WriteFileText(const std::filesystem::path& path, std::string_view content)
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        return static_cast<bool>(out);
    }

    bool WriteFileIfDifferent(const std::filesystem::path& tmp, const std::filesystem::path& dst)
    {
        std::error_code ec;
        if (std::filesystem::exists(dst, ec) && ReadFileText(tmp) == ReadFileText(dst))
        {
            std::filesystem::remove(tmp, ec);
            return true;
        }
        std::filesystem::remove(dst, ec);
        std::filesystem::rename(tmp, dst, ec);
        return !ec;
    }
}
