add_executable(riptide_validate
  main.cpp
  lobster_message.cpp
  lobster_book.cpp
  adapter.cpp
  comparator.cpp
)

target_link_libraries(riptide_validate
  PRIVATE
    riptide_core
    riptide_warnings
)
