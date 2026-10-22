# Embeds all .spv files in INPUT_DIR into C++ source and header files.
if (NOT DEFINED INPUT_DIR OR NOT DEFINED OUTPUT_HEADER OR NOT DEFINED OUTPUT_SOURCE)
    message(FATAL_ERROR "INPUT_DIR, OUTPUT_HEADER, and OUTPUT_SOURCE must be defined")
endif ()

file(GLOB _files LIST_DIRECTORIES false "${INPUT_DIR}/*.spv")
list(SORT _files)

set(_entries "")
set(_lookups "")

foreach (_file ${_files})
    get_filename_component(_name "${_file}" NAME)
    string(MAKE_C_IDENTIFIER "${_name}" _var)
    file(READ "${_file}" _hex HEX)

    string(LENGTH "${_hex}" _len)
    set(_idx 0)
    set(_bytes "")
    while (_idx LESS _len)
        math(EXPR _rem "${_len} - ${_idx}")
        if (_rem GREATER 32)
            set(_chunk_len 32)
        else ()
            set(_chunk_len ${_rem})
        endif ()
        string(SUBSTRING "${_hex}" ${_idx} ${_chunk_len} _chunk)
        string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1, " _chunk_bytes "${_chunk}")
        string(APPEND _bytes "    ${_chunk_bytes}\n")
        math(EXPR _idx "${_idx} + ${_chunk_len}")
    endwhile ()

    string(APPEND _entries "alignas(4) static const unsigned char k${_var}[] = {\n${_bytes}};\n\n")
    string(APPEND _lookups "    if (name == \"${_name}\") return {reinterpret_cast<const std::byte*>(k${_var}), sizeof(k${_var})};\n")
endforeach ()

set(_header_content "#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace lumora {
std::span<const std::byte> EmbeddedSpirv(std::string_view name);
}
")

set(_source_content "#include \"EmbeddedShaders.h\"

namespace lumora {
namespace {
${_entries}} // namespace

std::span<const std::byte> EmbeddedSpirv(std::string_view name)
{
${_lookups}    return {};
}
} // namespace lumora
")

file(WRITE "${OUTPUT_HEADER}" "${_header_content}")
file(WRITE "${OUTPUT_SOURCE}" "${_source_content}")
