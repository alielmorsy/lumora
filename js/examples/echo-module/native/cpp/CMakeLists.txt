add_library(lumora_echo STATIC Echo.cpp)
target_link_libraries(lumora_echo PUBLIC lumora::script)
