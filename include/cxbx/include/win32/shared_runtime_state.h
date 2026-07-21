// Process-level shared-runtime access without exposing stored configuration types.
#pragma once

#include <cstddef>

namespace cxbx::platform
{

inline constexpr std::size_t kSharedXbePathCapacity = 260;

void InitializeSharedRuntime();
void ShutdownSharedRuntime();
void GetSharedXbePath(char (&path)[kSharedXbePathCapacity]);
void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity]);

} // namespace cxbx::platform
