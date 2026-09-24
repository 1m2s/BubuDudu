#pragma once
#include "Protocol.h"

// Manual, awake-board bench operation. No ID allocation or power policy here.
namespace CC1101WakeTx
{
    enum class Result { Acked, RadioUnavailable, Busy, TxFailed, AckTimeout, InvalidAck };
    struct Report
    {
        Result result = Result::RadioUnavailable;
        uint8_t attempts = 0;
        bool rxReady = false; // Independent of whether the peer acknowledged.
    };
    // Synchronous and bounded: three attempts, 300 ms ACK wait each, at most
    // one RX recovery. Existing FIFO data at entry is left untouched.
    Report send(const Protocol::Message& event, Protocol::DeviceId peer);
    const char* toString(Result result);
}
