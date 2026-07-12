// Bounded single-producer/single-consumer ring for fixed trace records.
#pragma once

#include "core/trace_record.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cxbx::trace
{

template <std::size_t Capacity>
class TraceSpscRing final
{
    static_assert(Capacity >= 2);
    static_assert(Capacity <= std::numeric_limits<std::uint16_t>::max());
    static_assert((Capacity & (Capacity - 1)) == 0,
                  "Trace ring capacity must be a power of two");

  public:
    TraceSpscRing() noexcept = default;
    TraceSpscRing(const TraceSpscRing&) = delete;
    TraceSpscRing& operator=(const TraceSpscRing&) = delete;

    [[nodiscard]] bool Push(const TraceRecord* records, std::size_t count) noexcept
    {
        if(records == nullptr || count == 0 || count > Capacity)
        {
            m_DroppedEvents.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        const std::uint64_t head = m_Head.load(std::memory_order_relaxed);
        const std::uint64_t tail = m_Tail.load(std::memory_order_acquire);
        if(count > Capacity - static_cast<std::size_t>(head - tail))
        {
            m_DroppedEvents.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        for(std::size_t index = 0; index < count; ++index)
        {
            m_Records[(head + index) & (Capacity - 1)] = records[index];
            m_GroupLengths[(head + index) & (Capacity - 1)] = 0;
        }
        m_GroupLengths[head & (Capacity - 1)] = static_cast<std::uint16_t>(count);
        m_Head.store(head + count, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::size_t Pop(TraceRecord* records, std::size_t capacity) noexcept
    {
        if(records == nullptr || capacity == 0)
        {
            return 0;
        }

        const std::uint64_t tail = m_Tail.load(std::memory_order_relaxed);
        const std::uint64_t head = m_Head.load(std::memory_order_acquire);
        const std::size_t available = static_cast<std::size_t>(head - tail);
        std::size_t count = 0;
        while(count < available)
        {
            const std::size_t groupLength = m_GroupLengths[(tail + count) & (Capacity - 1)];
            if(groupLength == 0 || groupLength > available - count ||
               groupLength > capacity - count)
            {
                break;
            }
            count += groupLength;
        }
        for(std::size_t index = 0; index < count; ++index)
        {
            records[index] = m_Records[(tail + index) & (Capacity - 1)];
        }
        m_Tail.store(tail + count, std::memory_order_release);
        return count;
    }

    [[nodiscard]] std::size_t Size() const noexcept
    {
        const std::uint64_t head = m_Head.load(std::memory_order_acquire);
        const std::uint64_t tail = m_Tail.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

    [[nodiscard]] std::uint64_t TakeDroppedEvents() noexcept
    {
        return m_DroppedEvents.exchange(0, std::memory_order_acq_rel);
    }

  private:
    alignas(64) std::atomic<std::uint64_t> m_Head{ 0 };
    alignas(64) std::atomic<std::uint64_t> m_Tail{ 0 };
    alignas(64) std::atomic<std::uint64_t> m_DroppedEvents{ 0 };
    std::array<TraceRecord, Capacity> m_Records{};
    std::array<std::uint16_t, Capacity> m_GroupLengths{};
};

} // namespace cxbx::trace
