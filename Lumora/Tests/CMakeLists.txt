include(${PROJECT_SOURCE_DIR}/ThirdParty/add_googletest.cmake)
include(GoogleTest)

function(_lumora_test name)
    target_link_libraries(${name} PRIVATE lumora::lumora lumora::log GTest::gtest GTest::gtest_main ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
    lumora_defaults(${name})
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:${name}>>,copy_if_different,true>
            $<TARGET_RUNTIME_DLLS:${name}> $<TARGET_FILE_DIR:${name}>
        COMMAND_EXPAND_LISTS)
endfunction()

add_executable(lumora_tests
    ReactiveTests.cpp
    NodeTests.cpp
    WidgetTests.cpp
    LayoutTests.cpp
    ControlFlowTests.cpp
    VirtualListTests.cpp
    DragDropTests.cpp
    DoubleClickTests.cpp
    TextSelectionTests.cpp
    NumberFieldTests.cpp
    FocusTests.cpp
    InputRoutingTests.cpp
    AnimationTests.cpp
    PaintTests.cpp
    ImageTests.cpp
    CountersTests.cpp
    DockDetachTests.cpp
)
_lumora_test(lumora_tests)
gtest_discover_tests(lumora_tests)

add_executable(lumora_style_tests
    StyleParserTests.cpp
    StyleMatchTests.cpp
    StyleApplyTests.cpp
    TransformTests.cpp
    StylePseudoTests.cpp
    StyleInheritVarsMediaTests.cpp
    StyleTransitionTests.cpp
    StyleKeyframesTests.cpp
)
_lumora_test(lumora_style_tests)
gtest_discover_tests(lumora_style_tests)

add_executable(lumora_graph_tests
    GraphPainterRepaintTests.cpp
    GraphCameraTests.cpp
    GraphTypesTests.cpp
    GraphModelTests.cpp
    GraphLayoutTests.cpp
    GraphHistoryTests.cpp
    GraphControllerTests.cpp
    GraphPaintTests.cpp
    GraphInputTests.cpp
    GraphWidgetTests.cpp
    GraphSerializeTests.cpp
    GraphFlowTests.cpp
)
_lumora_test(lumora_graph_tests)
gtest_discover_tests(lumora_graph_tests)

add_executable(lumora_batch_tests BatchTests.cpp PaintGeometryTests.cpp ../Renderer/Source/BatchBuilder.cpp)
_lumora_test(lumora_batch_tests)
target_include_directories(lumora_batch_tests PRIVATE ../Renderer/Source)
gtest_discover_tests(lumora_batch_tests)

if (TARGET lumora_renderer)
    add_executable(lumora_shared_image_tests SharedImageTests.cpp)
    target_link_libraries(lumora_shared_image_tests PRIVATE lumora::renderer lumora_rhi GTest::gtest GTest::gtest_main)
    lumora_defaults(lumora_shared_image_tests)
    add_custom_command(TARGET lumora_shared_image_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:lumora_shared_image_tests>>,copy_if_different,true>
            $<TARGET_RUNTIME_DLLS:lumora_shared_image_tests> $<TARGET_FILE_DIR:lumora_shared_image_tests>
        COMMAND_EXPAND_LISTS)
    gtest_discover_tests(lumora_shared_image_tests)
endif ()

add_executable(LumoraBench Bench.cpp BenchScenes.cpp)
target_link_libraries(LumoraBench PRIVATE lumora::lumora lumora::log lumora::text)
target_include_directories(LumoraBench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Source)
lumora_defaults(LumoraBench)
add_custom_command(TARGET LumoraBench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<BOOL:$<TARGET_RUNTIME_DLLS:LumoraBench>>,copy_if_different,true>
        $<TARGET_RUNTIME_DLLS:LumoraBench> $<TARGET_FILE_DIR:LumoraBench>
    COMMAND_EXPAND_LISTS)
set(_bench_font "${CMAKE_CURRENT_SOURCE_DIR}/../ThirdParty/harfbuzz/perf/fonts/Roboto-Regular.ttf")
if (EXISTS "${_bench_font}")
    target_compile_definitions(LumoraBench PRIVATE LUMORA_BENCH_FONT="${_bench_font}")
endif ()
