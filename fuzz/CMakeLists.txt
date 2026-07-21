# Shared by tests/differential_test.cpp and the fuzz target below: the
# random operation-sequence generator has zero fuzzer-specific
# dependencies, so it's a plain static library, always built.
add_library(riptide_fuzz_support
  random_sequence.cpp
)
target_include_directories(riptide_fuzz_support
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(riptide_fuzz_support
  PUBLIC riptide_core
  PRIVATE riptide_warnings
)

# Compiles by default as a plain, portable executable (works with any
# compiler, including AppleClang on this project's macOS dev machine,
# which lacks the libFuzzer runtime) — that's what lets it be smoke-
# tested everywhere. For REAL coverage-guided fuzzing, build with
# mainline Clang and -DRIPTIDE_FUZZ_WITH_LIBFUZZER=ON (see
# docs/DESIGN.md).
option(RIPTIDE_FUZZ_WITH_LIBFUZZER
  "Build the fuzz target with -fsanitize=fuzzer (needs mainline Clang)" OFF)

add_executable(riptide_fuzz_target
  fuzz_target.cpp
)
target_include_directories(riptide_fuzz_target
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(riptide_fuzz_target
  PRIVATE
    riptide_core
    riptide_warnings
)

if(RIPTIDE_FUZZ_WITH_LIBFUZZER)
  target_compile_definitions(riptide_fuzz_target PRIVATE RIPTIDE_FUZZ_TARGET_LIBFUZZER_MAIN)
  target_compile_options(riptide_fuzz_target PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(riptide_fuzz_target PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
