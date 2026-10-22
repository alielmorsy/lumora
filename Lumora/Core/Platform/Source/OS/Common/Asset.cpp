#include <lumora/os/Asset.h>
#include <lumora/os/Environment.h>

#include <filesystem>
#include <fstream>

namespace lumora::os
{
    std::optional<std::vector<std::byte>> ReadAsset(std::string_view path)
    {
        while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
        {
            path.remove_prefix(1);
        }

        const std::filesystem::path fullPath = ExecutableDir() / "assets" / std::filesystem::path(path);
        std::error_code ec;
        if (!std::filesystem::exists(fullPath, ec) || ec)
        {
            return std::nullopt;
        }

        const auto size = std::filesystem::file_size(fullPath, ec);
        if (ec)
        {
            return std::nullopt;
        }

        std::ifstream stream(fullPath, std::ios::binary);
        if (!stream)
        {
            return std::nullopt;
        }

        std::vector<std::byte> buffer(static_cast<std::size_t>(size));
        if (size > 0)
        {
            stream.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
            if (!stream)
            {
                return std::nullopt;
            }
        }
        return buffer;
    }

    std::optional<std::string> ReadAssetText(std::string_view path)
    {
        const auto bytes = ReadAsset(path);
        if (!bytes)
        {
            return std::nullopt;
        }
        return std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
}
