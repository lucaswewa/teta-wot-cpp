# Writes OUTPUT, a C++ source holding the bytes of INPUT as SYMBOL (see
# TetaWotEmbed.cmake). Run with `cmake -P`.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
# Line breaks every 32 bytes keep the file readable for compilers and people.
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){32})" "\\1\n" bytes "${bytes}")
file(WRITE "${OUTPUT}" "// Generated from ${INPUT}. Do not edit.\n#include <cstddef>\n\nnamespace teta::wot::embedded {\n\nextern const unsigned char ${SYMBOL}[];\nextern const std::size_t ${SYMBOL}_size;\n\nconst unsigned char ${SYMBOL}[] = {\n${bytes}\n0x00};\nconst std::size_t ${SYMBOL}_size = ${size};\n\n}  // namespace teta::wot::embedded\n")
