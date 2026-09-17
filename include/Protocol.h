#pragma once

#include <Arduino.h>

namespace Protocol
{
    constexpr uint8_t VERSION = 1;

    enum class MessageType : uint8_t
    {
        Event = 1,
        Ack   = 2
    };

    enum class DeviceId : uint8_t
    {
        Bubu = 1,
        Dudu = 2
    };

    enum class EventType : uint8_t
    {
        None      = 0,
        Heartbeat = 1
    };

    struct __attribute__((packed)) Message
    {
        uint8_t version;
        MessageType type;
        uint16_t messageId;
        DeviceId sender;
        EventType event;
        uint16_t ackForMessageId;
    };

    static_assert(
        sizeof(Message) == 8,
        "Protocol::Message must be exactly 8 bytes"
    );
}