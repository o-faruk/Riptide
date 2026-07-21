# A small, honest demo CLI: runs one example Strategy (see
# rest_at_best_bid_strategy.hpp) against real LOBSTER data through
# Backtester<ReferenceEngine>. Named riptide_backtest_cli, distinct from
# the header-only riptide_backtest library target it links against.
add_executable(riptide_backtest_cli
  main.cpp
)

target_link_libraries(riptide_backtest_cli
  PRIVATE
    riptide_backtest  # brings in riptide_core + riptide_lobster
    riptide_warnings
)
