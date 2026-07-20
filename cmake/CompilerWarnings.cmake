# INTERFACE target so warning flags are opt-in per target rather than global.
# Global CMAKE_CXX_FLAGS would also apply -Werror to fetched third-party
# targets (GoogleTest, Google Benchmark) whose headers we don't control and
# can't guarantee are warning-clean under our flags.
add_library(riptide_warnings INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(riptide_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
  )
endif()
