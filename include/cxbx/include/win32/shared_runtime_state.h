// Process-level shared-runtime access without exposing stored configuration types.
#pragma once

#include <cstddef>

namespace cxbx::platform
{

inline constexpr std::size_t kSharedXbePathCapacity = 260;

void InitializeSharedRuntime();
void ShutdownSharedRuntime();
// Guest process only: keep shutdown from persisting configuration (registry
// writes are the launcher's job, and title code may have stomped this
// process's static data by exit time).
void DisableSharedRuntimePersist();
void GetSharedXbePath(char (&path)[kSharedXbePathCapacity]);
void SetSharedXbePath(const char (&path)[kSharedXbePathCapacity]);

} // namespace cxbx::platform
