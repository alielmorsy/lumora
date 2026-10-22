#pragma once

#include "Api.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <mutex>
#include <type_traits>
namespace lumora
{
    /// FNV-1a 64-bit string id; `"name"_sid` hashes at compile time.
    class StringId
    {
    public:
        constexpr StringId() noexcept : _hash(0)
        {
        }

        constexpr StringId(const std::string_view str) noexcept
            : _hash(Hash(str))
        {
#ifndef NDEBUG
            if (!std::is_constant_evaluated())
            {
                if (_hash != 0)
                {
                    RegisterString(str);
                }
            }
#endif
        }


        [[nodiscard]] uint64_t Value() const noexcept { return _hash; }

        /// Rebuilds an id from a hash obtained from Value(). The string is not recovered (GetString() returns ""), but
        /// equality and map lookup still work.
        [[nodiscard]] static constexpr StringId FromHash(uint64_t hash) noexcept
        {
            StringId id;
            id._hash = hash;
            return id;
        }

        [[nodiscard]] const char* GetString() const noexcept
        {
#ifdef NDEBUG

            return "";
#else
            if (_hash == 0)
            {
                return "";
            }
            return GetStringRegistry()->GetString(_hash);
#endif
        }

        bool operator==(const StringId& other) const noexcept { return _hash == other._hash; }

        bool operator!=(const StringId& other) const noexcept { return _hash != other._hash; }
        bool operator<(const StringId& other) const noexcept { return _hash < other._hash; }

        operator uint64_t() const noexcept { return _hash; }

        [[nodiscard]] static constexpr uint64_t Hash(const std::string_view str) noexcept
        {
            uint64_t hash = 14695981039346656037ull; // FNV offset basis
            for (const char c : str)
            {
                hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
                hash *= 1099511628211ull; // FNV prime
            }
            return hash;
        }

    private:
        uint64_t _hash;

        class LUMORA_API StringRegistry
        {
        public:
            static StringRegistry* GetInstance() noexcept
            {
                static StringRegistry instance;
                return &instance;
            }

            void Register(const std::string_view str)
            {
                std::lock_guard lock(_mutex);
                const auto hash = Hash(str);
                if (!_strings.contains(hash))
                {
                    _strings[hash] = str;
                }
            }

            const char* GetString(uint64_t hash) const noexcept
            {
                std::lock_guard<std::mutex> lock(_mutex);
                auto it = _strings.find(hash);
                if (it != _strings.end())
                {
                    return it->second.c_str();
                }
                return "";
            }

        private:
            StringRegistry() = default;
            mutable std::mutex _mutex;
            std::unordered_map<uint64_t, std::string> _strings;
        };

        static StringRegistry* GetStringRegistry() noexcept
        {
            return StringRegistry::GetInstance();
        }

        static void RegisterString(const std::string_view str) noexcept
        {
            GetStringRegistry()->Register(str);
        }
    };

    struct StringIdHash
    {
        std::size_t operator()(const StringId& id) const noexcept
        {
            return static_cast<std::size_t>(id.Value());
        }
    };
} // namespace lumora

template <>
struct std::hash<lumora::StringId>
{
    std::size_t operator()(const lumora::StringId& id) const noexcept
    {
        return id.Value();
    }
};

constexpr lumora::StringId operator""_sid(const char* str, std::size_t len) noexcept
{
    return lumora::StringId(std::string_view(str, len));
}
