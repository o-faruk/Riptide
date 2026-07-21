# Header-only: Backtester<Engine> depends on both the core engine and
# LOBSTER's Adapter (Phase 2), so it can't live inside riptide_core
# (which has no LOBSTER dependency) — this is its own small library so
# that dependency is explicit and doesn't leak into riptide_core.
add_library(riptide_backtest INTERFACE)

target_include_directories(riptide_backtest
  INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(riptide_backtest
  INTERFACE
    riptide_core
    riptide_lobster
)
