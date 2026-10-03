#include <Arduino.h>

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Config.h"
#include "ESPNowRadio.h"
#include "Protocol.h"
#include "PowerManager.h"
#include "CC1101SleepArm.h"
#include "RtcState.h"
#include "CC1101WakeRecovery.h"
#include "CC1101WakeTx.h"
#include "Motion.h"
#include "Display.h"
#include "LED.h"

void saveRtcHistory();
bool restoreRtcHistory();


namespace
{
    Motion motion;
    Display display;
    LED led;
    bool motionReady = false;
    bool displayReady = false;
    // Presentation strings only; update this snapshot AFTER a safe redraw.
    struct DisplaySnapshot
    {
        const char* peer;
        const char* distance;
        const char* radio;
        const char* state;
        const char* motion;
    };
    DisplaySnapshot displayedStatus{};

#ifdef DEVICE_DUDU
    // Temporary observation only; attempts include awake/final-sleep frames,
    // not initialization, and cannot confirm delivery to the physical panel.
    struct DisplayDiagnostic
    {
        bool sampled = false;
        uint32_t sampledAt = 0, attempts = 0, lastAttemptAt = 0;
    } displayDiagnostic;

    void noteDisplayAttempt()
    {
        ++displayDiagnostic.attempts;
        displayDiagnostic.lastAttemptAt = uint32_t(millis());
    }
#endif

    enum class MovementState : uint8_t { READY, MOVING, WAITING };
    MovementState movementState = MovementState::READY;
    uint32_t settleStartedAt = 0;
    // Physically tuned additional wait AFTER sensor Inactivity on current hardware.
    // ADXL345 TIME_INACT remains 3 seconds.
    constexpr uint32_t SETTLE_MS = 1000;

    void resetMovement()
    {
        movementState = MovementState::READY;
        settleStartedAt = 0;
    }

    // Loop-owned diagnostic state only. True is a one-shot settled result.
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
            if (movementState != MovementState::MOVING)
                Serial.println("MOVEMENT | MOVING");
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

    constexpr UBaseType_t RX_QUEUE_LENGTH = 8;
    QueueHandle_t receiveQueue = nullptr;
    bool protocolReady = false;
    CC1101WakeRecovery::BootInfo bootInfo;
    CC1101WakeRecovery::Report wakeReport;
    bool rtcRestored = false;
    bool sleepDrainWaiting = false;
    uint32_t sleepDrainStarted = 0;
    constexpr uint32_t SLEEP_DRAIN_TIMEOUT_MS = 3000;

    constexpr uint32_t AUTOMATIC_SLEEP_INACTIVITY_MS = 35000;
    constexpr uint32_t AUTOMATIC_SLEEP_PEER_GRACE_MS = 3000;
    uint32_t lastMeaningfulActivity = 0;
    bool automaticSleepArmed = false;
    bool productSleepEligible(uint32_t now, uint32_t requiredInactivityMs = AUTOMATIC_SLEEP_INACTIVITY_MS);
    const char* sleepTransportBlockedReason();

    void noteLocalActivity(uint32_t now)
    {
        lastMeaningfulActivity = now;
        automaticSleepArmed = true;
        PowerManager::injectActivity(now);
    }

    constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
    bool buttonRawPressed = false;
    bool buttonStablePressed = false;
    uint32_t buttonChangedAt = 0;
    bool buttonHeartbeatPending = false;
    bool buttonWakeIntentHeld = false;
    bool buttonWakeRetryOnPress = false;
    uint32_t buttonWakeHeldAt = 0;
    constexpr uint32_t BUTTON_WAKE_DRAIN_TIMEOUT_MS = 3000;
    bool retainedUserAnimationPending = false;
    uint16_t retainedUserAnimationId = 0;
    const char* lastButtonSleepBlock = nullptr;

    void failButtonWake(const char* reason)
    {
        if (!buttonWakeIntentHeld) return;
        buttonWakeIntentHeld = buttonHeartbeatPending = false;
        buttonWakeRetryOnPress = true;
        Serial.printf("BUTTON WAKE | GIVE_UP | reason=%s | intent cleared | delivery=UNCONFIRMED | new debounced press required\n", reason);
    }

    void beginButton()
    {
        pinMode(BUTTON_PIN, INPUT_PULLUP);
        buttonRawPressed = digitalRead(BUTTON_PIN) == LOW;
        buttonWakeIntentHeld = bootInfo.wokeFromGpio(BUTTON_PIN);
        // A latched wake is already one press, even if released during startup.
        // Seed the pressed state so only a stable HIGH can rearm an awake edge.
        buttonStablePressed = buttonWakeIntentHeld;
        buttonChangedAt = uint32_t(millis());
        buttonWakeHeldAt = buttonChangedAt;
        buttonWakeRetryOnPress = false;
        buttonHeartbeatPending = buttonWakeIntentHeld;
        lastButtonSleepBlock = nullptr;
        if (buttonWakeIntentHeld)
        {
            Serial.println("BUTTON WAKE | GPIO5 | one UserHeartbeat intent preserved | HELD_FOR_PEER_WAKE_HANDOFF");
            noteLocalActivity(buttonChangedAt);
        }
    }

    void serviceButton()
    {
        const uint32_t now = uint32_t(millis());
        const bool pressed = digitalRead(BUTTON_PIN) == LOW;
        if (pressed != buttonRawPressed)
        {
            buttonRawPressed = pressed;
            buttonChangedAt = now;
        }
        if (buttonRawPressed == buttonStablePressed || uint32_t(now - buttonChangedAt) < BUTTON_DEBOUNCE_MS)
            return;
        buttonStablePressed = buttonRawPressed;
        if (!buttonStablePressed)
        {
            Serial.println("BUTTON | RELEASED | stable 30ms | rearmed");
            return; // Release does not clear the held wake intent or create an EVENT.
        }
        Serial.println("BUTTON | PRESSED");
        noteLocalActivity(now);
        if (!buttonHeartbeatPending)
        {
            buttonHeartbeatPending = true;
            if (buttonWakeRetryOnPress)
            {
                buttonWakeIntentHeld = true;
                buttonWakeHeldAt = now;
                Serial.println("BUTTON WAKE | RETRY_ARMED | new debounced press");
            }
            Serial.println("BUTTON HEARTBEAT | PENDING");
        }
    }

    const char* buttonSleepBlockedReason()
    {
        // A held LOW would immediately wake again. Poll at admission/final guards;
        // a complete pulse between polls or in SDK sleep entry can still be missed.
        const char* reason = digitalRead(BUTTON_PIN) == LOW ? "BUTTON_LOW" :
            buttonWakeIntentHeld ? "BUTTON_WAKE_INTENT_HELD" :
            buttonHeartbeatPending ? "BUTTON_HEARTBEAT_PENDING" : nullptr;
        if (reason != lastButtonSleepBlock)
        {
            lastButtonSleepBlock = reason;
            if (reason) Serial.printf("BUTTON SLEEP | BLOCKED | reason=%s\n", reason);
        }
        return reason;
    }


    // ======================================================
    // Device identity
    // ======================================================

#ifdef DEVICE_BUBU

    constexpr Protocol::DeviceId LOCAL_DEVICE =
        Protocol::DeviceId::Bubu;

    constexpr Protocol::DeviceId PEER_DEVICE =
        Protocol::DeviceId::Dudu;

#elif defined(DEVICE_DUDU)

    constexpr Protocol::DeviceId LOCAL_DEVICE =
        Protocol::DeviceId::Dudu;

    constexpr Protocol::DeviceId PEER_DEVICE =
        Protocol::DeviceId::Bubu;

#else

#error "Device identity not configured"

#endif

    uint16_t nextMessageId =
        1;


    enum class Transport : uint8_t { ESP_NOW, CC1101 };
    Transport selectedTransport = Transport::ESP_NOW;
    Transport pendingTransport = Transport::ESP_NOW;

    enum class ProximityUpdateState : uint8_t { READY, CHECKING };
    ProximityUpdateState proximityUpdateState = ProximityUpdateState::READY;
    enum class ProximityClassification : uint8_t { UNKNOWN, CLOSE, FAR };
    ProximityClassification proximityClassification = ProximityClassification::UNKNOWN;
    void startKnownHeartbeatCadence(uint32_t now);
    bool automaticSelectionPending = false;
    bool espNowFallbackPending = false;
    // PROVISIONAL conservative RSSI boundaries; these do not represent meters.
    constexpr int8_t ENTER_FAR_DBM = -80;
    constexpr int8_t ENTER_CLOSE_DBM = -75;
    struct ProximitySample { uint16_t messageId; int8_t rssi; };
    ProximitySample proximitySamples[3]{};
    uint8_t proximitySampleCount = 0;
    uint32_t checkStartedAt = 0;
    // PROVISIONAL overall sampling budget; physically evaluate traffic cadence.
    constexpr uint32_t CHECK_TIMEOUT_MS = 12000;

    constexpr uint32_t PROBE_REPLY_WAIT_MS = 500;
    bool probeOutstanding = false;
    bool probeTimerActive = false; // Also spaces attempts after immediate TX rejection.
    uint16_t probeMessageId = 0;
    uint32_t probeStartedAt = 0;

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
        for (auto& sample : proximitySamples) sample = {};
    }

    void cancelProximityCheck(const char* reason)
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
        if (buttonHeartbeatPending ||
            PowerManager::localState() != PowerManager::LocalState::ACTIVE ||
            PowerManager::peerState() == PowerManager::PeerState::OFFLINE)
            return;
        // Keep failed-entry recovery pending until application/control work and
        // callbacks drain. An existing measurement can absorb it without restart.
        if (PowerManager::sleepRecoveryCheckPending() &&
            proximityUpdateState != ProximityUpdateState::CHECKING &&
            (movementState != MovementState::READY || sleepDrainWaiting || sleepTransportBlockedReason()))
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
            noteLocalActivity(now);
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
        if ((selectedTransport != Transport::CC1101 &&
             proximityClassification != ProximityClassification::UNKNOWN) ||
            proximityUpdateState != ProximityUpdateState::CHECKING)
        {
            resetProximityProbe();
            return;
        }
        if (probeTimerActive && uint32_t(now - probeStartedAt) < PROBE_REPLY_WAIT_MS)
            return;

        // Expiry abandons the old ID. A rejected send still consumes this opportunity.
        resetProximityProbe();
        const Protocol::Message probe{Protocol::VERSION, Protocol::MessageType::ProximityProbe,
            nextMessageId++, LOCAL_DEVICE, Protocol::EventType::None, 0};
        probeMessageId = probe.messageId;
        probeStartedAt = now;
        probeTimerActive = true;
        probeOutstanding = ESPNowRadio::send(reinterpret_cast<const uint8_t*>(&probe), sizeof(probe));
    }

    void sampleProximity(const ESPNowRadio::RssiObservation& observation, uint32_t now)
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
        const bool probeReply = selectedTransport == Transport::CC1101 ||
            (proximityClassification == ProximityClassification::UNKNOWN &&
             observation.message.type == Protocol::MessageType::ProximityProbeReply);
        if (probeReply)
        {
            const auto& message = observation.message;
            if (!probeOutstanding || message.version != Protocol::VERSION ||
                message.type != Protocol::MessageType::ProximityProbeReply ||
                message.event != Protocol::EventType::None ||
                message.ackForMessageId != probeMessageId ||
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
        Serial.printf("PROXIMITY CHECK | SAMPLE | n=%u | rssi=%d dBm\n",
                      proximitySampleCount, static_cast<int>(observation.rssi));
        if (proximitySampleCount != 3) return;
        int8_t a = proximitySamples[0].rssi, b = proximitySamples[1].rssi, c = proximitySamples[2].rssi;
        if (a > b) { const int8_t temp = a; a = b; b = temp; }
        if (b > c) { const int8_t temp = b; b = c; c = temp; }
        if (a > b) { const int8_t temp = a; a = b; b = temp; }
        Serial.printf("PROXIMITY CHECK | COMPLETE | samples=3 | median=%d dBm\n", static_cast<int>(b));
        const bool firstClassification = proximityClassification == ProximityClassification::UNKNOWN;
        if (proximityClassification == ProximityClassification::FAR)
        {
            if (b >= ENTER_CLOSE_DBM) proximityClassification = ProximityClassification::CLOSE;
        }
        else // UNKNOWN uses the same conservative FAR boundary as CLOSE.
            proximityClassification = b <= ENTER_FAR_DBM ? ProximityClassification::FAR : ProximityClassification::CLOSE;
        if (firstClassification) startKnownHeartbeatCadence(now);
        automaticSelectionPending = true; // Every completed median is fresh policy evidence.
        espNowFallbackPending = false; // Fresh proximity evidence supersedes an older delivery failure.
        Serial.printf("PROXIMITY | median=%d dBm | %s\n", static_cast<int>(b),
                      proximityClassification == ProximityClassification::FAR ? "FAR" : "CLOSE");
        resetProximityCheck();
    }


    // ======================================================
    // Reliability settings
    // ======================================================

    constexpr unsigned long FIRST_EVENT_DELAY_MS =
        1000;

    constexpr unsigned long EVENT_INTERVAL_MS =
        2500;

    constexpr unsigned long FAR_EVENT_INTERVAL_MS = 6000;

    unsigned long automaticHeartbeatIntervalMs()
    {
        switch (proximityClassification)
        {
            case ProximityClassification::CLOSE: return EVENT_INTERVAL_MS;
            case ProximityClassification::FAR: return FAR_EVENT_INTERVAL_MS;
            default: return 0; // UNKNOWN has no automatic heartbeat cadence.
        }
    }

    constexpr unsigned long ACK_TIMEOUT_MS =
        300;

    constexpr uint8_t MAX_RETRIES =
        2;


    // ======================================================
    // Temporary reliability test
    //
    // TRUE:
    // Each device intentionally drops its first ACK.
    //
    // This forces:
    // timeout -> retry -> duplicate detection -> ACK again
    //
    // After proving reliability, change this to false.
    // ======================================================

    constexpr bool DROP_FIRST_ACK_FOR_TEST =
        false;


    bool testAckAlreadyDropped =
        false;


    // ======================================================
    // Protocol state
    // ======================================================

    const char* transportName(Transport transport)
    { return transport == Transport::ESP_NOW ? "ESP-NOW" : "CC1101"; }

    Protocol::Message pendingMessage{};


    bool waitingForAck =
        false;


    uint8_t retryCount =
        0;


    unsigned long ackWaitStart =
        0;


    unsigned long nextEventTime =
        0;

    void startKnownHeartbeatCadence(uint32_t now)
    {
        // A pending transaction keeps its clock and schedules normally on completion.
        if (!waitingForAck) nextEventTime = now + automaticHeartbeatIntervalMs();
    }

    // Bounded transport outbox, owned by loop(), just like pendingMessage.
    // One packet at a time uses the existing 300 ms / two-retry machinery.
    struct QueuedControl
    {
        Protocol::Message message;
        uint32_t notBefore;
    };
    constexpr size_t CONTROL_QUEUE_LENGTH = 4;
    QueuedControl controlQueue[CONTROL_QUEUE_LENGTH];
    size_t controlCount = 0;


    // ======================================================
    // Duplicate detection
    // ======================================================

    bool haveLastPeerEvent =
        false;


    uint16_t lastPeerEventId =
        0;

    // Awake user dedup also survives interleaved background EVENTs. No visual queue.
    uint16_t recentUserEventIds[RX_QUEUE_LENGTH]{};
    uint32_t recentUserEventTimes[RX_QUEUE_LENGTH]{};
    constexpr uint32_t USER_EVENT_HISTORY_MS = 10000; // Covers retries; never retains IDs across allocator wrap.
    UBaseType_t recentUserEventCount = 0, nextUserEventSlot = 0;


    // ======================================================
    // Device name helper
    // ======================================================

    const char* deviceName(
        Protocol::DeviceId device
    )
    {
        switch (device)
        {
            case Protocol::DeviceId::Bubu:

                return "BUBU";


            case Protocol::DeviceId::Dudu:

                return "DUDU";


            default:

                return "UNKNOWN";
        }
    }


    // ======================================================
    // Send one Protocol::Message
    // ======================================================

    bool sendProtocolMessage(
        const Protocol::Message& message,
        Transport transport = Transport::ESP_NOW
    )
    {
        if (transport == Transport::CC1101)
        {
            const auto result = CC1101WakeRecovery::submitAwake(message);
            if (result != CC1101WakeRecovery::SubmitResult::Accepted)
                Serial.printf("CC1101 APP TX | %s | id=%u\n",
                              result == CC1101WakeRecovery::SubmitResult::Busy ? "BUSY" : "FAILED", message.messageId);
            return result == CC1101WakeRecovery::SubmitResult::Accepted;
        }
        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(
                &message
            );


        return ESPNowRadio::send(
            bytes,
            sizeof(message)
        );
    }


    // ======================================================
    // Send ACK
    // ======================================================

    bool sendAck(
        uint16_t receivedMessageId,
        Transport transport = Transport::ESP_NOW
    )
    {
        Protocol::Message ack{};


        ack.version =
            Protocol::VERSION;


        ack.type =
            Protocol::MessageType::Ack;


        ack.messageId =
            nextMessageId++;


        ack.sender =
            LOCAL_DEVICE;


        ack.event =
            Protocol::EventType::None;


        ack.ackForMessageId =
            receivedMessageId;


        bool accepted =
            sendProtocolMessage(
                ack, transport
            );


        if (accepted)
        {
            Serial.printf(
                "TX ACK | id=%u | ackFor=%u | via=%s\n",
                ack.messageId,
                ack.ackForMessageId, transportName(transport)
            );
        }
        else
        {
            Serial.printf(
                "TX ACK REQUEST FAILED | ackFor=%u\n",
                receivedMessageId
            );
        }
        return accepted;
    }


    // ======================================================
    // Transmit pending EVENT or reliable sleep control
    // ======================================================

    void transmitPendingMessage(
        bool retry
    )
    {
        if (Protocol::isSleepControl(pendingMessage.type))
        {
            // Recheck at the actual send boundary, including retries. Time
            // spent draining/logging other packets must not send expired work.
            PowerManager::update(millis());
            if (!PowerManager::controlStillNeeded(pendingMessage.type, pendingMessage.ackForMessageId))
            {
                waitingForAck = false;
                retryCount = 0;
                return;
            }
        }
        bool accepted =
            sendProtocolMessage(
                pendingMessage, pendingTransport
            );


        /*
         * Start / restart the ACK timeout timer.
         */
        ackWaitStart =
            millis();


        if (Protocol::isSleepControl(pendingMessage.type))
        {
            Serial.printf("POWER TX %s | sleepId=%u messageId=%u retry=%u\n",
                          Protocol::controlName(pendingMessage.type), pendingMessage.ackForMessageId,
                          pendingMessage.messageId, retryCount);
        }
        else if (retry)
        {
            Serial.printf(
                "RETRY %u/%u"
                " | id=%u"
                " | via=%s | waiting for ACK\n",
                retryCount,
                MAX_RETRIES,
                pendingMessage.messageId, transportName(pendingTransport)
            );
        }
        else
        {
            Serial.printf(
                "TX EVENT"
                " | sender=%s"
                " | id=%u"
                " | via=%s | waiting for ACK\n",
                deviceName(
                    LOCAL_DEVICE
                ),
                pendingMessage.messageId, transportName(pendingTransport)
            );
        }


        if (!accepted)
        {
            Serial.printf("WARNING: %s TX request failed\n", transportName(pendingTransport));
        }
        else if (Protocol::isSleepControl(pendingMessage.type))
        {
            PowerManager::controlSent(pendingMessage.type, pendingMessage.ackForMessageId, millis());
        }
    }


    void discardObsoleteControls()
    {
        if (waitingForAck && Protocol::isSleepControl(pendingMessage.type) &&
            !PowerManager::controlStillNeeded(pendingMessage.type, pendingMessage.ackForMessageId))
        {
            Serial.printf("POWER: retire superseded/cancelled TX | messageId=%u\n", pendingMessage.messageId);
            waitingForAck = false;
            retryCount = 0;
        }
        size_t kept = 0;
        for (size_t i = 0; i < controlCount; ++i)
        {
            const auto& message = controlQueue[i].message;
            if (PowerManager::controlStillNeeded(message.type, message.ackForMessageId))
                controlQueue[kept++] = controlQueue[i];
        }
        controlCount = kept;
    }


    bool queueSleepControl(Protocol::MessageType type, uint16_t sleepId)
    {
        if (!protocolReady)
            return false;
        discardObsoleteControls();
        if (type != Protocol::MessageType::SleepCancel)
        {
            // Repeated semantic responses reuse the outstanding packet/retry
            // budget; duplicates cannot keep refreshing its delivery timer.
            if (waitingForAck && pendingMessage.type == type && pendingMessage.ackForMessageId == sleepId)
                return true;
            for (size_t i = 0; i < controlCount; ++i)
                if (controlQueue[i].message.type == type && controlQueue[i].message.ackForMessageId == sleepId)
                    return true;
            if (controlCount == CONTROL_QUEUE_LENGTH)
                return false;
        }

        Protocol::Message message{};
        message.version = Protocol::VERSION;
        message.type = type;
        message.messageId = type == Protocol::MessageType::SleepRequest ? sleepId : nextMessageId++;
        message.sender = LOCAL_DEVICE;
        message.event = Protocol::EventType::None;
        message.ackForMessageId = sleepId;
        if (type == Protocol::MessageType::SleepCancel)
        {
            const bool accepted = sendProtocolMessage(message);
            Serial.printf("POWER TX SLEEP_CANCEL | sleepId=%u messageId=%u best-effort accepted=%d\n",
                          sleepId, message.messageId, accepted);
            return accepted;
        }
        controlQueue[controlCount++] = {message, uint32_t(millis())};
        return true;
    }


    void sendNextControl()
    {
        if (waitingForAck || controlCount == 0 ||
            uint32_t(uint32_t(millis()) - controlQueue[0].notBefore) >= 0x80000000UL)
            return;
        pendingMessage = controlQueue[0].message;
        pendingTransport = Transport::ESP_NOW; // Sleep controls never follow the application selector.
        for (size_t i = 1; i < controlCount; ++i)
            controlQueue[i - 1] = controlQueue[i];
        --controlCount;
        retryCount = 0;
        waitingForAck = true;
        transmitPendingMessage(false);
    }


    // ======================================================
    // Create a new heartbeat EVENT
    // ======================================================

    bool backgroundHeartbeatAllowed()
    {
        return proximityClassification != ProximityClassification::UNKNOWN &&
            PowerManager::automaticHeartbeatAllowed();
    }

    bool haveFarBackgroundHeartbeat = false;
    uint32_t lastFarBackgroundHeartbeatAt = 0;

    void requestBackgroundHeartbeat()
    {
        if (!backgroundHeartbeatAllowed()) return;
        if (proximityClassification == ProximityClassification::FAR)
        {
            if (led.userHeartbeatActive()) return;
            const uint32_t now = uint32_t(millis());
            // Share one FAR cadence across outgoing and incoming background events.
            // Suppressed requests neither restart the pulse nor postpone the next one.
            if (haveFarBackgroundHeartbeat &&
                uint32_t(now - lastFarBackgroundHeartbeatAt) < FAR_EVENT_INTERVAL_MS) return;
            haveFarBackgroundHeartbeat = true;
            lastFarBackgroundHeartbeatAt = now;
        }
        led.requestHeartbeat();
    }

    void startHeartbeatEvent(Transport transport = selectedTransport,
                             Protocol::EventType event = Protocol::EventType::Heartbeat)
    {
        PowerManager::applicationTxStarted(); // Retries never pass this boundary.
        pendingTransport = transport;
        pendingMessage.version =
            Protocol::VERSION;


        pendingMessage.type =
            Protocol::MessageType::Event;


        pendingMessage.messageId =
            nextMessageId++;


        pendingMessage.sender =
            LOCAL_DEVICE;


        pendingMessage.event =
            event;


        pendingMessage.ackForMessageId =
            0;


        retryCount =
            0;


        waitingForAck =
            true;

        // Button intent animates only its receiver.
        // Retries only call transmitPendingMessage(), so cannot replay either request.
        if (event == Protocol::EventType::Heartbeat)
            requestBackgroundHeartbeat();

        transmitPendingMessage(
            false
        );
    }


    // ======================================================
    // Handle received ACK
    // ======================================================

    void handleAck(
        const Protocol::Message& message, Transport transport
    )
    {
        Serial.printf(
            "RX ACK"
            " | from=%s"
            " | ackFor=%u"
            " | ackMessageId=%u\n",
            deviceName(
                message.sender
            ),
            message.ackForMessageId,
            message.messageId
        );


        /*
         * An ACK only counts if it belongs to the EVENT
         * we are currently waiting for.
         */
        if (
            waitingForAck && transport == pendingTransport &&
            message.ackForMessageId ==
                pendingMessage.messageId
        )
        {
            Serial.printf(
                "ACK MATCHED"
                " | message=%u | via=%s\n",
                pendingMessage.messageId, transportName(transport)
            );

            if (pendingMessage.type == Protocol::MessageType::Event)
                PowerManager::applicationTxAcknowledged();


            waitingForAck =
                false;


            retryCount =
                0;


            nextEventTime =
                millis() +
                automaticHeartbeatIntervalMs();


            return;
        }


        Serial.println(
            "ACK IGNORED:"
            " does not match pending message"
        );
    }


    // ======================================================
    // Handle received EVENT
    // ======================================================

    bool handleEvent(
        const Protocol::Message& message,
        bool sendReceipt = true, Transport transport = Transport::ESP_NOW
    )
    {
        bool duplicate =
            haveLastPeerEvent &&
            message.messageId ==
                lastPeerEventId;

        if (message.event == Protocol::EventType::UserHeartbeat)
            for (UBaseType_t i = 0; i < recentUserEventCount; ++i)
                duplicate = duplicate || (recentUserEventIds[i] == message.messageId &&
                    uint32_t(uint32_t(millis()) - recentUserEventTimes[i]) < USER_EVENT_HISTORY_MS);


        // --------------------------------------------------
        // Duplicate EVENT
        // --------------------------------------------------

        if (duplicate)
        {
            Serial.printf(
                "RX DUPLICATE"
                " | sender=%s"
                " | id=%u"
                " | event ignored"
                " | ACK again\n",
                deviceName(
                    message.sender
                ),
                message.messageId
            );


            /*
             * CRITICAL:
             *
             * Do NOT execute the heartbeat/event again.
             *
             * The sender probably retried because our
             * previous ACK was lost.
             *
             * Therefore ACK the duplicate again.
             */
            if (sendReceipt) sendAck(message.messageId, transport);


            return false;
        }


        // --------------------------------------------------
        // New EVENT
        // --------------------------------------------------

        haveLastPeerEvent =
            true;


        lastPeerEventId =
            message.messageId;

        if (message.event == Protocol::EventType::UserHeartbeat)
        {
            recentUserEventIds[nextUserEventSlot] = message.messageId;
            recentUserEventTimes[nextUserEventSlot] = uint32_t(millis());
            nextUserEventSlot = (nextUserEventSlot + 1) % RX_QUEUE_LENGTH;
            if (recentUserEventCount < RX_QUEUE_LENGTH) ++recentUserEventCount;
        }

        // Periodic Heartbeat is background traffic, not local user activity.
        PowerManager::applicationEvent(millis());


        Serial.printf(
            "RX NEW EVENT"
            " | sender=%s"
            " | id=%u"
            " | event=%u\n",
            deviceName(
                message.sender
            ),
            message.messageId,
            static_cast<uint8_t>(
                message.event
            )
        );


        // Runtime requests happen only after validation and duplicate rejection.
        // Only explicit user intent gets priority, regardless of local distance.
        // Retained delivery records its visual obligation in handleWakeEvent().
        if (sendReceipt && message.type == Protocol::MessageType::Event &&
            message.event == Protocol::EventType::UserHeartbeat)
        {
            led.requestUserHeartbeat();
            Serial.printf("PARTNER LED | USER HEARTBEAT | id=%u | via=%s\n",
                          message.messageId, transportName(transport));
        }
        else if (sendReceipt && message.event == Protocol::EventType::Heartbeat)
            requestBackgroundHeartbeat();


        // --------------------------------------------------
        // TEMPORARY TEST:
        //
        // Deliberately lose the first ACK so that we can
        // physically prove retry + duplicate handling.
        // --------------------------------------------------

        if (
            sendReceipt && transport == Transport::ESP_NOW && DROP_FIRST_ACK_FOR_TEST &&
            !testAckAlreadyDropped
        )
        {
            testAckAlreadyDropped =
                true;


            Serial.printf(
                "TEST: intentionally dropping ACK"
                " | event=%u\n",
                message.messageId
            );


            return true;
        }


        // --------------------------------------------------
        // Normal ACK
        // --------------------------------------------------

        if (sendReceipt) sendAck(message.messageId, transport);
        return true;
    }


    // ======================================================
    // Process incoming ESP-NOW data
    // ======================================================

    // Runs in the Wi-Fi task. Copy bytes only; loop() owns protocol state.
    void queueReceivedData(const uint8_t* data, size_t length)
    {
        if (length != sizeof(Protocol::Message))
        {
            return;
        }

        Protocol::Message message{};
        memcpy(&message, data, sizeof(message));

        // Never block the Wi-Fi task. If full, drop without ACKing;
        // the sender's existing timeout/retry logic can retry the packet.
        (void)xQueueSend(receiveQueue, &message, 0);
    }


    void startPeerAvailabilityProximityCheck(PowerManager::PeerState previousPeer)
    {
        // Recovery may arrive before a deferred fallback switches the selected radio.
        const bool recoveringPeer = previousPeer == PowerManager::PeerState::OFFLINE &&
            (selectedTransport == Transport::CC1101 || espNowFallbackPending);
        const bool firstContact = previousPeer == PowerManager::PeerState::UNKNOWN &&
            proximityClassification == ProximityClassification::UNKNOWN;
        if (PowerManager::peerState() == PowerManager::PeerState::ONLINE &&
            (recoveringPeer || firstContact))
            startProximityCheck(uint32_t(millis())); // Existing guards and loop-owned probes remain authoritative.
    }

    void handleReceivedData(
        const uint8_t* data,
        size_t length, Transport transport = Transport::ESP_NOW
    )
    {
        // --------------------------------------------------
        // Packet size validation
        // --------------------------------------------------

        if (
            length !=
            sizeof(Protocol::Message)
        )
        {
            Serial.printf(
                "RX INVALID SIZE | bytes=%u\n",
                static_cast<unsigned int>(
                    length
                )
            );


            return;
        }


        Protocol::Message message{};


        memcpy(
            &message,
            data,
            sizeof(message)
        );


        // --------------------------------------------------
        // Protocol version validation
        // --------------------------------------------------

        if (
            message.version !=
            Protocol::VERSION
        )
        {
            Serial.printf(
                "RX INVALID VERSION | version=%u\n",
                message.version
            );


            return;
        }


        // --------------------------------------------------
        // Sender validation
        // --------------------------------------------------

        if (
            message.sender !=
            PEER_DEVICE
        )
        {
            Serial.printf(
                "RX INVALID SENDER | sender=%u\n",
                static_cast<uint8_t>(
                    message.sender
                )
            );


            return;
        }


        if (message.type == Protocol::MessageType::ProximityProbe ||
            message.type == Protocol::MessageType::ProximityProbeReply)
        {
            // ESP-NOW-only best effort; never enter application or sleep reliability.
            if (transport != Transport::ESP_NOW || message.event != Protocol::EventType::None)
                return;
            // Replies are correlated solely by the RSSI observation's complete message.
            if (message.type == Protocol::MessageType::ProximityProbeReply) return;
            if (message.ackForMessageId != 0) return;
            if (buttonHeartbeatPending) return; // Measurement replies yield to queued user intent.
            const auto state = PowerManager::localState();
            if (state != PowerManager::LocalState::ACTIVE)
                return;
            const Protocol::Message reply{Protocol::VERSION, Protocol::MessageType::ProximityProbeReply,
                nextMessageId++, LOCAL_DEVICE, Protocol::EventType::None, message.messageId};
            (void)ESPNowRadio::send(reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
            return;
        }

        if (Protocol::isSleepControl(message.type))
        {
            if (transport != Transport::ESP_NOW) return;
            if (message.event != Protocol::EventType::None ||
                (message.type == Protocol::MessageType::SleepRequest && message.ackForMessageId != message.messageId))
            {
                Serial.println("POWER: malformed control rejected");
                return;
            }
            // Sample fresh admission BEFORE this packet creates its own ACK TX.
            // An existing transaction retains duplicate/collision/phase handling.
            const bool freshRequest = message.type == Protocol::MessageType::SleepRequest &&
                !PowerManager::transaction().active;
            const bool admitFreshRequest = freshRequest && productSleepEligible(uint32_t(millis()),
                AUTOMATIC_SLEEP_INACTIVITY_MS - AUTOMATIC_SLEEP_PEER_GRACE_MS);
            // Receipt ACK is independent of semantic acceptance. Duplicates
            // and stale controls are acknowledged, then evaluated by the FSM.
            const bool receiptAccepted = sendAck(message.messageId);
            PowerManager::handleControl(message, millis(), admitFreshRequest);
            if (freshRequest && PowerManager::transaction().active)
                automaticSleepArmed = false; // Participation also consumes our automatic initiation opportunity.
            if (!receiptAccepted && PowerManager::localState() == PowerManager::LocalState::SLEEPING)
            {
                Serial.println("COORDINATED DEEP SLEEP | ABORTED | reason=RECEIPT_TX_REJECTED");
                PowerManager::notifySleepExecutionFailed(millis());
            }
            return;
        }

        // Dispatch existing EVENT / receipt ACK without changing wire behavior.
        // A new EVENT can mark ONLINE inside handleEvent(), before notePeerSeen().
        const auto previousPeer = PowerManager::peerState();

        switch (
            message.type
        )
        {
            case Protocol::MessageType::Event:

                if (!Protocol::isHeartbeat(message.event) || message.ackForMessageId != 0) break;
                handleEvent(
                    message, true, transport
                );
                PowerManager::notePeerSeen();
                startPeerAvailabilityProximityCheck(previousPeer);

                break;


            case Protocol::MessageType::Ack:
            {
                if (message.event != Protocol::EventType::None) break;
                const bool heartbeatAcknowledged = waitingForAck && transport == pendingTransport &&
                    pendingMessage.type == Protocol::MessageType::Event &&
                    message.ackForMessageId == pendingMessage.messageId;
                handleAck(
                    message, transport
                );
                // A late receipt for COMMIT/CANCEL does not resolve uncertainty
                // about the peer's semantic sleep state.
                if (heartbeatAcknowledged)
                {
                    PowerManager::notePeerSeen();
                    startPeerAvailabilityProximityCheck(previousPeer);
                }

                break;
            }


            default:

                Serial.printf(
                    "RX UNKNOWN TYPE | type=%u\n",
                    static_cast<uint8_t>(
                        message.type
                    )
                );

                break;
        }
    }


    // ======================================================
    // ACK timeout + retry
    // ======================================================

    void handleAckTimeout()
    {
        if (!waitingForAck)
        {
            return;
        }


        // --------------------------------------------------
        // Still inside 300 ms waiting period.
        // --------------------------------------------------

        if (
            millis() -
            ackWaitStart <
            ACK_TIMEOUT_MS
        )
        {
            return;
        }


        Serial.printf(
            "ACK TIMEOUT"
            " | message=%u\n",
            pendingMessage.messageId
        );


        // --------------------------------------------------
        // Retry available
        // --------------------------------------------------

        if (
            retryCount <
            MAX_RETRIES
        )
        {
            retryCount++;


            /*
             * IMPORTANT:
             *
             * We do NOT create a new message.
             *
             * We resend pendingMessage.
             *
             * Therefore the message ID remains identical.
             */
            transmitPendingMessage(
                true
            );


            return;
        }


        // --------------------------------------------------
        // Retries exhausted
        // --------------------------------------------------

        Serial.printf(
            "GIVE UP"
            " | message=%u"
            " | retries=%u exhausted\n",
            pendingMessage.messageId,
            MAX_RETRIES
        );

        waitingForAck =
            false;


        retryCount =
            0;

        if (Protocol::isSleepControl(pendingMessage.type))
        {
            PowerManager::controlFailed(pendingMessage.type, pendingMessage.ackForMessageId, millis());
            if (pendingMessage.type == Protocol::MessageType::SleepAck &&
                PowerManager::localState() == PowerManager::LocalState::SLEEPING)
            {
                Serial.println("COORDINATED DEEP SLEEP | ABORTED | reason=FINAL_RECEIPT_TIMEOUT");
                PowerManager::notifySleepExecutionFailed(millis());
            }
        }
        else
        {
            PowerManager::notePeerUnreachable();
            if (pendingMessage.type == Protocol::MessageType::Event && pendingTransport == Transport::ESP_NOW)
            {
                espNowFallbackPending = true; // The exhausted EVENT stays finished on its original radio.
                automaticSelectionPending = false;
            }
        }


        nextEventTime =
            millis() +
            automaticHeartbeatIntervalMs();
    }


    // Display alone may ignore a stopped CC1101's persistent busy indication.
    // Continue checking every remaining blocker; the first reason is returned.
    const char* transportBlockedReason(bool allowStoppedCc1101)
    {
        if (!protocolReady) return "RUNTIME_NOT_READY";
        if (CC1101WakeTx::deferredPending()) return "CC1101_WAKE_EVENTS_PENDING";
        if (CC1101WakeRecovery::awakeBusy() && !(allowStoppedCc1101 && CC1101WakeRecovery::awakeStopped()))
            return "CC1101_RUNTIME_BUSY";
        if (waitingForAck) return "ACK_PENDING";
        if (controlCount != 0) return "CONTROL_QUEUED";
        if (PowerManager::transaction().active) return "TRANSACTION_ACTIVE";
        if (ESPNowRadio::txInFlight() != 0) return "TX_IN_FLIGHT";
        if (ESPNowRadio::receiveCallbackActive()) return "RX_CALLBACK_ACTIVE";
        if (!receiveQueue) return "RX_QUEUE_MISSING";
        if (uxQueueMessagesWaiting(receiveQueue) != 0) return "RX_QUEUED";
        return nullptr;
    }

    // nullptr means drained. Keep sleep admission/final entry and all other
    // existing callers strict, including when CC1101 is stopped.
    const char* sleepTransportBlockedReason()
    {
        return transportBlockedReason(false);
    }

    const char* sleepEntryBlockedReason()
    {
        if (const char* reason = buttonSleepBlockedReason()) return reason;
        if (const char* reason = sleepTransportBlockedReason()) return reason;
        if (digitalRead(MOTION_INT1_PIN) != 0) return "MOTION_INT1_HIGH";
        return nullptr;
    }

    bool productSleepEligible(uint32_t now, uint32_t requiredInactivityMs)
    {
        return uint32_t(now - lastMeaningfulActivity) >= requiredInactivityMs &&
            buttonSleepBlockedReason() == nullptr &&
            motionReady && movementState == MovementState::READY &&
            proximityUpdateState != ProximityUpdateState::CHECKING &&
            PowerManager::automaticHeartbeatAllowed() && PowerManager::cooldownLeftMs(now) == 0 &&
            sleepTransportBlockedReason() == nullptr;
    }

    void serviceAutomaticSleep()
    {
        const uint32_t now = uint32_t(millis());
        if (!automaticSleepArmed || !productSleepEligible(now)) return;
        if (PowerManager::requestSleep(nextMessageId++, now))
            automaticSleepArmed = false;
    }

    void showDeepSleepStatus()
    {
        if (!displayReady) return;
#ifdef DEVICE_DUDU
        noteDisplayAttempt();
#endif
        display.showDeepSleep(DEVICE_NAME);
        // An aborted entry must repaint even if the awake fields are unchanged.
        displayedStatus = {};
    }

    void enterPhysicalSleep()
    {
        led.off(); // Sleep wins immediately; animation is never a drain condition.
        resetMovement(); // An aborted attempt must not retain an old settle timer.
        cancelProximityCheck("SLEEP");
        if (motion.prepareForSleep())
        {
            Serial.println("MOTION SLEEP ARM | READY | GPIO3 LOW | activity only | INT_ENABLE=0x10 POWER_CTL=0x08");
            CC1101WakeRecovery::enterDeepSleep(saveRtcHistory, sleepEntryBlockedReason,
                                               showDeepSleepStatus);
        }
        else
        {
            RtcState::invalidate();
            Serial.println("COORDINATED DEEP SLEEP | ABORTED | reason=MOTION_NOT_READY_OR_INT1_HIGH");
        }
        // Successful deep sleep reboots. Every return restores awake sensing.
        Serial.printf("MOTION SLEEP ARM | CANCELLED | awake_restore=%s\n",
                      motion.cancelSleepPreparation() ? "OK" : "FAILED");
    }

    void serviceSleepExecution()
    {
        if (PowerManager::localState() != PowerManager::LocalState::SLEEPING)
        {
            sleepDrainWaiting = false;
            return;
        }
        if (!sleepDrainWaiting)
        {
            sleepDrainWaiting = true;
            sleepDrainStarted = millis();
        }
        if (const char* reason = sleepTransportBlockedReason())
        {
            if (uint32_t(millis() - sleepDrainStarted) >= SLEEP_DRAIN_TIMEOUT_MS)
            {
                Serial.printf("COORDINATED DEEP SLEEP | ABORTED | reason=DRAIN_TIMEOUT/%s\n", reason);
                PowerManager::notifySleepExecutionFailed(millis());
                sleepDrainWaiting = false;
            }
            return;
        }
        // Catch Activity arriving while this iteration processed radio work.
        // Existing activity semantics revoke an unconsumed SLEEPING decision.
        serviceMotion();
        serviceButton();
        if (PowerManager::localState() != PowerManager::LocalState::SLEEPING)
        {
            sleepDrainWaiting = false;
            return;
        }
        if (buttonSleepBlockedReason()) return; // Keep raw LOW/pending intent ahead of decision consumption.
        PowerManager::SleepDecision decision{};
        if (!PowerManager::takeSleepDecision(decision)) return;
        Serial.printf("SLEEP EXECUTION READY | sleepId=%u | role=%s\n",
                      decision.sleepId, PowerManager::toString(decision.role));
        Serial.println("SLEEP TRANSPORT DRAINED");
        enterPhysicalSleep();
        // Successful deep sleep reboots. A return always means entry failed.
        PowerManager::notifySleepExecutionFailed(millis());
        sleepDrainWaiting = false;
    }

    // Shared motion/button wake request. The transmitter owns the entire bounded
    // retry episode; this layer allocates exactly one EVENT after runtime guards.
    void serviceDeferredWakeEvents();

    CC1101WakeTx::Report requestPeerWake()
    {
        CC1101WakeTx::Report refused;
        refused.result = CC1101WakeTx::Result::Busy;
        if (CC1101WakeTx::deferredPending() || CC1101WakeRecovery::awakeBusy())
        {
            Serial.println("CC1101 WAKE TX | REFUSED | awake CC1101 operation pending");
            return refused;
        }
        if (!protocolReady || waitingForAck || controlCount != 0 ||
            uxQueueMessagesWaiting(receiveQueue) != 0 || PowerManager::transaction().active ||
            !PowerManager::automaticHeartbeatAllowed())
        {
            Serial.println("CC1101 WAKE TX | REFUSED | require ACTIVE and drained transport/RX queue");
            return refused;
        }
        const Protocol::Message event{Protocol::VERSION, Protocol::MessageType::Event,
            nextMessageId++, LOCAL_DEVICE, Protocol::EventType::Heartbeat, 0};
        const auto result = CC1101WakeTx::send(event, PEER_DEVICE);
        if (result.result == CC1101WakeTx::Result::Acked)
            Serial.printf("CC1101 WAKE ACK | id=%u | OK | RX_READY=%d\n", event.messageId, result.rxReady);
        else
            Serial.printf("CC1101 WAKE TX | id=%u | GIVE_UP | retries=%u | reason=%s | RX_READY=%d\n",
                event.messageId, result.attempts ? result.attempts - 1 : 0,
                CC1101WakeTx::toString(result.result), result.rxReady);
        serviceDeferredWakeEvents(); // send() has returned SPI ownership, even on failure.
        return result;
    }

    void receiveCc1101(const Protocol::Message& message)
    {
        handleReceivedData(reinterpret_cast<const uint8_t*>(&message), sizeof(message), Transport::CC1101);
    }

    void serviceDeferredWakeEvents()
    {
        if (!protocolReady || (CC1101WakeRecovery::awakeBusy() && !CC1101WakeRecovery::awakeStopped())) return;
        Protocol::Message event{};
        if (!CC1101WakeTx::takeDeferredEvent(event)) return;
        Serial.printf("CC1101 WAKE TX | FORWARD EVENT | id=%u | via=CC1101\n", event.messageId);
        // Normal validation/dedup/receiver LED and same-radio receipt. One per
        // boundary: finish its asynchronous ACK before forwarding another.
        // A stopped radio still permits delivery; failed receipts log explicitly.
        receiveCc1101(event);
    }

    bool applicationTransportPolicyBlocked()
    {
        // Let the pending recovery measurement arbitrate before old selection
        // evidence. A subsequently OFFLINE peer still needs normal fallback.
        return sleepTransportBlockedReason() || sleepDrainWaiting || !PowerManager::automaticHeartbeatAllowed() ||
            (PowerManager::sleepRecoveryCheckPending() && PowerManager::peerState() != PowerManager::PeerState::OFFLINE) ||
            proximityUpdateState == ProximityUpdateState::CHECKING;
    }

    void serviceAutomaticTransportSelection()
    {
        if (espNowFallbackPending)
        {
            if (applicationTransportPolicyBlocked()) return;
            espNowFallbackPending = false;
            automaticSelectionPending = false; // Defensive precedence if both requests are present.
            if (selectedTransport == Transport::CC1101) return;
            selectedTransport = Transport::CC1101;
            Serial.println("APP TRANSPORT FALLBACK | selected=CC1101 | reason=ESP_NOW_EVENT_GIVE_UP");
            return;
        }
        if (!automaticSelectionPending) return;
        if (proximityClassification == ProximityClassification::UNKNOWN)
        {
            automaticSelectionPending = false;
            return;
        }
        const Transport target = proximityClassification == ProximityClassification::CLOSE ?
            Transport::ESP_NOW : Transport::CC1101;
        if (target == selectedTransport)
        {
            automaticSelectionPending = false;
            return;
        }
        // Apply only at the awake/drained boundary.
        // Also keep each proximity measurement on one sampling mode.
        if (applicationTransportPolicyBlocked()) return;
        selectedTransport = target;
        automaticSelectionPending = false;
        Serial.printf("APP TRANSPORT AUTO | selected=%s | proximity=%s\n", transportName(selectedTransport),
                      proximityClassification == ProximityClassification::FAR ? "FAR" : "CLOSE");
    }

    void serviceButtonHeartbeat()
    {
        if (!buttonHeartbeatPending) return;
        // Intent preempts measurement, but never an in-flight radio packet or ACK.
        cancelProximityCheck("BUTTON_HEARTBEAT");
        if (buttonWakeIntentHeld)
        {
            if (uint32_t(millis() - buttonWakeHeldAt) >= BUTTON_WAKE_DRAIN_TIMEOUT_MS)
            {
                failButtonWake("DRAIN_TIMEOUT");
                return;
            }
            if (CC1101WakeRecovery::awakeStopped())
            {
                failButtonWake("CC1101_RUNTIME_STOPPED");
                return;
            }
            // Drain coincident traffic first; don't skip a received user pulse
            // while the existing synchronous wake sender waits for its ACK.
            if (sleepTransportBlockedReason() || sleepDrainWaiting ||
                !PowerManager::automaticHeartbeatAllowed() || led.userHeartbeatActive()) return;
            Serial.println("BUTTON WAKE | PEER_WAKE | one bounded episode");
            const auto result = requestPeerWake();
            const bool peerAcked = result.result == CC1101WakeTx::Result::Acked;
            Serial.printf("BUTTON WAKE | RESULT | peer_ack=%d | RX_READY=%d | attempts=%u\n",
                          peerAcked, result.rxReady, result.attempts);
            if (!peerAcked || !result.rxReady)
            {
                failButtonWake(peerAcked ? "LOCAL_RX_NOT_READY" : CC1101WakeTx::toString(result.result));
                return;
            }
            buttonWakeIntentHeld = buttonWakeRetryOnPress = false;
            Serial.println("BUTTON WAKE | HANDOFF | UserHeartbeat pending | delivery awaits application ACK");
        }
        if (sleepTransportBlockedReason() || sleepDrainWaiting || !PowerManager::automaticHeartbeatAllowed())
            return;
        serviceAutomaticTransportSelection();
        // UNKNOWN uses the long-range awake radio without asserting CLOSE/FAR.
        // submitAwake/serviceAwake retain ownership of preflight and RX recovery.
        const Transport transport = proximityClassification == ProximityClassification::UNKNOWN ?
            Transport::CC1101 : selectedTransport;
        buttonHeartbeatPending = false;
        Serial.printf("BUTTON HEARTBEAT | SEND | via=%s | proximity=%s\n", transportName(transport),
                      proximityClassification == ProximityClassification::UNKNOWN ? "UNKNOWN" : "KNOWN");
        startHeartbeatEvent(transport, Protocol::EventType::UserHeartbeat);
    }

    const char* displayPeerName(PowerManager::PeerState peer)
    {
        if (peer == PowerManager::PeerState::SLEEP_PENDING || peer == PowerManager::PeerState::SLEEPING)
            return "SLEEP";
        return PowerManager::toString(peer);
    }

    const char* displayDistanceName()
    {
        if (proximityUpdateState == ProximityUpdateState::CHECKING) return "CHECKING";
        switch (proximityClassification)
        {
            case ProximityClassification::CLOSE: return "CLOSE";
            case ProximityClassification::FAR: return "FAR";
            default: return "UNKNOWN";
        }
    }

    const char* displayStateName(PowerManager::LocalState state)
    {
        if (state == PowerManager::LocalState::SLEEP_NEGOTIATING) return "SLEEP NEG";
        if (state == PowerManager::LocalState::SLEEPING) return "SLEEP";
        return PowerManager::toString(state);
    }

    const char* displayMotionName()
    {
        if (!motionReady) return "N/A";
        switch (movementState)
        {
            case MovementState::MOVING: return "MOVING";
            case MovementState::WAITING: return "SETTLING";
            default: return "STILL";
        }
    }

#ifdef DEVICE_DUDU
    void reportDisplayDecision(const DisplaySnapshot& current, const DisplaySnapshot& cached, const char* reason)
    {
        const auto local = PowerManager::localState();
        if (local != PowerManager::LocalState::ACTIVE) return;
        const uint32_t now = uint32_t(millis());
        if (displayDiagnostic.sampled && uint32_t(now - displayDiagnostic.sampledAt) < 1000) return;
        displayDiagnostic.sampled = true;
        displayDiagnostic.sampledAt = now; // A dropped line also consumes this opportunity; never wait/retry.
        const char* classification = proximityClassification == ProximityClassification::UNKNOWN ? "UNKNOWN" :
            proximityClassification == ProximityClassification::CLOSE ? "CLOSE" : "FAR";
        // Tuple order: peer/distance/radio/local-state/motion. Cache is the input
        // to this decision, not evidence of a delivered or visible frame.
        char line[256];
        const int length = snprintf(line, sizeof(line),
            "OLED %s ms=%lu power=%s/%s prox=%s/%s want=%s/%s/%s/%s/%s cache=%s/%s/%s/%s/%s "
            "why=%s cc_stop=%u attempted=%lu last_ms=%lu\n",
            DEVICE_NAME, static_cast<unsigned long>(now), PowerManager::toString(local),
            PowerManager::toString(PowerManager::peerState()), classification,
            proximityUpdateState == ProximityUpdateState::CHECKING ? "CHECKING" : "READY",
            current.peer, current.distance, current.radio, current.state, current.motion,
            cached.peer ? cached.peer : "-", cached.distance ? cached.distance : "-",
            cached.radio ? cached.radio : "-", cached.state ? cached.state : "-", cached.motion ? cached.motion : "-",
            reason, unsigned(CC1101WakeRecovery::awakeStopped()),
            static_cast<unsigned long>(displayDiagnostic.attempts),
            static_cast<unsigned long>(displayDiagnostic.lastAttemptAt));
        // One bounded best-effort write; no flush, backlog or activity/sleep bookkeeping.
        if (length > 0 && size_t(length) < sizeof(line) && Serial.availableForWrite() >= length)
            (void)Serial.write(reinterpret_cast<const uint8_t*>(line), size_t(length));
    }
#endif

    void serviceDisplayStatus()
    {
        // No framebuffer transfer during protocol deadlines, sleep transitions
        // or an outstanding probe reply. Passive CHECKING may be shown when idle.
        // Only display permits STOPPED/busy; all other guards retain their order.
        const char* reason = !displayReady ? "DISPLAY_NOT_READY" : transportBlockedReason(true);
        if (!reason && sleepDrainWaiting) reason = "SLEEP_DRAIN";
        if (!reason && !PowerManager::automaticHeartbeatAllowed()) reason = "POWER_NOT_AWAKE";
        if (!reason && probeOutstanding) reason = "PROBE_PENDING";
        const DisplaySnapshot current{displayPeerName(PowerManager::peerState()), displayDistanceName(),
                                      transportName(selectedTransport), displayStateName(PowerManager::localState()),
                                      displayMotionName()};
#ifdef DEVICE_DUDU
        const auto cached = displayedStatus;
#endif
        if (!reason)
        {
            if (displayedStatus.peer && strcmp(current.peer, displayedStatus.peer) == 0 &&
                strcmp(current.distance, displayedStatus.distance) == 0 &&
                strcmp(current.radio, displayedStatus.radio) == 0 && strcmp(current.state, displayedStatus.state) == 0 &&
                strcmp(current.motion, displayedStatus.motion) == 0)
                reason = "UNCHANGED";
            else
            {
#ifdef DEVICE_DUDU
                noteDisplayAttempt();
#endif
                display.showStatus(DEVICE_NAME, current.peer, current.distance, current.radio, current.state, current.motion);
                displayedStatus = current;
                reason = "ATTEMPTED";
            }
        }
#ifdef DEVICE_DUDU
        reportDisplayDecision(current, cached, reason);
#endif
    }

}


// Save at the final coordinated-sleep boundary, after all packet IDs have been
// allocated; only real deep-wake startup restores this checkpoint.
void saveRtcHistory()
{
    RtcState::save({nextMessageId, haveLastPeerEvent, lastPeerEventId,
                    PowerManager::exportHistory()});
}

// Deep-wake startup only: call PowerManager::begin() first, then restore
// before enabling transport. Never reset CC1101 or runtime state to simulate it.
bool restoreRtcHistory()
{
    if (protocolReady || waitingForAck || controlCount != 0)
        return false;
    RtcState::History history{};
    if (!RtcState::load(history) || !PowerManager::restoreHistory(history.sleep))
        return false;
    nextMessageId = history.nextMessageId;
    haveLastPeerEvent = history.haveLastPeerEvent;
    lastPeerEventId = history.lastPeerEventId;
    RtcState::invalidate(); // Consume only after successful application.
    return true;
}


// ==========================================================
// Arduino setup
// ==========================================================

// Wake-only callback: use the existing EVENT dedup/delivery path, but return a
// CC1101 receipt. Wi-Fi is not initialized yet; no ESP-NOW send is attempted.
Protocol::Message handleWakeEvent(const Protocol::Message& event, bool& processed)
{
    processed = handleEvent(event, false);
    if (processed && event.type == Protocol::MessageType::Event && event.event == Protocol::EventType::UserHeartbeat)
    {
        retainedUserAnimationId = event.messageId;
        retainedUserAnimationPending = true; // One retained packet, before its receipt ACK.
        Serial.printf("PARTNER LED | DEFERRED USER HEARTBEAT | id=%u\n", event.messageId);
    }
    return {Protocol::VERSION, Protocol::MessageType::Ack, nextMessageId++,
            LOCAL_DEVICE, Protocol::EventType::None, event.messageId};
}

void setup()
{
    bootInfo = CC1101WakeRecovery::captureBoot(); // EARLIEST: before Serial/SPI.
    retainedUserAnimationPending = false;
    recentUserEventCount = nextUserEventSlot = 0; // RAM-only; retained dedup remains authoritative at wake.
    resetMovement(); // Startup sensor history is not a fresh awake movement.
    resetProximityCheck();
    proximityClassification = ProximityClassification::UNKNOWN; // RAM-only; no retained distance.
    automaticSelectionPending = false;
    espNowFallbackPending = false;
    selectedTransport = pendingTransport = Transport::ESP_NOW; // RAM-only; never restored from RTC.
    displayReady = false;
    motionReady = false;
    automaticSleepArmed = false;
    lastMeaningfulActivity = 0;
    displayedStatus = {}; // Request one initial draw when runtime is awake and drained.
#ifdef DEVICE_DUDU
    displayDiagnostic = {};
#endif
    Serial.begin(115200);
    if (bootInfo.wokeFromGpio(BUTTON_PIN))
        Serial.printf("BUTTON WAKE | DETECTED | GPIO5 | captured mask=0x%02llX\n",
                      static_cast<unsigned long long>(bootInfo.gpioMask));
    PowerManager::begin(LOCAL_DEVICE, queueSleepControl);
    if (bootInfo.deep)
    {
        // No USB delay, normal CC1101 begin, reset or configuration here.
        rtcRestored = restoreRtcHistory();
        if (!rtcRestored) RtcState::invalidate();
        // GPIO mask identifies the wake SOURCE, not whether FIFO data exists.
        // Inspect retained RX even for motion/timer (empty is normal); never
        // reset away a coincident packet. All GPIO wake inputs use this same path.
        wakeReport = CC1101WakeRecovery::recover(rtcRestored, PEER_DEVICE, handleWakeEvent);
        CC1101WakeRecovery::printReport(bootInfo, rtcRestored, wakeReport);
    }
    else
    {
        RtcState::invalidate();
        delay(1500);
        Serial.println("BOOT | COLD");
        const auto armInit = CC1101SleepArm::begin();
        Serial.printf("CC1101 ARM INIT | %s | CPU stays awake\n", CC1101SleepArm::toString(armInit));
    }
    // Recover and ACK any retained CC1101 wake packet before doing sensor I2C work.
    motionReady = motion.begin(MOTION_SDA_PIN, MOTION_SCL_PIN, MOTION_INT1_PIN);
    const int motionLevel = digitalRead(motion.getInterruptPin());
    if (motionReady)
        Serial.printf("MOTION INIT | OK (DEVID=0xE5) | GPIO%u INT1=%d | startup=%d "
                      "(0=None, 1=Activity, 2=Inactivity)\n",
                      motion.getInterruptPin(), motionLevel, static_cast<int>(motion.getStartupEvent()));
    else
        Serial.printf("MOTION INIT | FAILED (DEVID/config check) | GPIO%u INT1=%d | startup=UNAVAILABLE | continuing\n",
                      motion.getInterruptPin(), motionLevel);

    displayReady = display.begin();
    if (!displayReady) Serial.println("DISPLAY INIT | FAILED | continuing");
#ifdef DEVICE_DUDU
    // Keep Motion's configuration result separate from these later address checks.
    motion.reportStartupI2cHealth(display.diagnosticI2cAddress());
#endif
    led.begin(); // Retained wake recovery and shared-I2C hardware initialization are complete.
    if (retainedUserAnimationPending)
    {
        retainedUserAnimationPending = false;
        led.requestUserHeartbeat();
        Serial.printf("PARTNER LED | USER HEARTBEAT | id=%u | via=CC1101 | retained=1\n", retainedUserAnimationId);
    }
    beginButton();

    PowerManager::printStatus(millis());

    receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
    if (receiveQueue == nullptr)
    {
        Serial.println("ESP-NOW RECEIVE QUEUE CREATION FAILED");
        failButtonWake("RUNTIME_INIT_FAILED");
        return;
    }


    if (
        !ESPNowRadio::begin(
            queueReceivedData
        )
    )
    {
        Serial.println(
            "ESP-NOW STARTUP FAILED"
        );
        failButtonWake("RUNTIME_INIT_FAILED");


        return;
    }


    Serial.println(
        "ESP-NOW startup successful."
    );


    nextEventTime =
        millis() +
        FIRST_EVENT_DELAY_MS;

    protocolReady = true;

    // One-shot boot policy, after retained recovery, Motion and runtime setup.
    // GPIO4 is CC1101 GDO0: its participation suppresses a return wake, even
    // when GPIO3 also fired. Never re-evaluate this from loop() or rearm on failure.
    if (!buttonWakeIntentHeld && bootInfo.wokeFromGpio(MOTION_INT1_PIN) && !bootInfo.wokeFromGpio(4))
    {
        Serial.println("MOTION PEER WAKE | one-shot request");
        requestPeerWake();
    }

    // One bootstrap attempt per runtime startup, even before peer ONLINE.
    // UNKNOWN suppresses heartbeats; this bounded check supplies its own probes.
    if (!buttonHeartbeatPending && proximityClassification == ProximityClassification::UNKNOWN)
        startProximityCheck(uint32_t(millis())); // Existing ACTIVE/overlap guards; no loop-based restart.
    lastMeaningfulActivity = uint32_t(millis()); // Fresh runtime, including a completely stationary boot.
    automaticSleepArmed = true;
}


// ==========================================================
// Arduino loop
// ==========================================================

void loop()
{
    led.update(uint32_t(millis()), backgroundHeartbeatAllowed()); // Current distance and power state.
    checkProximityEligibility(); // Observe boundaries even if power changes back to ACTIVE below.
    // Clear stale tracking even if a power transition returns to ACTIVE below.
    if (PowerManager::localState() != PowerManager::LocalState::ACTIVE)
        resetMovement();
    if (protocolReady) serviceMotion(); // Real movement wins before admission, controls and physical entry.
    serviceButton(); // Only a debounced press counts as meaningful local activity.
    // Activity/deadlines take effect before any queued control can advance the
    // FSM. Receipt ACKs in the RX batch still run before transport timeouts.
    PowerManager::update(uint32_t(millis()));
    if (!protocolReady)
    {
        failButtonWake("RUNTIME_NOT_READY");
        resetMovement();
        cancelProximityCheck("RUNTIME_NOT_READY");
        delay(10);
        return;
    }

    // Handle queued ACKs before timeouts. Bound each batch so continuous
    // incoming traffic cannot prevent retries or outgoing events.
    for (UBaseType_t i = 0; i < RX_QUEUE_LENGTH; ++i)
    {
        Protocol::Message message{};
        if (xQueueReceive(receiveQueue, &message, 0) != pdPASS)
        {
            break;
        }

        handleReceivedData(
            reinterpret_cast<const uint8_t*>(&message),
            sizeof(message)
        );
    }

    // ------------------------------------------------------
    // Check ACK timeout / retry state.
    // ------------------------------------------------------

    CC1101WakeRecovery::serviceAwake(PEER_DEVICE, receiveCc1101); // RX ACK before the 300 ms deadline.
    serviceDeferredWakeEvents(); // Also drain failed/overflowed episodes before admission or new button work.
    discardObsoleteControls();
    handleAckTimeout();
    serviceAutomaticSleep();
    sendNextControl();


    // Physical execution is separate from semantic agreement.
    serviceSleepExecution();
    serviceButtonHeartbeat(); // User intent has priority at the same transport boundary.
    serviceAutomaticTransportSelection(); // Apply policy before the next periodic EVENT snapshots its transport.


    // ------------------------------------------------------
    // Only create a new EVENT if there is no previous EVENT
    // still waiting for its ACK.
    // ------------------------------------------------------

    if (
        !buttonHeartbeatPending &&
        proximityClassification != ProximityClassification::UNKNOWN &&
        PowerManager::automaticHeartbeatAllowed() &&
        controlCount == 0 &&
        !waitingForAck &&
        (selectedTransport != Transport::CC1101 || !CC1101WakeRecovery::awakeBusy()) &&
        (long)(
            millis() -
            nextEventTime
        ) >= 0
    )
    {
        startHeartbeatEvent();
    }

    // Once a background EVENT starts FAR output, service its shared visual clock
    // even between packets. ACK/retry completion and an offset peer must not
    // stretch the six-second rhythm. User animation and power guards still win.
    if (proximityClassification == ProximityClassification::FAR && haveFarBackgroundHeartbeat)
        requestBackgroundHeartbeat();

    serviceProximityProbe(uint32_t(millis()));

    // Best-effort diagnostics only, after all normal protocol/sleep/motion work.
    // Never drain indefinitely or include this backlog in sleep-entry guards.
    for (unsigned i = 0; i < 2; ++i)
    {
        ESPNowRadio::RssiObservation observation{};
        if (!ESPNowRadio::takeRssiObservation(observation)) break;
        Serial.printf("ESPNOW RSSI | sender=%s | id=%u | type=%u | ackFor=%u | rssi=%d dBm | age=%lu ms\n",
                      deviceName(observation.message.sender), observation.message.messageId,
                      static_cast<unsigned>(observation.message.type), observation.message.ackForMessageId,
                      static_cast<int>(observation.rssi),
                      static_cast<unsigned long>(uint32_t(millis() - observation.receivedAt)));
        sampleProximity(observation, uint32_t(millis()));
    }

    serviceDisplayStatus();

    delay(
        10
    );
}
