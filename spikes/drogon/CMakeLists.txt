# The Phase 0 Drogon spike (docs/notes/phase-00-baseline.md). Not installed.
add_executable(drogon-spike main.cpp)
target_link_libraries(drogon-spike PRIVATE Drogon::Drogon)
set_target_properties(drogon-spike PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
