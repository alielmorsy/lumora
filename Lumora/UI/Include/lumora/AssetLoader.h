#pragma once

#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace lumora
{
    /// Reads an asset by path into `out`; false when missing or unreadable. The host injects this via
    /// LumoraDesc to resolve against a VFS/.pak.
    using AssetLoader = bool (*)(std::string_view path, std::string& out);

    /// Default: filesystem read.
    inline bool DefaultFileLoad(std::string_view path, std::string& out)
    {
        std::ifstream file(std::string(path), std::ios::binary);
        if (!file)
        {
            return false;
        }
        out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return true;
    }
}
