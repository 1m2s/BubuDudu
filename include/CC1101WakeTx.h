#pragma once
#include "Protocol.h"

// Peer wake for motion/button boot policy. No ID allocation or power policy here.
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
    // Valid concurrent EVENTs are copied only; no application/radio callbacks
    // run inside send(). Drain after ownership returns, one receipt TX at a time.
    // Eight packets maximum; on full, send() stops Busy before reading the next
    // FIFO packet. New episodes refuse until the deferred queue is drained.
    bool deferredPending();
    bool takeDeferredEvent(Protocol::Message& event); // Refuses while send owns SPI.
    const char* toString(Result result);
}
