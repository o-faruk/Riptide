#include "riptide/version.hpp"

#include <gtest/gtest.h>

#include <string_view>

// Phase 0 sanity check: proves include/ -> src/ -> tests/ wiring works
// end to end. Real coverage starts in Phase 1.

TEST(Version, StringMatchesVersionConstants) {
  EXPECT_EQ(std::string_view(riptide::version_string()), "0.1.0");
}

TEST(Version, MajorVersionIsZero) { EXPECT_EQ(riptide::kVersionMajor, 0); }
