#pragma once
#include <cstring>
#include <deque>
#include <vector>

struct HostQueue
{
    size_t capacity;
    size_t itemSize;
    std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = HostQueue*;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t size)
{
    return new HostQueue{capacity, size, {}};
}
inline int xQueueSend(QueueHandle_t queue, const void* data, unsigned wait)
{
    assert(wait == 0);
    if (queue->items.size() == queue->capacity)
        return 0;
    const auto* bytes = static_cast<const uint8_t*>(data);
    queue->items.emplace_back(bytes, bytes + queue->itemSize);
    return pdPASS;
}
inline int xQueueReceive(QueueHandle_t queue, void* data, unsigned wait)
{
    assert(wait == 0);
    if (queue->items.empty())
        return 0;
    memcpy(data, queue->items.front().data(), queue->itemSize);
    queue->items.pop_front();
    return pdPASS;
}

inline size_t uxQueueMessagesWaiting(QueueHandle_t queue) { return queue->items.size(); }
