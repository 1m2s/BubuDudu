#include "PowerManager.h"

#include <Arduino.h>

namespace PowerManager
{
    namespace
    {
        constexpr uint32_t HARD_TIMEOUT_MS = 5000;
        constexpr uint32_t HARD_TEST_PHASE_TIMEOUT_MS = 7000;
        constexpr uint32_t PHASE_TIMEOUT_MS = 2000;
        constexpr uint32_t FAILURE_COOLDOWN_MS = 3000;
        constexpr uint32_t SIMULATED_WAKE_MS = 250;

        LocalState local = LocalState::ACTIVE;
        PeerState peer = PeerState::UNKNOWN;
        SleepTransaction sleep;
        uint16_t nextSleepId = 1;
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

        void cancelNegotiation(uint32_t now, LocalState destination, const char* reason)
        {
            Serial.printf("POWER: sleep_id=%u CANCELLED | %s | cooldown=%lu ms\n",
                          sleep.sleepId, reason, static_cast<unsigned long>(FAILURE_COOLDOWN_MS));
            sleep = SleepTransaction{};
            cooldownActive = true;
            cooldownDeadline = now + FAILURE_COOLDOWN_MS;
            setLocal(destination, reason);
        }
    }

    void begin()
    {
        local = LocalState::ACTIVE;
        peer = PeerState::UNKNOWN;
        sleep = SleepTransaction{};
        nextSleepId = 1;
        cooldownActive = false;
        cooldownDeadline = 0;
        wakeDeadline = 0;
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
                cancelNegotiation(now, LocalState::IDLE, "HARD_TIMEOUT");
            else if (expired(now, sleep.phaseDeadline))
                cancelNegotiation(now, LocalState::IDLE, "PHASE_TIMEOUT");
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

    void startSimulatedNegotiation(uint32_t now, bool phaseTimeoutFirst)
    {
        update(now);
        if (local != LocalState::IDLE || sleep.active || cooldownActive)
        {
            Serial.println("POWER: negotiation refused; requires IDLE and expired cooldown");
            return;
        }

        sleep.active = true;
        sleep.sleepId = nextSleepId++;
        if (nextSleepId == 0)
            nextSleepId = 1;
        sleep.role = SleepRole::COORDINATOR;
        sleep.phase = SleepPhase::WAIT_READY;
        sleep.startedAt = now;
        sleep.phaseDeadline = now + (phaseTimeoutFirst ? PHASE_TIMEOUT_MS : HARD_TEST_PHASE_TIMEOUT_MS);
        sleep.hardDeadline = now + HARD_TIMEOUT_MS;
        setLocal(LocalState::SLEEP_NEGOTIATING, "LOCAL_SIMULATION_ONLY");
    }

    void injectActivity(uint32_t now)
    {
        update(now);
        if (sleep.active)
        {
            cancelNegotiation(now, LocalState::ACTIVE, "ACTIVITY");
        }
        else if (local == LocalState::SLEEPING)
        {
            wakeDeadline = now + SIMULATED_WAKE_MS;
            setLocal(LocalState::WAKING, "SIMULATED_ACTIVITY_WAKE");
        }
        else if (local != LocalState::WAKING)
        {
            setLocal(LocalState::ACTIVE, "ACTIVITY");
        }
        // Repeated activity cannot keep extending WAKING or erase cooldown.
    }

    void completeSimulatedSleep(uint32_t now)
    {
        update(now);
        if (!sleep.active || local != LocalState::SLEEP_NEGOTIATING)
        {
            Serial.println("POWER: simulated completion refused; requires active negotiation");
            return;
        }
        sleep = SleepTransaction{};
        setLocal(LocalState::SLEEPING, "DEV_SIMULATED_COMPLETION; CPU/radio remain awake");
    }

    void notePeerSeen()
    {
        setPeer(PeerState::ONLINE);
    }

    void notePeerUnreachable()
    {
        // A future handshake will assign SLEEP_PENDING/SLEEPING. A transport
        // failure alone must not overwrite either with OFFLINE.
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
        }
        return "INVALID";
    }
}
