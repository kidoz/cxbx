// Concurrent retain-only storage for crash-context records.
#pragma once

#include "core/trace_record.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace cxbx::trace
{
namespace detail
{

struct AtomicTraceSlot
{
    std::atomic<std::uint32_t> generation{ 0 };
    std::atomic<std::uint32_t> tickLow{ 0 };
    std::atomic<std::uint32_t> tickHigh{ 0 };
    std::atomic<std::uint32_t> sequenceLow{ 0 };
    std::atomic<std::uint32_t> sequenceHigh{ 0 };
    std::atomic<std::uint32_t> eventAndThread{ 0 };
    std::atomic<std::uint32_t> argument{ 0 };
};

inline void Publish(AtomicTraceSlot& slot, std::uint64_t ticket,
                    const TraceRecord& record) noexcept
{
    const std::uint32_t generation = static_cast<std::uint32_t>(ticket * 2);
    slot.generation.store(generation + 1, std::memory_order_release);
    slot.tickLow.store(static_cast<std::uint32_t>(record.tick), std::memory_order_relaxed);
    slot.tickHigh.store(static_cast<std::uint32_t>(record.tick >> 32),
                        std::memory_order_relaxed);
    slot.sequenceLow.store(static_cast<std::uint32_t>(record.localSequence),
                           std::memory_order_relaxed);
    slot.sequenceHigh.store(static_cast<std::uint32_t>(record.localSequence >> 32),
                            std::memory_order_relaxed);
    slot.eventAndThread.store(static_cast<std::uint32_t>(record.event) |
                                  (static_cast<std::uint32_t>(record.threadIndex) << 16),
                              std::memory_order_relaxed);
    slot.argument.store(record.argument, std::memory_order_relaxed);
    slot.generation.store(generation + 2, std::memory_order_release);
}

[[nodiscard]] inline bool Read(const AtomicTraceSlot& slot, std::uint64_t ticket,
                               TraceRecord& record) noexcept
{
    const std::uint32_t expected = static_cast<std::uint32_t>(ticket * 2) + 2;
    if(slot.generation.load(std::memory_order_acquire) != expected)
    {
        return false;
    }

    const std::uint32_t tickLow = slot.tickLow.load(std::memory_order_relaxed);
    const std::uint32_t tickHigh = slot.tickHigh.load(std::memory_order_relaxed);
    const std::uint32_t sequenceLow = slot.sequenceLow.load(std::memory_order_relaxed);
    const std::uint32_t sequenceHigh = slot.sequenceHigh.load(std::memory_order_relaxed);
    const std::uint32_t eventAndThread =
        slot.eventAndThread.load(std::memory_order_relaxed);
    const std::uint32_t argument = slot.argument.load(std::memory_order_relaxed);

    if(slot.generation.load(std::memory_order_acquire) != expected)
    {
        return false;
    }

    record = {
        (static_cast<std::uint64_t>(tickHigh) << 32) | tickLow,
        (static_cast<std::uint64_t>(sequenceHigh) << 32) | sequenceLow,
        static_cast<std::uint16_t>(eventAndThread),
        static_cast<std::uint16_t>(eventAndThread >> 16),
        argument,
    };
    return true;
}

template <std::size_t Capacity, typename Visitor>
void VisitSnapshot(const std::array<AtomicTraceSlot, Capacity>& slots, std::uint64_t end,
                   Visitor&& visitor) noexcept
{
    const std::uint64_t count = end < Capacity ? end : Capacity;
    const std::uint64_t first = end - count;
    for(std::uint64_t ticket = first; ticket < end; ++ticket)
    {
        TraceRecord record{};
        if(Read(slots[ticket % Capacity], ticket, record))
        {
            visitor(record);
        }
    }
}

} // namespace detail

template <std::size_t Capacity>
class TraceConcurrentFlightRing final
{
    static_assert(Capacity > 0);

  public:
    void Record(const TraceRecord& record) noexcept
    {
        const std::uint64_t ticket = m_WriteCount.load(std::memory_order_relaxed);
        detail::Publish(m_Slots[ticket % Capacity], ticket, record);
        m_WriteCount.store(ticket + 1, std::memory_order_release);
    }

    template <typename Visitor>
    void Visit(Visitor&& visitor) const noexcept
    {
        detail::VisitSnapshot(m_Slots, m_WriteCount.load(std::memory_order_acquire),
                              static_cast<Visitor&&>(visitor));
    }

    [[nodiscard]] std::size_t Copy(TraceRecord* records, std::size_t capacity) const noexcept
    {
        if(records == nullptr || capacity == 0)
        {
            return 0;
        }
        const std::uint64_t end = m_WriteCount.load(std::memory_order_acquire);
        const std::uint64_t retained = end < Capacity ? end : Capacity;
        const std::uint64_t count = retained < capacity ? retained : capacity;
        const std::uint64_t first = end - count;
        std::size_t copied = 0;
        for(std::uint64_t ticket = first; ticket < end; ++ticket)
        {
            TraceRecord record{};
            if(detail::Read(m_Slots[ticket % Capacity], ticket, record))
            {
                records[copied++] = record;
            }
        }
        return copied;
    }

  private:
    std::atomic<std::uint64_t> m_WriteCount{ 0 };
    std::array<detail::AtomicTraceSlot, Capacity> m_Slots{};
};

template <std::size_t Capacity>
class TraceMpscFlightRing final
{
    static_assert(Capacity > 0);

  public:
    void Record(TraceRecord record) noexcept
    {
        const std::uint64_t ticket = m_NextTicket.fetch_add(1, std::memory_order_relaxed);
        record.localSequence = ticket + 1;
        if(ticket >= Capacity)
        {
            m_Overwritten.fetch_add(1, std::memory_order_relaxed);
        }
        detail::Publish(m_Slots[ticket % Capacity], ticket, record);
    }

    template <typename Visitor>
    void Visit(Visitor&& visitor) const noexcept
    {
        detail::VisitSnapshot(m_Slots, m_NextTicket.load(std::memory_order_acquire),
                              static_cast<Visitor&&>(visitor));
    }

    [[nodiscard]] std::size_t Copy(TraceRecord* records, std::size_t capacity) const noexcept
    {
        if(records == nullptr || capacity == 0)
        {
            return 0;
        }
        const std::uint64_t end = m_NextTicket.load(std::memory_order_acquire);
        const std::uint64_t retained = end < Capacity ? end : Capacity;
        const std::uint64_t count = retained < capacity ? retained : capacity;
        const std::uint64_t first = end - count;
        std::size_t copied = 0;
        for(std::uint64_t ticket = first; ticket < end; ++ticket)
        {
            TraceRecord record{};
            if(detail::Read(m_Slots[ticket % Capacity], ticket, record))
            {
                records[copied++] = record;
            }
        }
        return copied;
    }

    [[nodiscard]] std::uint64_t Overwritten() const noexcept
    {
        return m_Overwritten.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<std::uint64_t> m_NextTicket{ 0 };
    std::atomic<std::uint64_t> m_Overwritten{ 0 };
    std::array<detail::AtomicTraceSlot, Capacity> m_Slots{};
};

} // namespace cxbx::trace
