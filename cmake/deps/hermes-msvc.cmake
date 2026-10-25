# Runs after project(Hermes) (CMAKE_PROJECT_Hermes_INCLUDE). Hermes V1 tags call __builtin_expect
# without a guard in include/hermes/VM/static_h.h (fixed on main), and MSVC has no such builtin.
file(WRITE "${CMAKE_BINARY_DIR}/msvc_builtin_expect.h" "#define __builtin_expect(e, v) (e)\n")
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_BINARY_DIR}/msvc_builtin_expect.h>")
