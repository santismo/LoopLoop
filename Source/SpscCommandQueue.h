#pragma once

#include <array>
#include <atomic>

template <typename Item, size_t Capacity>
class SpscCommandQueue
{
public:
    bool push (const Item& item) noexcept
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);
        const auto next = increment (write);

        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        buffer[write] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    bool pop (Item& item) noexcept
    {
        const auto read = readIndex.load (std::memory_order_relaxed);

        if (read == writeIndex.load (std::memory_order_acquire))
            return false;

        item = buffer[read];
        readIndex.store (increment (read), std::memory_order_release);
        return true;
    }

    void reset() noexcept
    {
        readIndex.store (0, std::memory_order_relaxed);
        writeIndex.store (0, std::memory_order_relaxed);
    }

private:
    static size_t increment (size_t value) noexcept { return (value + 1) % Capacity; }

    static_assert (Capacity > 1);
    std::array<Item, Capacity> buffer {};
    std::atomic<size_t> readIndex { 0 };
    std::atomic<size_t> writeIndex { 0 };
};
