#include <lumora/os/DynamicLibrary.h>

#include <lumora/log/Log.h>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <windows.h>

LUMORA_LOG_CATEGORY("OSCore")

namespace lumora::os
{
    std::optional<DynamicLibrary> DynamicLibrary::Open(const std::filesystem::path& path)
    {
        const HMODULE handle = ::LoadLibraryW(path.wstring().c_str());
        if (!handle)
        {
            LOG_ERROR("OSCore::DynamicLibrary: LoadLibrary failed ({}) for {}", ::GetLastError(), path.string());
            return std::nullopt;
        }
        return DynamicLibrary(reinterpret_cast<void*>(handle));
    }

    void* DynamicLibrary::RawSymbol(const char* name) const
    {
        if (!_handle) return nullptr;
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(_handle), name));
    }

    void DynamicLibrary::Close() noexcept
    {
        if (_handle)
        {
            ::FreeLibrary(static_cast<HMODULE>(_handle));
            _handle = nullptr;
        }
    }
}
