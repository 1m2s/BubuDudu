#pragma once

#include <stdint.h>
#include "Protocol.h"

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
    enum class SleepPhase : uint8_t { NONE, WAIT_READY, WAIT_COMMIT, WAIT_ACK, WAIT_SLEEP_ACK_TX };

    struct SleepDecision
    {
        uint16_t sleepId;
        SleepRole role;
    };

    // Peer REQUEST freshness only; no transaction or completed-reply authority.
    struct SleepHistory
    {
        bool havePeerRequest;
        uint16_t newestPeerRequest;
    };
    SleepHistory exportHistory();
    // Startup only, immediately after begin(). Refuses a live/nonfresh FSM.
    bool restoreHistory(const SleepHistory& history);

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

    // Called only from loop(). The transport copies/queues an intent, without
    // changing policy; false means its bounded queue could not accept it.
    using ControlSender = bool (*)(Protocol::MessageType type, uint16_t sleepId);
    void begin(Protocol::DeviceId device, ControlSender sender);
    void update(uint32_t now);
    void forceIdle(uint32_t now);
    void injectActivity(uint32_t now);

    // requestId comes from the existing application message-ID allocator.
    bool requestSleep(uint16_t requestId, uint32_t now);
    void handleControl(const Protocol::Message& message, uint32_t now);
    bool controlStillNeeded(Protocol::MessageType type, uint16_t sleepId);
    void controlSent(Protocol::MessageType type, uint16_t sleepId, uint32_t now);
    void controlFailed(Protocol::MessageType type, uint16_t sleepId, uint32_t now);
    // Consume semantic completion once; the caller must first drain transport.
    // An unconsumed decision is discarded when leaving simulated SLEEPING.
    bool takeSleepDecision(SleepDecision& out);
    bool automaticHeartbeatAllowed();
    void applicationEvent(uint32_t now);

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
