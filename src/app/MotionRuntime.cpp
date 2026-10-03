#include "MotionRuntime.h"
#include "ButtonRuntime.h"
#include "RadioRuntime.h"
#include "SleepRuntime.h"
#include "PowerManager.h"
#include <Arduino.h>

namespace MotionRuntime
{
    using namespace AppIdentity;
    using RadioRuntime::Transport;

    namespace
    {
        Motion motion;
        bool motionReady = false;
        MovementState movementState = MovementState::READY;
        uint32_t settleStartedAt = 0;
        // Additional wait AFTER sensor Inactivity; ADXL345 TIME_INACT remains 3 seconds.
        constexpr uint32_t SETTLE_MS = 1000;
        ProximityUpdateState proximityUpdateState = ProximityUpdateState::READY;
        ProximityClassification proximityClassification = ProximityClassification::UNKNOWN;
        // Provisional RSSI boundaries; these do not represent meters.
        constexpr int8_t ENTER_FAR_DBM = -80;
        constexpr int8_t ENTER_CLOSE_DBM = -75;
        struct ProximitySample
        {
            uint16_t messageId;
            int8_t rssi;
        };
        ProximitySample proximitySamples[3]{};
        uint8_t proximitySampleCount = 0;
        uint32_t checkStartedAt = 0;
        // Provisional overall sampling budget; physically evaluate traffic cadence.
        constexpr uint32_t CHECK_TIMEOUT_MS = 12000;
        constexpr uint32_t PROBE_REPLY_WAIT_MS = 500;
        bool probeOutstanding = false;
        bool probeTimerActive = false; // Also spaces attempts after immediate TX rejection.
        uint16_t probeMessageId = 0;
        uint32_t probeStartedAt = 0;
    } // namespace

    void resetMovement()
    {
        movementState = MovementState::READY;
        settleStartedAt = 0;
    }

    // Loop-owned state. A true result is a one-shot settled edge.
    bool updateMovement(MotionEvent event, uint32_t now)
    {
        if (PowerManager::localState() != PowerManager::LocalState::ACTIVE)
        {
            resetMovement();
            return false;
        }
        // Consume Activity before expiry: movement wins even at the deadline.
        if (event == MotionEvent::Activity)
        {
            if (movementState != MovementState::MOVING) Serial.println("MOVEMENT | MOVING");
            movementState = MovementState::MOVING;
            settleStartedAt = 0;
        }
        else if (event == MotionEvent::Inactivity && movementState == MovementState::MOVING)
        {
            movementState = MovementState::WAITING;
            settleStartedAt = now; // Repeated Inactivity cannot restart this timer.
            Serial.println("MOVEMENT | WAITING");
        }
        if (movementState == MovementState::WAITING && uint32_t(now - settleStartedAt) >= SETTLE_MS)
        {
            resetMovement();
            return true;
        }
        return false;
    }

    void resetProximityProbe()
    {
        probeOutstanding = false;
        probeTimerActive = false;
        probeMessageId = 0;
        probeStartedAt = 0;
    }

    void resetProximityCheck()
    {
        // Clear this measurement only; retain the last completed classification.
        proximityUpdateState = ProximityUpdateState::READY;
        checkStartedAt = 0;
        proximitySampleCount = 0;
        resetProximityProbe();
        for (auto &sample : proximitySamples)
            sample = {};
    }

    void cancelProximityCheck(const char *reason)
    {
        if (proximityUpdateState == ProximityUpdateState::CHECKING)
            Serial.printf("PROXIMITY CHECK | CANCELLED | reason=%s\n", reason);
        resetProximityCheck();
    }

    void checkProximityEligibility()
    {
        if (PowerManager::localState() != PowerManager::LocalState::ACTIVE)
            cancelProximityCheck("NOT_ACTIVE");
        else if (PowerManager::peerState() == PowerManager::PeerState::OFFLINE)
            cancelProximityCheck("PEER_OFFLINE");
    }

    void startProximityCheck(uint32_t now)
    {
        if (ButtonRuntime::pending() || PowerManager::localState() != PowerManager::LocalState::ACTIVE ||
            PowerManager::peerState() == PowerManager::PeerState::OFFLINE)
            return;
        // Keep failed-entry recovery pending until application/control work and
        // callbacks drain. An existing measurement can absorb it without restart.
        if (PowerManager::sleepRecoveryCheckPending() &&
            proximityUpdateState != ProximityUpdateState::CHECKING &&
            (movementState != MovementState::READY || SleepRuntime::draining() ||
             RadioRuntime::sleepTransportBlockedReason()))
            return;
        // An eligible motion/peer check also satisfies the one deferred request.
        PowerManager::sleepRecoveryCheckStarted();
        if (proximityUpdateState == ProximityUpdateState::CHECKING) return;
        resetProximityCheck();
        checkStartedAt = now;
        proximityUpdateState = ProximityUpdateState::CHECKING;
        Serial.println("PROXIMITY CHECK | START");
    }

    void serviceProximityCheck(uint32_t now)
    {
        checkProximityEligibility();
        if (proximityUpdateState == ProximityUpdateState::CHECKING &&
            uint32_t(now - checkStartedAt) >= CHECK_TIMEOUT_MS)
        {
            Serial.printf("PROXIMITY CHECK | TIMEOUT | samples=%u\n", proximitySampleCount);
            resetProximityCheck();
        }
    }

    void serviceMotion()
    {
        const MotionEvent event = motion.getEvent();
        const uint32_t now = uint32_t(millis());
        if (event == MotionEvent::Activity)
        {
            Serial.println("MOTION AWAKE | MOVING");
            SleepRuntime::noteLocalActivity(now);
            cancelProximityCheck("MOVEMENT");
        }
        else if (event == MotionEvent::Inactivity)
            Serial.println("MOTION AWAKE | INACTIVITY");
        if (updateMovement(event, now))
        {
            Serial.println("MOVEMENT | SETTLED | state=READY");
            startProximityCheck(now);
        }
    }

    void serviceProximityProbe(uint32_t now)
    {
        // Stationary ACTIVE can service recovery directly. Actual movement and
        // settling keep their existing trigger; join a running check without restart.
        if (PowerManager::sleepRecoveryCheckPending() &&
            (movementState == MovementState::READY || proximityUpdateState == ProximityUpdateState::CHECKING))
            startProximityCheck(now);
        serviceProximityCheck(now);
        if ((RadioRuntime::transport() != Transport::CC1101 &&
             proximityClassification != ProximityClassification::UNKNOWN) ||
            proximityUpdateState != ProximityUpdateState::CHECKING)
        {
            resetProximityProbe();
            return;
        }
        if (probeTimerActive && uint32_t(now - probeStartedAt) < PROBE_REPLY_WAIT_MS) return;

        // Expiry abandons the old ID. A rejected send still consumes this opportunity.
        resetProximityProbe();
        const Protocol::Message probe{Protocol::VERSION,
                                      Protocol::MessageType::ProximityProbe,
                                      RadioRuntime::allocateMessageId(),
                                      LOCAL_DEVICE,
                                      Protocol::EventType::None,
                                      0};
        probeMessageId = probe.messageId;
        probeStartedAt = now;
        probeTimerActive = true;
        probeOutstanding = ESPNowRadio::send(reinterpret_cast<const uint8_t *>(&probe), sizeof(probe));
    }

    void sampleProximity(const ESPNowRadio::RssiObservation &observation, uint32_t now)
    {
        serviceProximityCheck(now); // Hard timeout wins over a late-drained third sample.
        if (proximityUpdateState != ProximityUpdateState::CHECKING ||
            observation.message.sender != PEER_DEVICE)
            return;
        const uint32_t capturedAfterStart = uint32_t(observation.receivedAt - checkStartedAt);
        // Strictly AFTER start. Equal millis() is ambiguous and conservatively ignored.
        // Short window excludes older timestamps across rollover; reject future timestamps too.
        if (capturedAfterStart == 0 || capturedAfterStart >= CHECK_TIMEOUT_MS ||
            uint32_t(now - observation.receivedAt) >= 0x80000000UL)
            return;
        const bool probeReply = RadioRuntime::transport() == Transport::CC1101 ||
                                (proximityClassification == ProximityClassification::UNKNOWN &&
                                 observation.message.type == Protocol::MessageType::ProximityProbeReply);
        if (probeReply)
        {
            const auto &message = observation.message;
            if (!probeOutstanding || message.version != Protocol::VERSION ||
                message.type != Protocol::MessageType::ProximityProbeReply ||
                message.event != Protocol::EventType::None || message.ackForMessageId != probeMessageId ||
                uint32_t(now - probeStartedAt) >= PROBE_REPLY_WAIT_MS ||
                uint32_t(observation.receivedAt - probeStartedAt) >= PROBE_REPLY_WAIT_MS)
                return;
        }
        // One peer uses a shared message-ID allocator for every packet type.
        // Retransmissions of that message remain one sample, even with changed RSSI.
        for (uint8_t i = 0; i < proximitySampleCount; ++i)
            if (proximitySamples[i].messageId == observation.message.messageId) return;
        if (probeReply) resetProximityProbe();
        proximitySamples[proximitySampleCount++] = {observation.message.messageId, observation.rssi};
        Serial.printf("PROXIMITY CHECK | SAMPLE | n=%u | rssi=%d dBm\n", proximitySampleCount,
                      static_cast<int>(observation.rssi));
        if (proximitySampleCount != 3) return;
        int8_t a = proximitySamples[0].rssi, b = proximitySamples[1].rssi, c = proximitySamples[2].rssi;
        if (a > b)
        {
            const int8_t temp = a;
            a = b;
            b = temp;
        }
        if (b > c)
        {
            const int8_t temp = b;
            b = c;
            c = temp;
        }
        if (a > b)
        {
            const int8_t temp = a;
            a = b;
            b = temp;
        }
        Serial.printf("PROXIMITY CHECK | COMPLETE | samples=3 | median=%d dBm\n", static_cast<int>(b));
        const bool firstClassification = proximityClassification == ProximityClassification::UNKNOWN;
        if (proximityClassification == ProximityClassification::FAR)
        {
            if (b >= ENTER_CLOSE_DBM) proximityClassification = ProximityClassification::CLOSE;
        }
        else // UNKNOWN uses the same conservative FAR boundary as CLOSE.
            proximityClassification =
                b <= ENTER_FAR_DBM ? ProximityClassification::FAR : ProximityClassification::CLOSE;
        RadioRuntime::classificationCompleted(now, firstClassification);
        Serial.printf("PROXIMITY | median=%d dBm | %s\n", static_cast<int>(b),
                      proximityClassification == ProximityClassification::FAR ? "FAR" : "CLOSE");
        resetProximityCheck();
    }

    void resetBootState()
    {
        resetMovement();
        resetProximityCheck();
        proximityClassification = ProximityClassification::UNKNOWN; // RAM-only, never retained.
        motionReady = false;
    }
    bool ready()
    {
        return motionReady;
    }
    MovementState movement()
    {
        return movementState;
    }
    ProximityClassification classification()
    {
        return proximityClassification;
    }
    ProximityUpdateState checkState()
    {
        return proximityUpdateState;
    }
    bool probePending()
    {
        return probeOutstanding;
    }
    bool prepareForSleep()
    {
        return motion.prepareForSleep();
    }
    bool cancelSleepPreparation()
    {
        return motion.cancelSleepPreparation();
    }
#ifdef DEVICE_DUDU
    void reportStartupI2cHealth(uint8_t address)
    {
        motion.reportStartupI2cHealth(address);
    }
#endif
    void begin()
    {
        motionReady = motion.begin(MOTION_SDA_PIN, MOTION_SCL_PIN, MOTION_INT1_PIN);
        const int motionLevel = digitalRead(motion.getInterruptPin());
        if (motionReady)
            Serial.printf("MOTION INIT | OK (DEVID=0xE5) | GPIO%u INT1=%d | startup=%d "
                          "(0=None, 1=Activity, 2=Inactivity)\n",
                          motion.getInterruptPin(), motionLevel, static_cast<int>(motion.getStartupEvent()));
        else
            Serial.printf(
                "MOTION INIT | FAILED (DEVID/config check) | GPIO%u INT1=%d | startup=UNAVAILABLE | continuing\n",
                motion.getInterruptPin(), motionLevel);
    }
    void serviceRssiObservations()
    {
        // Best-effort diagnostics only, after all normal protocol/sleep/motion work.
        // Never drain indefinitely or include this backlog in sleep-entry guards.
        for (unsigned i = 0; i < 2; ++i)
        {
            ESPNowRadio::RssiObservation observation{};
            if (!ESPNowRadio::takeRssiObservation(observation)) break;
            Serial.printf(
                "ESPNOW RSSI | sender=%s | id=%u | type=%u | ackFor=%u | rssi=%d dBm | age=%lu ms\n",
                deviceName(observation.message.sender), observation.message.messageId,
                static_cast<unsigned>(observation.message.type), observation.message.ackForMessageId,
                static_cast<int>(observation.rssi),
                static_cast<unsigned long>(uint32_t(millis() - observation.receivedAt)));
            sampleProximity(observation, uint32_t(millis()));
        }
    }
} // namespace MotionRuntime
