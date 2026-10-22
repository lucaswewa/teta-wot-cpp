# The C++ twin server, for conformance/run.py (ADR-0011).
add_executable(twin-server main.cpp)
target_link_libraries(twin-server PRIVATE teta_wot_twins teta::wot teta_wot_warnings)
