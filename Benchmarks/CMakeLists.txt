add_executable(LumoraBench Bench.cpp BenchScenes.cpp)
target_link_libraries(LumoraBench PRIVATE lumora::lumora lumora::log lumora::text)
target_include_directories(LumoraBench PRIVATE
    ${LUMORA_ROOT_DIR}/Lumora/UI/Source
    ${LUMORA_ROOT_DIR}/Tests/UI)
lumora_defaults(LumoraBench)
lumora_copy_runtime_dlls(LumoraBench)

set(_bench_font "${LUMORA_ROOT_DIR}/ThirdParty/harfbuzz/perf/fonts/Roboto-Regular.ttf")
if (EXISTS "${_bench_font}")
    target_compile_definitions(LumoraBench PRIVATE LUMORA_BENCH_FONT="${_bench_font}")
endif ()
