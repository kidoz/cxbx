// Process-level shared-runtime access without exposing stored configuration types.
#pragma once

#include <cstddef>
#include <cstdint>

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

// Largest free span below the 64 MiB Xbox RAM line that the launcher reserved
// in the guest process. Contiguous "physical" guest memory is committed from
// this window, which keeps host == Xbox physical in the low 28 address bits --
// the invariant the NV2A DMA engine relies on when a title programs the
// pushbuffer with masked-physical addresses.
void GetSharedContiguousWindow(std::uint32_t& base, std::uint32_t& size);
void SetSharedContiguousWindow(std::uint32_t base, std::uint32_t size);

} // namespace cxbx::platform
