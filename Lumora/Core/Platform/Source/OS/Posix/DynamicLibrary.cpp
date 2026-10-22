#include <lumora/os/DynamicLibrary.h>
#include <lumora/log/Log.h>

#include <dlfcn.h>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    std::optional<DynamicLibrary> DynamicLibrary::Open(const std::filesystem::path& path)
    {
        void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
        {
            LOG_ERROR("OSCore::DynamicLibrary: dlopen failed ({}) for {}", ::dlerror(), path.string());
            return std::nullopt;
        }
        return DynamicLibrary(handle);
    }

    void* DynamicLibrary::RawSymbol(const char* name) const
    {
        if (!_handle) return nullptr;
        return ::dlsym(_handle, name);
    }

    void DynamicLibrary::Close() noexcept
    {
        if (_handle)
        {
            ::dlclose(_handle);
            _handle = nullptr;
        }
    }
}
