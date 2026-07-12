// Retain-only per-thread storage for recent trace records.
#pragma once

#include "core/trace_record.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace cxbx::trace
{

template <std::size_t Capacity>
class TraceFlightRing final
{
    static_assert(Capacity > 0);

  public:
    void Record(const TraceRecord& record) noexcept
    {
        m_Records[m_WriteCount % Capacity] = record;
        ++m_WriteCount;
        if(m_Count < Capacity)
        {
            ++m_Count;
        }
    }

    [[nodiscard]] std::size_t Copy(TraceRecord* records, std::size_t capacity) const noexcept
    {
        if(records == nullptr || capacity == 0)
        {
            return 0;
        }
        const std::size_t count = m_Count < capacity ? m_Count : capacity;
        const std::uint64_t first = m_WriteCount - count;
        for(std::size_t index = 0; index < count; ++index)
        {
            records[index] = m_Records[(first + index) % Capacity];
        }
        return count;
    }

    template <typename Visitor>
    void Visit(Visitor&& visitor) const noexcept
    {
        const std::uint64_t first = m_WriteCount - m_Count;
        for(std::size_t index = 0; index < m_Count; ++index)
        {
            visitor(m_Records[(first + index) % Capacity]);
        }
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        return m_Count;
    }

  private:
    std::uint64_t m_WriteCount = 0;
    std::size_t m_Count = 0;
    std::array<TraceRecord, Capacity> m_Records{};
};

} // namespace cxbx::trace
