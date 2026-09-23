#pragma once

#include <stdint.h>

// Single owner: Arduino setup()/loop(), never the Wi-Fi callback.
// This checkpoint models power state only; it cannot suspend radio or CPU.
namespace PowerManager
{
    enum class LocalState : uint8_t
    {
        ACTIVE, IDLE, SLEEP_NEGOTIATING, SLEEPING, WAKING
    };

    enum class PeerState : uint8_t
    {
        ONLINE, SLEEP_PENDING, SLEEPING, UNKNOWN, OFFLINE
    };

    enum class SleepRole : uint8_t { NONE, COORDINATOR, PARTICIPANT };
    enum class SleepPhase : uint8_t { NONE, WAIT_READY, WAIT_COMMIT, WAIT_ACK };

    struct SleepTransaction
    {
        bool active = false;
        uint16_t sleepId = 0;
        SleepRole role = SleepRole::NONE;
        SleepPhase phase = SleepPhase::NONE;
        uint32_t startedAt = 0;
        uint32_t phaseDeadline = 0;
        uint32_t hardDeadline = 0;
    };

    void begin();
    void update(uint32_t now);
    void forceIdle(uint32_t now);
    void injectActivity(uint32_t now);

    // Explicit bench requests only. Default: hard limit 5s, phase limit 7s
    // to demonstrate hard cancellation. Alternate: phase 2s, hard 5s.
    // No peer messages or automatic phase advancement/retries are simulated.
    void startSimulatedNegotiation(uint32_t now, bool phaseTimeoutFirst = false);
    void completeSimulatedSleep(uint32_t now);

    // Observations from existing protocol processing, not new radio policy.
    void notePeerSeen();
    void notePeerUnreachable();

    LocalState localState();
    PeerState peerState();
    const SleepTransaction& transaction();
    uint32_t cooldownLeftMs(uint32_t now);
    void printStatus(uint32_t now);

    const char* toString(LocalState state);
    const char* toString(PeerState state);
    const char* toString(SleepRole role);
    const char* toString(SleepPhase phase);
}
