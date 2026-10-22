#pragma once

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace lumora
{
    /// std::from_chars for float; falls back to strtof where the standard library lacks it (NDK libc++ before 20).
    inline std::from_chars_result FromChars(const char* first, const char* last, float& value) noexcept
    {
#if defined(__cpp_lib_to_chars)
        return std::from_chars(first, last, value);
#else
        // Match from_chars: no leading whitespace, '+' or hex.
        const char* digits = first != last && *first == '-' ? first + 1 : first;
        if (first == last || std::strchr(" \t\n\v\f\r+", *first) || (last - digits > 1 && digits[0] == '0' && (digits[1] | 0x20) == 'x'))
            return {first, std::errc::invalid_argument};

        char buffer[64];
        const size_t length = static_cast<size_t>(last - first) < sizeof(buffer) - 1 ? static_cast<size_t>(last - first) : sizeof(buffer) - 1;
        std::memcpy(buffer, first, length);
        buffer[length] = '\0';

        char* end = nullptr;
        errno = 0;
        const float parsed = std::strtof(buffer, &end);
        if (end == buffer)
            return {first, std::errc::invalid_argument};
        if (errno == ERANGE)
            return {first + (end - buffer), std::errc::result_out_of_range};
        value = parsed;
        return {first + (end - buffer), std::errc{}};
#endif
    }
}
