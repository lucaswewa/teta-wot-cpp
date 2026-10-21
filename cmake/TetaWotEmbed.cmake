# teta_wot_embed(<target> <file> <symbol>)
#
# Compiles <file> into <target> as `extern const unsigned char <symbol>[]`
# and `extern const std::size_t <symbol>_size`, in namespace
# teta::wot::embedded. A byte array rather than a string literal, so any size
# compiles on MSVC (whose string literals are limited to 64 KB); C++20 has no
# `#embed`. The array is regenerated whenever <file> changes.
function(teta_wot_embed target file symbol)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${file} -DOUTPUT=${output} -DSYMBOL=${symbol}
                -P "${PROJECT_SOURCE_DIR}/cmake/TetaWotEmbedFile.cmake"
        DEPENDS "${file}" "${PROJECT_SOURCE_DIR}/cmake/TetaWotEmbedFile.cmake"
        COMMENT "Embedding ${file}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
endfunction()
