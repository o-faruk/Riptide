# Phase 1 will add matching_engine.cpp, etc. to this list.
add_library(riptide_core
  version.cpp
  order_book.cpp
)

target_include_directories(riptide_core
  PUBLIC ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(riptide_core PUBLIC cxx_std_20)
target_link_libraries(riptide_core PRIVATE riptide_warnings)
