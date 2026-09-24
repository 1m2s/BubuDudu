#pragma once

#include <stdint.h>
#include "PowerManager.h"

namespace RtcState
{
    // History, never work in progress. IDs use existing uint16_t rollover rules;
    // zero and 0xFFFF are valid. Flags determine whether peer history exists.
    struct History
    {
        uint16_t nextMessageId;
        bool haveLastPeerEvent;
        uint16_t lastPeerEventId;
        PowerManager::SleepHistory sleep;
    };

    // Single application owner. Replace snapshot; load leaves out untouched on
    // failure. Load is read-only; the startup caller explicitly invalidates it
    // after applying history, so an old allocator snapshot cannot be replayed.
    void save(const History& history);
    bool load(History& out);
    void invalidate();

    // Not wired to runtime yet. Future entry must save AFTER the last ID is
    // allocated, immediately before sleeping. An awake CPU makes snapshots stale.
    // Future startup must gate load on a real deep-wake reset cause, invalidate
    // on cold boot, and recover the retained CC1101 FIFO BEFORE begin() can reset
    // the radio. Neither reset-cause handling nor that recovery is implemented.
}
