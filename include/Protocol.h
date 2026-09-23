#pragma once

#include <Arduino.h>

namespace Protocol
{
    constexpr uint8_t VERSION = 1;

    enum class MessageType : uint8_t
    {
        Event = 1,
        Ack   = 2,
        SleepRequest = 3,
        SleepReady   = 4,
        SleepCommit  = 5,
        SleepAck     = 6,
        SleepCancel  = 7
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
        // Ack: packet being acknowledged. Sleep controls: request's messageId
        // (sleepId). SleepRequest repeats its own messageId here. EVENT stays 0.
        uint16_t ackForMessageId;
    };

    static_assert(
        sizeof(Message) == 8,
        "Protocol::Message must be exactly 8 bytes"
    );

    inline bool isSleepControl(MessageType type)
    {
        return type >= MessageType::SleepRequest && type <= MessageType::SleepCancel;
    }

    inline const char* controlName(MessageType type)
    {
        switch (type)
        {
            case MessageType::SleepRequest: return "SLEEP_REQUEST";
            case MessageType::SleepReady: return "SLEEP_READY";
            case MessageType::SleepCommit: return "SLEEP_COMMIT";
            case MessageType::SleepAck: return "SLEEP_ACK";
            case MessageType::SleepCancel: return "SLEEP_CANCEL";
            default: return "NOT_SLEEP_CONTROL";
        }
    }
}
