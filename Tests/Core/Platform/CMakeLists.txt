add_executable(lumora_platform_tests
    WindowGeometryTests.cpp
    DirectoryWatcherTests.cpp
)

lumora_defaults(lumora_platform_tests)
target_link_libraries(lumora_platform_tests PRIVATE lumora::platform lumora::log GTest::gtest GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lumora_platform_tests)
