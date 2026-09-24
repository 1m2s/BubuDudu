#include "PowerManager.h"

#include <Arduino.h>

namespace PowerManager
{
    namespace
    {
        constexpr uint32_t HARD_TIMEOUT_MS = 5000;
        constexpr uint32_t PHASE_TIMEOUT_MS = 3000;
        constexpr uint32_t FAILURE_COOLDOWN_MS = 3000;
        constexpr uint32_t SIMULATED_WAKE_MS = 250;

        LocalState local = LocalState::ACTIVE;
        PeerState peer = PeerState::UNKNOWN;
        SleepTransaction sleep;
        SleepDecision sleepDecision{};
        bool sleepDecisionPending = false;
        Protocol::DeviceId localDevice = Protocol::DeviceId::Bubu;
        Protocol::DeviceId peerDevice = Protocol::DeviceId::Dudu;
        ControlSender sendControl = nullptr;
        bool havePeerRequest = false;
        uint16_t newestPeerRequest = 0;
        bool commitAccepted = false;
        uint16_t acceptedCommitMessageId = 0;
        // Replay receipt only, never authority to reopen/advance a transaction.
        struct CompletedReply
        {
            bool valid = false;
            uint16_t sleepId = 0;
            uint16_t commitMessageId = 0;
        } completed;
        bool cooldownActive = false;
        uint32_t cooldownDeadline = 0;
        uint32_t wakeDeadline = 0;

        // Wrap-safe for these short deadlines, with update called regularly.
        // No deadline is more than INT32_MAX milliseconds into the future.
        bool expired(uint32_t now, uint32_t deadline)
        {
            return uint32_t(now - deadline) < 0x80000000UL;
        }

        uint32_t timeLeft(uint32_t now, uint32_t deadline)
        {
            return expired(now, deadline) ? 0 : uint32_t(deadline - now);
        }

        void setLocal(LocalState next, const char* reason)
        {
            if (local == next)
                return;
            if (next != LocalState::SLEEPING)
                sleepDecisionPending = false;
            Serial.printf("POWER: %s -> %s | %s\n", toString(local), toString(next), reason);
            local = next;
        }

        void setPeer(PeerState next)
        {
            if (peer == next)
                return;
            Serial.printf("POWER PEER: %s -> %s\n", toString(peer), toString(next));
            peer = next;
        }

        void cancelNegotiation(uint32_t now, LocalState destination, PeerState peerAfter,
                               const char* reason, bool notifyPeer = true)
        {
            const uint16_t id = sleep.sleepId;
            Serial.printf("POWER: sleep_id=%u CANCELLED | %s | cooldown=%lu ms\n",
                          sleep.sleepId, reason, static_cast<unsigned long>(FAILURE_COOLDOWN_MS));
            sleep = SleepTransaction{};
            commitAccepted = false;
            completed = CompletedReply{};
            cooldownActive = true;
            cooldownDeadline = now + FAILURE_COOLDOWN_MS;
            setPeer(peerAfter);
            setLocal(destination, reason);
            // One best-effort CANCEL; loss is covered by the peer's deadlines.
            if (notifyPeer && sendControl && !sendControl(Protocol::MessageType::SleepCancel, id))
                Serial.println("POWER: CANCEL send request failed; peer timeout remains fallback");
        }

        bool emit(Protocol::MessageType type, uint32_t now)
        {
            if (sendControl && sendControl(type, sleep.sleepId))
                return true;
            cancelNegotiation(now, LocalState::IDLE, PeerState::UNKNOWN, "CONTROL_QUEUE_FAILED");
            return false;
        }

        void startTransaction(uint16_t id, SleepRole role, uint32_t now)
        {
            sleep.active = true;
            sleep.sleepId = id;
            sleep.role = role;
            sleep.phase = role == SleepRole::COORDINATOR ? SleepPhase::WAIT_READY : SleepPhase::WAIT_COMMIT;
            sleep.startedAt = now;
            sleep.phaseDeadline = now + PHASE_TIMEOUT_MS;
            sleep.hardDeadline = now + HARD_TIMEOUT_MS;
            commitAccepted = false;
            completed = CompletedReply{};
            setPeer(PeerState::SLEEP_PENDING);
            setLocal(LocalState::SLEEP_NEGOTIATING, "HANDSHAKE_STARTED");
        }

        void finishTransaction()
        {
            sleepDecision = {sleep.sleepId, sleep.role};
            sleepDecisionPending = true;
            sleep = SleepTransaction{};
            commitAccepted = false;
            setPeer(PeerState::SLEEPING);
            setLocal(LocalState::SLEEPING, "HANDSHAKE_COMPLETE");
        }

        void reject(const Protocol::Message& message, const char* reason)
        {
            Serial.printf("POWER: stale/control rejected %s | sleepId=%u current=%u role=%s phase=%s | %s\n",
                          Protocol::controlName(message.type), message.ackForMessageId,
                          sleep.sleepId, toString(sleep.role), toString(sleep.phase), reason);
        }
    }

    void begin(Protocol::DeviceId device, ControlSender sender)
    {
        local = LocalState::ACTIVE;
        peer = PeerState::UNKNOWN;
        sleep = SleepTransaction{};
        localDevice = device;
        sleepDecisionPending = false;
        peerDevice = device == Protocol::DeviceId::Bubu ? Protocol::DeviceId::Dudu : Protocol::DeviceId::Bubu;
        sendControl = sender;
        havePeerRequest = false;
        newestPeerRequest = 0;
        commitAccepted = false;
        acceptedCommitMessageId = 0;
        completed = CompletedReply{};
        cooldownActive = false;
        cooldownDeadline = 0;
        wakeDeadline = 0;
    }

    SleepHistory exportHistory()
    {
        return {havePeerRequest, newestPeerRequest};
    }

    bool restoreHistory(const SleepHistory& history)
    {
        if (local != LocalState::ACTIVE || peer != PeerState::UNKNOWN ||
            sleep.active || sleepDecisionPending || cooldownActive)
            return false;
        havePeerRequest = history.havePeerRequest;
        newestPeerRequest = history.newestPeerRequest;
        return true;
    }

    void update(uint32_t now)
    {
        if (cooldownActive && expired(now, cooldownDeadline))
            cooldownActive = false;

        if (sleep.active)
        {
            // Overall limit wins if both deadlines have expired. Nothing can
            // extend it by repeatedly requesting another negotiation.
            if (expired(now, sleep.hardDeadline))
                cancelNegotiation(now, LocalState::IDLE, PeerState::UNKNOWN, "HARD_TIMEOUT");
            else if (expired(now, sleep.phaseDeadline))
                cancelNegotiation(now, LocalState::IDLE, PeerState::UNKNOWN, "PHASE_TIMEOUT");
        }

        if (local == LocalState::WAKING && expired(now, wakeDeadline))
        {
            wakeDeadline = 0;
            setLocal(LocalState::ACTIVE, "SIMULATED_WAKE_COMPLETE");
        }
        // IDLE never starts negotiation automatically, even after cooldown.
    }

    void forceIdle(uint32_t now)
    {
        update(now);
        if (local != LocalState::ACTIVE && local != LocalState::IDLE)
        {
            Serial.println("POWER: IDLE refused; use a to cancel/wake first");
            return;
        }
        setLocal(LocalState::IDLE, "DEV_FORCE_IDLE");
    }

    bool requestSleep(uint16_t requestId, uint32_t now)
    {
        update(now);
        if (local != LocalState::IDLE || sleep.active || cooldownActive)
        {
            Serial.println("POWER: negotiation refused; requires IDLE and expired cooldown");
            return false;
        }

        startTransaction(requestId, SleepRole::COORDINATOR, now);
        return emit(Protocol::MessageType::SleepRequest, now);
    }

    void injectActivity(uint32_t now)
    {
        update(now);
        if (sleep.active)
        {
            cancelNegotiation(now, LocalState::ACTIVE, PeerState::ONLINE, "ACTIVITY");
        }
        else if (local == LocalState::SLEEPING)
        {
            completed = CompletedReply{};
            wakeDeadline = now + SIMULATED_WAKE_MS;
            setLocal(LocalState::WAKING, "SIMULATED_ACTIVITY_WAKE");
        }
        else if (local != LocalState::WAKING)
        {
            setLocal(LocalState::ACTIVE, "ACTIVITY");
        }
        // Repeated activity cannot keep extending WAKING or erase cooldown.
    }

    void handleControl(const Protocol::Message& message, uint32_t now)
    {
        update(now);
        using Type = Protocol::MessageType;
        const uint16_t id = message.ackForMessageId;
        if (message.version != Protocol::VERSION || message.sender != peerDevice ||
            !Protocol::isSleepControl(message.type) || message.event != Protocol::EventType::None ||
            (message.type == Type::SleepRequest && id != message.messageId))
        {
            reject(message, "invalid envelope");
            return;
        }
        Serial.printf("POWER RX %s | sleepId=%u messageId=%u\n",
                      Protocol::controlName(message.type), id, message.messageId);

        if (message.type == Type::SleepRequest)
        {
            if (sleep.active && sleep.role == SleepRole::PARTICIPANT && id == sleep.sleepId)
            {
                Serial.println("POWER: duplicate REQUEST for current transaction; deadlines unchanged");
                if (!commitAccepted)
                    emit(Type::SleepReady, now);
                return;
            }
            // Serial-number comparison prevents a delayed closed request from
            // becoming a fresh transaction after cooldown. Reboots have no wire
            // session nonce; both peers must be reset together for that bench case.
            const uint16_t advance = uint16_t(id - newestPeerRequest);
            if (havePeerRequest && (advance == 0 || advance >= 0x8000))
            {
                reject(message, "old/closed REQUEST");
                return;
            }
            havePeerRequest = true;
            newestPeerRequest = id;

            if (sleep.active && sleep.role == SleepRole::COORDINATOR && sleep.phase == SleepPhase::WAIT_READY)
            {
                if (static_cast<uint8_t>(localDevice) < static_cast<uint8_t>(peerDevice))
                {
                    Serial.printf("POWER: simultaneous requests; local DeviceId=%u wins, keeping sleepId=%u\n",
                                  static_cast<unsigned>(localDevice), sleep.sleepId);
                    return;
                }
                Serial.printf("POWER: simultaneous requests; peer DeviceId=%u wins; abandon %u, accept %u\n",
                              static_cast<unsigned>(peerDevice), sleep.sleepId, id);
                const uint32_t originalStart = sleep.startedAt;
                const uint32_t originalHard = sleep.hardDeadline;
                // Do not CANCEL the losing ID: equal IDs can occur in opposite
                // directions. Superseded transport work will be discarded.
                startTransaction(id, SleepRole::PARTICIPANT, now);
                sleep.startedAt = originalStart;
                sleep.hardDeadline = originalHard; // Collision cannot extend the episode.
                emit(Type::SleepReady, now);
                return;
            }
            if (local == LocalState::IDLE && !sleep.active && !cooldownActive)
            {
                startTransaction(id, SleepRole::PARTICIPANT, now);
                emit(Type::SleepReady, now);
                return;
            }
            reject(message, "REQUEST requires IDLE/no cooldown; no takeover after COMMIT");
            if (!sleep.active && sendControl)
                sendControl(Type::SleepCancel, id); // Best-effort refusal.
            return;
        }

        if (!sleep.active)
        {
            // Exact post-completion retransmission: replay the receipt only.
            // No transaction/role/deadline/state transition is created here.
            if (local == LocalState::SLEEPING && completed.valid && message.type == Type::SleepCommit &&
                id == completed.sleepId && message.messageId == completed.commitMessageId)
            {
                Serial.println("POWER: duplicate completed COMMIT; resend SLEEP_ACK, no state transition");
                if (!sendControl || !sendControl(Type::SleepAck, id))
                    Serial.println("POWER: completed SLEEP_ACK replay could not be queued");
                return;
            }
            reject(message, "no active transaction");
            return;
        }
        if (id != sleep.sleepId)
        {
            reject(message, "sleepId mismatch");
            return;
        }
        if (message.type == Type::SleepCancel)
        {
            cancelNegotiation(now, LocalState::IDLE, PeerState::ONLINE, "PEER_CANCEL", false);
            return;
        }
        if (message.type == Type::SleepReady && sleep.role == SleepRole::COORDINATOR)
        {
            if (sleep.phase == SleepPhase::WAIT_READY)
            {
                sleep.phase = SleepPhase::WAIT_ACK;
                sleep.phaseDeadline = now + PHASE_TIMEOUT_MS;
            }
            else if (sleep.phase == SleepPhase::WAIT_ACK)
            {
                Serial.println("POWER: duplicate READY; resend COMMIT, deadlines unchanged");
            }
            else
            {
                reject(message, "READY in wrong phase");
                return;
            }
            emit(Type::SleepCommit, now);
            return;
        }
        if (message.type == Type::SleepCommit && sleep.role == SleepRole::PARTICIPANT &&
            (sleep.phase == SleepPhase::WAIT_COMMIT || sleep.phase == SleepPhase::WAIT_SLEEP_ACK_TX))
        {
            if (sleep.phase == SleepPhase::WAIT_SLEEP_ACK_TX)
                Serial.println("POWER: duplicate current COMMIT; SLEEP_ACK already scheduled");
            else
            {
                commitAccepted = true;
                acceptedCommitMessageId = message.messageId;
                sleep.phase = SleepPhase::WAIT_SLEEP_ACK_TX;
                sleep.phaseDeadline = now + PHASE_TIMEOUT_MS;
                // COMMIT is forward progress. Only this first acceptance starts
                // a new phase budget; duplicates/retries never extend either timer.
                Serial.printf("POWER: WAIT_COMMIT -> WAIT_SLEEP_ACK_TX | sleepId=%u\n", id);
            }
            emit(Type::SleepAck, now);
            // Close only when the transport actually submits SLEEP_ACK, not
            // when it is merely waiting behind another packet in a queue.
            return;
        }
        if (message.type == Type::SleepAck && sleep.role == SleepRole::COORDINATOR &&
            sleep.phase == SleepPhase::WAIT_ACK)
        {
            finishTransaction();
            return;
        }
        reject(message, "wrong role/phase");
    }

    bool controlStillNeeded(Protocol::MessageType type, uint16_t id)
    {
        using Type = Protocol::MessageType;
        if (type == Type::SleepAck && local == LocalState::SLEEPING && completed.valid && id == completed.sleepId)
            return true;
        if (!sleep.active || id != sleep.sleepId)
            return false;
        if (sleep.role == SleepRole::COORDINATOR)
            return (type == Type::SleepRequest && sleep.phase == SleepPhase::WAIT_READY) ||
                   (type == Type::SleepCommit && sleep.phase == SleepPhase::WAIT_ACK);
        return (type == Type::SleepReady && sleep.phase == SleepPhase::WAIT_COMMIT && !commitAccepted) ||
               (type == Type::SleepAck && sleep.phase == SleepPhase::WAIT_SLEEP_ACK_TX && commitAccepted);
    }

    void controlSent(Protocol::MessageType type, uint16_t id, uint32_t now)
    {
        update(now);
        if (type == Protocol::MessageType::SleepAck && sleep.active && id == sleep.sleepId &&
            sleep.role == SleepRole::PARTICIPANT && sleep.phase == SleepPhase::WAIT_SLEEP_ACK_TX && commitAccepted)
        {
            completed.valid = true;
            completed.sleepId = id;
            completed.commitMessageId = acceptedCommitMessageId;
            finishTransaction();
        }
    }

    void controlFailed(Protocol::MessageType type, uint16_t id, uint32_t now)
    {
        update(now);
        if (!controlStillNeeded(type, id))
            return;
        if (sleep.active)
        {
            const PeerState outcome = type == Protocol::MessageType::SleepRequest ? PeerState::OFFLINE : PeerState::UNKNOWN;
            cancelNegotiation(now, LocalState::IDLE, outcome, "CONTROL_RETRIES_EXHAUSTED");
        }
        else
        {
            // Peer may have received our final SLEEP_ACK but its receipt was
            // lost. Do not label it OFFLINE or pretend agreement is certain.
            setPeer(PeerState::UNKNOWN);
            Serial.println("POWER: final SLEEP_ACK delivery uncertain; execution policy must decide");
        }
    }

    bool takeSleepDecision(SleepDecision& out)
    {
        if (!sleepDecisionPending)
            return false;
        out = sleepDecision;
        sleepDecisionPending = false;
        return true;
    }

    void notifySleepExecutionFailed(uint32_t now)
    {
        if (local != LocalState::SLEEPING || sleep.active) return;
        completed = CompletedReply{};
        cooldownActive = true;
        cooldownDeadline = now + FAILURE_COOLDOWN_MS;
        setLocal(LocalState::IDLE, "SLEEP_EXECUTION_FAILED");
        // Peer may already be asleep. No CANCEL, OFFLINE inference or retry.
    }

    bool automaticHeartbeatAllowed()
    {
        return local == LocalState::ACTIVE || local == LocalState::IDLE;
    }

    void applicationEvent(uint32_t now)
    {
        if (sleep.active)
            injectActivity(now);
        setPeer(PeerState::ONLINE); // Actual application activity is awake evidence.
    }

    void notePeerSeen()
    {
        // A packet receipt ACK does not prove the peer abandoned its sleep
        // intent. In particular it must not undo HANDSHAKE_COMPLETE.
        if (peer != PeerState::SLEEP_PENDING && peer != PeerState::SLEEPING)
            setPeer(PeerState::ONLINE);
    }

    void notePeerUnreachable()
    {
        // A transport failure alone must not overwrite the handshake's
        // SLEEP_PENDING/SLEEPING states with OFFLINE.
        if (peer != PeerState::SLEEP_PENDING && peer != PeerState::SLEEPING)
            setPeer(PeerState::OFFLINE);
    }

    LocalState localState() { return local; }
    PeerState peerState() { return peer; }
    const SleepTransaction& transaction() { return sleep; }

    uint32_t cooldownLeftMs(uint32_t now)
    {
        return cooldownActive ? timeLeft(now, cooldownDeadline) : 0;
    }

    void printStatus(uint32_t now)
    {
        Serial.printf("POWER: LOCAL=%s PEER=%s TXN_ACTIVE=%u ROLE=%s PHASE=%s "
                      "SLEEP_ID=%u PHASE_LEFT_MS=%lu HARD_LEFT_MS=%lu COOLDOWN_LEFT_MS=%lu\n",
                      toString(local), toString(peer), sleep.active ? 1U : 0U,
                      toString(sleep.role), toString(sleep.phase), sleep.sleepId,
                      static_cast<unsigned long>(sleep.active ? timeLeft(now, sleep.phaseDeadline) : 0),
                      static_cast<unsigned long>(sleep.active ? timeLeft(now, sleep.hardDeadline) : 0),
                      static_cast<unsigned long>(cooldownLeftMs(now)));
    }

    const char* toString(LocalState state)
    {
        switch (state)
        {
            case LocalState::ACTIVE: return "ACTIVE";
            case LocalState::IDLE: return "IDLE";
            case LocalState::SLEEP_NEGOTIATING: return "SLEEP_NEGOTIATING";
            case LocalState::SLEEPING: return "SLEEPING";
            case LocalState::WAKING: return "WAKING";
        }
        return "INVALID";
    }

    const char* toString(PeerState state)
    {
        switch (state)
        {
            case PeerState::ONLINE: return "ONLINE";
            case PeerState::SLEEP_PENDING: return "SLEEP_PENDING";
            case PeerState::SLEEPING: return "SLEEPING";
            case PeerState::UNKNOWN: return "UNKNOWN";
            case PeerState::OFFLINE: return "OFFLINE";
        }
        return "INVALID";
    }

    const char* toString(SleepRole role)
    {
        switch (role)
        {
            case SleepRole::NONE: return "NONE";
            case SleepRole::COORDINATOR: return "COORDINATOR";
            case SleepRole::PARTICIPANT: return "PARTICIPANT";
        }
        return "INVALID";
    }

    const char* toString(SleepPhase phase)
    {
        switch (phase)
        {
            case SleepPhase::NONE: return "NONE";
            case SleepPhase::WAIT_READY: return "WAIT_READY";
            case SleepPhase::WAIT_COMMIT: return "WAIT_COMMIT";
            case SleepPhase::WAIT_ACK: return "WAIT_ACK";
            case SleepPhase::WAIT_SLEEP_ACK_TX: return "WAIT_SLEEP_ACK_TX";
        }
        return "INVALID";
    }
}
