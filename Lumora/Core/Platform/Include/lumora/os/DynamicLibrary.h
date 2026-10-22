#pragma once


#include <filesystem>
#include <optional>

namespace lumora::os
{
    // RAII wrapper over a loaded dynamic library (LoadLibrary / dlopen). Move-only; unloads on
    // destruction. Symbols resolved from it are valid only while the owning instance is alive.
    class DynamicLibrary
    {
    public:
        DynamicLibrary() = default;
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;

        DynamicLibrary(DynamicLibrary&& other) noexcept : _handle(other._handle) { other._handle = nullptr; }

        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept
        {
            if (this != &other)
            {
                Close();
                _handle = other._handle;
                other._handle = nullptr;
            }
            return *this;
        }

        ~DynamicLibrary() { Close(); }

        [[nodiscard]] static std::optional<DynamicLibrary> Open(const std::filesystem::path& path);

        [[nodiscard]] void* RawSymbol(const char* name) const;

        template <typename Fn>
        [[nodiscard]] Fn Symbol(const char* name) const
        {
            return reinterpret_cast<Fn>(RawSymbol(name));
        }

        [[nodiscard]] explicit operator bool() const noexcept { return _handle != nullptr; }

    private:
        explicit DynamicLibrary(void* handle) noexcept : _handle(handle)
        {
        }

        void Close() noexcept;

        void* _handle = nullptr;
    };
}
