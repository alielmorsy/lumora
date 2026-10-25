add_library(lumora_{{lower}} STATIC {{Name}}.cpp)
target_link_libraries(lumora_{{lower}} PUBLIC lumora::script)
