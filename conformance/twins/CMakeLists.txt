# The twin Things in C++ (conformance/reference-rs's twins, ported).
add_library(teta_wot_twins STATIC twins.cpp)
target_include_directories(teta_wot_twins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(teta_wot_twins PUBLIC teta::wot PRIVATE teta_wot_warnings)
