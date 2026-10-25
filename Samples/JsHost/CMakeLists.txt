if (NOT TARGET lumora::script)
    return()
endif ()

add_library(lumora_js_host STATIC JsHost.cpp)
target_include_directories(lumora_js_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lumora_js_host PUBLIC lumora_sample_host PRIVATE lumora::script)
lumora_defaults(lumora_js_host)

lumora_add_sample(LumoraJsHost main.cpp)
target_link_libraries(LumoraJsHost PRIVATE lumora_js_host)

if (LUMORA_BUILD_TESTS AND NOT ANDROID AND NOT EMSCRIPTEN)
    add_test(NAME LumoraJsHost.hello COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:LumoraJsHost> -DBUNDLE=${CMAKE_CURRENT_SOURCE_DIR}/test/hello.js
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/hello.txt -DEXPECT=exact "-DTEXT=Count: 1"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check.cmake)
    add_test(NAME LumoraJsHost.throws COMMAND ${CMAKE_COMMAND}
        -DEXE=$<TARGET_FILE:LumoraJsHost> -DBUNDLE=${CMAKE_CURRENT_SOURCE_DIR}/test/throws.js
        -DOUT=${CMAKE_CURRENT_BINARY_DIR}/throws.txt -DEXPECT=contains -DTEXT=boom
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check.cmake)
endif ()
