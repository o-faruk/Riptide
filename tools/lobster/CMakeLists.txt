# Shared by tools/validate (correctness) and bench/ (performance) — both
# need to parse LOBSTER's message/orderbook CSV format and translate
# messages into MatchingEngine calls, and that logic shouldn't be
# duplicated between them.
add_library(riptide_lobster
  lobster_message.cpp
  lobster_book.cpp
  adapter.cpp
)

target_include_directories(riptide_lobster
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(riptide_lobster
  PUBLIC riptide_core
  PRIVATE riptide_warnings
)
