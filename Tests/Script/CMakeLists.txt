lumora_add_test(lumora_script_tests SOURCES HermesSmokeTests.cpp JsAppTests.cpp NativeModuleTests.cpp LINK lumora::script)
# The tests hold UTF-8 literals.
target_compile_options(lumora_script_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
