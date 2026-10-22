teta_wot_add_example(drogon-integration SOURCES main.cpp)
# <teta/wot/drogon.hpp> needs Drogon's headers.
target_link_libraries(example-drogon-integration PRIVATE teta::wot-drogon)
