add_executable(riptide_validate
  main.cpp
)

target_link_libraries(riptide_validate
  PRIVATE
    riptide_lobster  # transitively brings in riptide_core (PUBLIC link)
    riptide_warnings
)
