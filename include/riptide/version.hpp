#pragma once

namespace riptide {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// Returns "MAJOR.MINOR.PATCH", matching the k*Version* constants above.
const char* version_string();

}  // namespace riptide
