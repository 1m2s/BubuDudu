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

void saveRtcHistory();
bool restoreRtcHistory();


namespace
{
    constexpr UBaseType_t RX_QUEUE_LENGTH = 8;
    QueueHandle_t receiveQueue = nullptr;
    bool protocolReady = false;
    CC1101WakeRecovery::BootInfo bootInfo;
    CC1101WakeRecovery::Report wakeReport;
    bool rtcRestored = false;


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


    // ======================================================
    // Reliability settings
    // ======================================================

    constexpr unsigned long FIRST_EVENT_DELAY_MS =
        1000;

    constexpr unsigned long EVENT_INTERVAL_MS =
        4000;

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

    uint16_t nextMessageId =
        1;


    Protocol::Message pendingMessage{};


    bool waitingForAck =
        false;


    uint8_t retryCount =
        0;


    unsigned long ackWaitStart =
        0;


    unsigned long nextEventTime =
        0;

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
    bool pauseAutomaticHeartbeats = false;
    bool delayControlsForTest = false;


    // ======================================================
    // Duplicate detection
    // ======================================================

    bool haveLastPeerEvent =
        false;


    uint16_t lastPeerEventId =
        0;


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
        const Protocol::Message& message
    )
    {
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

    void sendAck(
        uint16_t receivedMessageId
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
                ack
            );


        if (accepted)
        {
            Serial.printf(
                "TX ACK | id=%u | ackFor=%u\n",
                ack.messageId,
                ack.ackForMessageId
            );
        }
        else
        {
            Serial.printf(
                "TX ACK REQUEST FAILED | ackFor=%u\n",
                receivedMessageId
            );
        }
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
                pendingMessage
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
                " | waiting for ACK\n",
                retryCount,
                MAX_RETRIES,
                pendingMessage.messageId
            );
        }
        else
        {
            Serial.printf(
                "TX EVENT"
                " | sender=%s"
                " | id=%u"
                " | waiting for ACK\n",
                deviceName(
                    LOCAL_DEVICE
                ),
                pendingMessage.messageId
            );
        }


        if (!accepted)
        {
            Serial.println(
                "WARNING: ESP-NOW TX request failed"
            );
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
        controlQueue[controlCount++] = {message, uint32_t(millis()) + (delayControlsForTest ? 1000U : 0U)};
        return true;
    }


    void sendNextControl()
    {
        if (waitingForAck || controlCount == 0 ||
            uint32_t(uint32_t(millis()) - controlQueue[0].notBefore) >= 0x80000000UL)
            return;
        pendingMessage = controlQueue[0].message;
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

    void startHeartbeatEvent()
    {
        pendingMessage.version =
            Protocol::VERSION;


        pendingMessage.type =
            Protocol::MessageType::Event;


        pendingMessage.messageId =
            nextMessageId++;


        pendingMessage.sender =
            LOCAL_DEVICE;


        pendingMessage.event =
            Protocol::EventType::Heartbeat;


        pendingMessage.ackForMessageId =
            0;


        retryCount =
            0;


        waitingForAck =
            true;


        transmitPendingMessage(
            false
        );
    }


    // ======================================================
    // Handle received ACK
    // ======================================================

    void handleAck(
        const Protocol::Message& message
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
            waitingForAck &&
            message.ackForMessageId ==
                pendingMessage.messageId
        )
        {
            Serial.printf(
                "ACK MATCHED"
                " | message=%u\n",
                pendingMessage.messageId
            );


            waitingForAck =
                false;


            retryCount =
                0;


            nextEventTime =
                millis() +
                EVENT_INTERVAL_MS;


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
        bool receiptOverEspNow = true
    )
    {
        bool duplicate =
            haveLastPeerEvent &&
            message.messageId ==
                lastPeerEventId;


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
            if (receiptOverEspNow) sendAck(message.messageId);


            return false;
        }


        // --------------------------------------------------
        // New EVENT
        // --------------------------------------------------

        haveLastPeerEvent =
            true;


        lastPeerEventId =
            message.messageId;

        // Only a new application EVENT is meaningful activity. Re-ACKing an
        // old duplicate must not execute the event/cancellation a second time.
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


        /*
         * Eventually this is where the heartbeat event
         * will be forwarded to the application / LED side.
         *
         * For now the serial print represents processing.
         */


        // --------------------------------------------------
        // TEMPORARY TEST:
        //
        // Deliberately lose the first ACK so that we can
        // physically prove retry + duplicate handling.
        // --------------------------------------------------

        if (
            receiptOverEspNow && DROP_FIRST_ACK_FOR_TEST &&
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

        if (receiptOverEspNow) sendAck(message.messageId);
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


    void handleReceivedData(
        const uint8_t* data,
        size_t length
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


        if (Protocol::isSleepControl(message.type))
        {
            if (message.event != Protocol::EventType::None ||
                (message.type == Protocol::MessageType::SleepRequest && message.ackForMessageId != message.messageId))
            {
                Serial.println("POWER: malformed control rejected");
                return;
            }
            // Receipt ACK is independent of semantic acceptance. Duplicates
            // and stale controls are acknowledged, then evaluated by the FSM.
            sendAck(message.messageId);
            PowerManager::handleControl(message, millis());
            return;
        }

        // Dispatch existing EVENT / receipt ACK without changing wire behavior.

        switch (
            message.type
        )
        {
            case Protocol::MessageType::Event:

                handleEvent(
                    message
                );
                PowerManager::notePeerSeen();

                break;


            case Protocol::MessageType::Ack:
            {
                const bool heartbeatAcknowledged = waitingForAck &&
                    pendingMessage.type == Protocol::MessageType::Event &&
                    message.ackForMessageId == pendingMessage.messageId;
                handleAck(
                    message
                );
                // A late receipt for COMMIT/CANCEL does not resolve uncertainty
                // about the peer's semantic sleep state.
                if (heartbeatAcknowledged)
                    PowerManager::notePeerSeen();

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
            PowerManager::controlFailed(pendingMessage.type, pendingMessage.ackForMessageId, millis());
        else
            PowerManager::notePeerUnreachable();


        nextEventTime =
            millis() +
            EVENT_INTERVAL_MS;
    }


    void servicePowerTest()
    {
        const uint32_t now = millis();
        PowerManager::update(now);
        // Bound serial work too: one character per loop, no waiting for input.
        if (Serial.available() == 0)
            return;

        switch (Serial.read())
        {
            case 'p':
                if (bootInfo.deep) CC1101WakeRecovery::printReport(bootInfo, rtcRestored, wakeReport);
                break;
            case 'x':
                if (!protocolReady || waitingForAck || controlCount != 0 ||
                    uxQueueMessagesWaiting(receiveQueue) != 0 ||
                    !PowerManager::automaticHeartbeatAllowed() || PowerManager::transaction().active)
                    Serial.println("BENCH DEEP SLEEP | REFUSED | require ACTIVE/IDLE and drained transport/RX queue");
                else
                    CC1101WakeRecovery::benchDeepSleep(saveRtcHistory);
                break;
            case 'w':
            {
                if (!protocolReady || waitingForAck || controlCount != 0 ||
                    uxQueueMessagesWaiting(receiveQueue) != 0 || PowerManager::transaction().active ||
                    !PowerManager::automaticHeartbeatAllowed())
                {
                    Serial.println("CC1101 WAKE TX | REFUSED | require ACTIVE/IDLE and drained transport/RX queue");
                    break;
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
                break;
            }
            case 'i': PowerManager::forceIdle(now); break;
            case 's':
                if (!protocolReady || waitingForAck || controlCount != 0)
                    Serial.println("POWER: start refused; wait for transport to drain (p shows pending TX)");
                else
                    PowerManager::requestSleep(nextMessageId++, now);
                break;
            case 'a': PowerManager::injectActivity(now); break;
            case 'h':
                pauseAutomaticHeartbeats = !pauseAutomaticHeartbeats;
                Serial.printf("BENCH: automatic heartbeats %s; pending TX/ACKs still run\n",
                              pauseAutomaticHeartbeats ? "PAUSED" : "ENABLED");
                break;
            case 'd':
                delayControlsForTest = !delayControlsForTest;
                Serial.printf("BENCH: initial control delay=%u ms; ACKs/retries/deadlines unchanged\n",
                              delayControlsForTest ? 1000U : 0U);
                break;
            case '?':
                Serial.println("Power tests: p=status i=IDLE s=handshake a=activity/cancel "
                               "h=toggle auto heartbeats d=toggle 1s control delay x=BENCH deep sleep "
                               "w=BENCH CC1101 wake EVENT; handshake stays awake");
                break;
            default: return; // Includes serial line endings.
        }
        PowerManager::printStatus(now);
        Serial.printf("TRANSPORT: pending=%d id=%u queued=%u auto_heartbeats=%s control_delay_ms=%u\n",
                      waitingForAck, waitingForAck ? pendingMessage.messageId : 0,
                      static_cast<unsigned>(controlCount), pauseAutomaticHeartbeats ? "PAUSED" : "ENABLED",
                      delayControlsForTest ? 1000U : 0U);
    }
}


// Only manual bench entry calls save; only real deep-wake startup calls restore.
// Save belongs at the future final sleep boundary, after all packet IDs have
// been allocated. Do not save at today's arm check and keep using that snapshot
// while the CPU continues to send packets.
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
    return {Protocol::VERSION, Protocol::MessageType::Ack, nextMessageId++,
            LOCAL_DEVICE, Protocol::EventType::None, event.messageId};
}

void setup()
{
    bootInfo = CC1101WakeRecovery::captureBoot(); // EARLIEST: before Serial/SPI.
    Serial.begin(115200);
    PowerManager::begin(LOCAL_DEVICE, queueSleepControl);
    if (bootInfo.deep)
    {
        // No USB delay, normal CC1101 begin, reset or configuration here.
        rtcRestored = restoreRtcHistory();
        if (!rtcRestored) RtcState::invalidate();
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
    Serial.println("Sleep handshake bench: ? for commands. Handshake keeps CPU awake; x is BENCH-only deep sleep.");
    PowerManager::printStatus(millis());

    receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
    if (receiveQueue == nullptr)
    {
        Serial.println("ESP-NOW RECEIVE QUEUE CREATION FAILED");
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


        return;
    }


    Serial.println(
        "ESP-NOW startup successful."
    );


    nextEventTime =
        millis() +
        FIRST_EVENT_DELAY_MS;

    protocolReady = true;
}


// ==========================================================
// Arduino loop
// ==========================================================

void loop()
{
    // Activity/deadlines take effect before any queued control can advance the
    // FSM. Receipt ACKs in the RX batch still run before transport timeouts.
    servicePowerTest();
    if (!protocolReady)
    {
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

    discardObsoleteControls();
    handleAckTimeout();
    sendNextControl();

    // Semantic completion can precede the final SLEEP_ACK's packet receipt.
    // Wait for all reliable work to finish (ACK or bounded retry exhaustion).
    // This checkpoint only reports readiness; CPU and radio remain awake.
    if (!waitingForAck && controlCount == 0)
    {
        PowerManager::SleepDecision decision{};
        if (PowerManager::takeSleepDecision(decision))
        {
            Serial.printf("SLEEP EXECUTION READY | sleepId=%u | role=%s\n",
                          decision.sleepId, PowerManager::toString(decision.role));
            const auto arm = CC1101SleepArm::prepareForSleep();
            Serial.printf("CC1101 SLEEP ARM | %s | reason=%s | PART=0x%02X VERSION=0x%02X "
                          "IOCFG0=0x%02X MARCSTATE=0x%02X RXBYTES=0x%02X GDO0=%d | CPU stays awake\n",
                          arm.result == CC1101SleepArm::Result::Ready ? "READY" : "FAILED",
                          CC1101SleepArm::toString(arm.result), arm.part, arm.version,
                          arm.iocfg0, arm.marc, arm.rxBytes, arm.gdo);
        }
    }


    // ------------------------------------------------------
    // Only create a new EVENT if there is no previous EVENT
    // still waiting for its ACK.
    // ------------------------------------------------------

    if (
        !pauseAutomaticHeartbeats &&
        PowerManager::automaticHeartbeatAllowed() &&
        controlCount == 0 &&
        !waitingForAck &&
        (long)(
            millis() -
            nextEventTime
        ) >= 0
    )
    {
        startHeartbeatEvent();
    }

    delay(
        10
    );
}
