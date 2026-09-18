# EmbedSpirv.cmake — convert a binary file into a C++ header with a byte array.
# Usage: cmake -DINPUT=path/to/file.spv -DOUTPUT=path/to/out.hpp -DSYMBOL=symbol_name -P EmbedSpirv.cmake

if(NOT INPUT OR NOT OUTPUT OR NOT SYMBOL)
    message(FATAL_ERROR "EmbedSpirv.cmake requires INPUT, OUTPUT, and SYMBOL")
endif()

file(READ "${INPUT}" _hex HEX)
string(LENGTH "${_hex}" _hex_len)
set(_bytes "")
math(EXPR _count "${_hex_len} / 2")
set(_i 0)
while(_i LESS _count)
    math(EXPR _off "${_i} * 2")
    string(SUBSTRING "${_hex}" ${_off} 2 _byte)
    if(_i EQUAL 0)
        set(_bytes "0x${_byte}")
    else()
        set(_bytes "${_bytes}, 0x${_byte}")
    endif()
    math(EXPR _i "${_i} + 1")
endwhile()

file(WRITE "${OUTPUT}" "// Auto-generated — do not edit
#pragma once
#include \"core/Types.hpp\"

namespace Caffeine::Render::Shaders {

inline constexpr u8 ${SYMBOL}[] = { ${_bytes} };
inline constexpr usize ${SYMBOL}Size = sizeof(${SYMBOL});

}  // namespace Caffeine::Render::Shaders
")
