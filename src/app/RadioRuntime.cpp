#include "RadioRuntime.h"
#include "ButtonRuntime.h"
#include "MotionRuntime.h"
#include "Presentation.h"
#include "SleepRuntime.h"
#include "PowerManager.h"
#include "RtcState.h"
#include "CC1101WakeRecovery.h"
#include "ESPNowRadio.h"
#include <Arduino.h>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace RadioRuntime
{
    using namespace AppIdentity;
    using MotionRuntime::ProximityClassification;
    using MotionRuntime::ProximityUpdateState;

    namespace
    {
        constexpr UBaseType_t RX_QUEUE_LENGTH = 8;
        QueueHandle_t receiveQueue = nullptr;
        bool protocolReady = false;
        uint16_t nextMessageId = 1;
        Transport selectedTransport = Transport::ESP_NOW;
        Transport pendingTransport = Transport::ESP_NOW;
        bool automaticSelectionPending = false;
        bool espNowFallbackPending = false;
        constexpr unsigned long FIRST_EVENT_DELAY_MS = 1000;
        constexpr unsigned long ACK_TIMEOUT_MS = 300;
        constexpr uint8_t MAX_RETRIES = 2;
        // Optional bench fault injection; disabled in both product builds.
        constexpr bool DROP_FIRST_ACK_FOR_TEST = false;
        bool testAckAlreadyDropped = false;
        Protocol::Message pendingMessage{};
        bool waitingForAck = false;
        uint8_t retryCount = 0;
        unsigned long ackWaitStart = 0;
        unsigned long nextEventTime = 0;
        // Bounded loop-owned outbox; one packet uses the 300 ms / two-retry machinery.
        struct QueuedControl
        {
            Protocol::Message message;
            uint32_t notBefore;
        };
        constexpr size_t CONTROL_QUEUE_LENGTH = 4;
        QueuedControl controlQueue[CONTROL_QUEUE_LENGTH];
        size_t controlCount = 0;
        bool haveLastPeerEvent = false;
        uint16_t lastPeerEventId = 0;
        // Awake user dedup survives interleaved background EVENTs. No visual queue.
        uint16_t recentUserEventIds[RX_QUEUE_LENGTH]{};
        uint32_t recentUserEventTimes[RX_QUEUE_LENGTH]{};
        constexpr uint32_t USER_EVENT_HISTORY_MS = 10000; // Covers retries, not allocator wrap.
        UBaseType_t recentUserEventCount = 0, nextUserEventSlot = 0;
    } // namespace

    void startKnownHeartbeatCadence(uint32_t now);
    unsigned long automaticHeartbeatIntervalMs()
    {
        switch (MotionRuntime::classification())
        {
        case ProximityClassification::CLOSE:
            return EVENT_INTERVAL_MS;
        case ProximityClassification::FAR:
            return FAR_EVENT_INTERVAL_MS;
        default:
            return 0; // UNKNOWN has no automatic heartbeat cadence.
        }
    }

    const char *transportName(Transport transport)
    {
        return transport == Transport::ESP_NOW ? "ESP-NOW" : "CC1101";
    }

    void startKnownHeartbeatCadence(uint32_t now)
    {
        // A pending transaction keeps its clock and schedules normally on completion.
        if (!waitingForAck) nextEventTime = now + automaticHeartbeatIntervalMs();
    }

    bool sendProtocolMessage(const Protocol::Message &message, Transport transport = Transport::ESP_NOW)
    {
        if (transport == Transport::CC1101)
        {
            const auto result = CC1101WakeRecovery::submitAwake(message);
            if (result != CC1101WakeRecovery::SubmitResult::Accepted)
                Serial.printf("CC1101 APP TX | %s | id=%u\n",
                              result == CC1101WakeRecovery::SubmitResult::Busy ? "BUSY" : "FAILED",
                              message.messageId);
            return result == CC1101WakeRecovery::SubmitResult::Accepted;
        }
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&message);

        return ESPNowRadio::send(bytes, sizeof(message));
    }

    bool sendAck(uint16_t receivedMessageId, Transport transport = Transport::ESP_NOW)
    {
        Protocol::Message ack{};

        ack.version = Protocol::VERSION;

        ack.type = Protocol::MessageType::Ack;

        ack.messageId = nextMessageId++;

        ack.sender = LOCAL_DEVICE;

        ack.event = Protocol::EventType::None;

        ack.ackForMessageId = receivedMessageId;

        bool accepted = sendProtocolMessage(ack, transport);

        if (accepted)
        {
            Serial.printf("TX ACK | id=%u | ackFor=%u | via=%s\n", ack.messageId, ack.ackForMessageId,
                          transportName(transport));
        }
        else
        {
            Serial.printf("TX ACK REQUEST FAILED | ackFor=%u\n", receivedMessageId);
        }
        return accepted;
    }

    void transmitPendingMessage(bool retry)
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
        bool accepted = sendProtocolMessage(pendingMessage, pendingTransport);

        ackWaitStart = millis();

        if (Protocol::isSleepControl(pendingMessage.type))
        {
            Serial.printf("POWER TX %s | sleepId=%u messageId=%u retry=%u\n",
                          Protocol::controlName(pendingMessage.type), pendingMessage.ackForMessageId,
                          pendingMessage.messageId, retryCount);
        }
        else if (retry)
        {
            Serial.printf("RETRY %u/%u"
                          " | id=%u"
                          " | via=%s | waiting for ACK\n",
                          retryCount, MAX_RETRIES, pendingMessage.messageId, transportName(pendingTransport));
        }
        else
        {
            Serial.printf("TX EVENT"
                          " | sender=%s"
                          " | id=%u"
                          " | via=%s | waiting for ACK\n",
                          deviceName(LOCAL_DEVICE), pendingMessage.messageId,
                          transportName(pendingTransport));
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
            const auto &message = controlQueue[i].message;
            if (PowerManager::controlStillNeeded(message.type, message.ackForMessageId))
                controlQueue[kept++] = controlQueue[i];
        }
        controlCount = kept;
    }

    bool queueSleepControl(Protocol::MessageType type, uint16_t sleepId)
    {
        if (!protocolReady) return false;
        discardObsoleteControls();
        if (type != Protocol::MessageType::SleepCancel)
        {
            // Repeated semantic responses reuse the outstanding packet/retry
            // budget; duplicates cannot keep refreshing its delivery timer.
            if (waitingForAck && pendingMessage.type == type && pendingMessage.ackForMessageId == sleepId)
                return true;
            for (size_t i = 0; i < controlCount; ++i)
                if (controlQueue[i].message.type == type &&
                    controlQueue[i].message.ackForMessageId == sleepId)
                    return true;
            if (controlCount == CONTROL_QUEUE_LENGTH) return false;
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

    void startHeartbeatEvent(Transport transport, Protocol::EventType event)
    {
        PowerManager::applicationTxStarted(); // Retries never pass this boundary.
        pendingTransport = transport;
        pendingMessage.version = Protocol::VERSION;

        pendingMessage.type = Protocol::MessageType::Event;

        pendingMessage.messageId = nextMessageId++;

        pendingMessage.sender = LOCAL_DEVICE;

        pendingMessage.event = event;

        pendingMessage.ackForMessageId = 0;

        retryCount = 0;

        waitingForAck = true;

        // Button intent animates only its receiver.
        // Retries only call transmitPendingMessage(), so cannot replay either request.
        if (event == Protocol::EventType::Heartbeat) Presentation::requestBackgroundHeartbeat();

        transmitPendingMessage(false);
    }

    void handleAck(const Protocol::Message &message, Transport transport)
    {
        Serial.printf("RX ACK"
                      " | from=%s"
                      " | ackFor=%u"
                      " | ackMessageId=%u\n",
                      deviceName(message.sender), message.ackForMessageId, message.messageId);

        // Accept only the pending packet ID on its original transport.
        if (waitingForAck && transport == pendingTransport &&
            message.ackForMessageId == pendingMessage.messageId)
        {
            Serial.printf("ACK MATCHED"
                          " | message=%u | via=%s\n",
                          pendingMessage.messageId, transportName(transport));

            if (pendingMessage.type == Protocol::MessageType::Event)
                PowerManager::applicationTxAcknowledged();

            waitingForAck = false;

            retryCount = 0;

            nextEventTime = millis() + automaticHeartbeatIntervalMs();

            return;
        }

        Serial.println("ACK IGNORED:"
                       " does not match pending message");
    }

    bool handleEvent(const Protocol::Message &message, bool sendReceipt = true,
                     Transport transport = Transport::ESP_NOW)
    {
        bool duplicate = haveLastPeerEvent && message.messageId == lastPeerEventId;

        if (message.event == Protocol::EventType::UserHeartbeat)
            for (UBaseType_t i = 0; i < recentUserEventCount; ++i)
                duplicate = duplicate ||
                            (recentUserEventIds[i] == message.messageId &&
                             uint32_t(uint32_t(millis()) - recentUserEventTimes[i]) < USER_EVENT_HISTORY_MS);

        if (duplicate)
        {
            Serial.printf("RX DUPLICATE"
                          " | sender=%s"
                          " | id=%u"
                          " | event ignored"
                          " | ACK again\n",
                          deviceName(message.sender), message.messageId);

            // Re-ACK retries after a lost receipt without delivering the event again.
            if (sendReceipt) sendAck(message.messageId, transport);

            return false;
        }

        haveLastPeerEvent = true;

        lastPeerEventId = message.messageId;

        if (message.event == Protocol::EventType::UserHeartbeat)
        {
            recentUserEventIds[nextUserEventSlot] = message.messageId;
            recentUserEventTimes[nextUserEventSlot] = uint32_t(millis());
            nextUserEventSlot = (nextUserEventSlot + 1) % RX_QUEUE_LENGTH;
            if (recentUserEventCount < RX_QUEUE_LENGTH) ++recentUserEventCount;
        }

        // Periodic Heartbeat is background traffic, not local user activity.
        PowerManager::applicationEvent(millis());

        Serial.printf("RX NEW EVENT"
                      " | sender=%s"
                      " | id=%u"
                      " | event=%u\n",
                      deviceName(message.sender), message.messageId, static_cast<uint8_t>(message.event));

        // Runtime requests happen only after validation and duplicate rejection.
        // Only explicit user intent gets priority, regardless of local distance.
        // Retained delivery records its visual obligation in handleWakeEvent().
        if (sendReceipt && message.type == Protocol::MessageType::Event &&
            message.event == Protocol::EventType::UserHeartbeat)
        {
            Presentation::requestUserHeartbeat();
            Serial.printf("PARTNER LED | USER HEARTBEAT | id=%u | via=%s\n", message.messageId,
                          transportName(transport));
        }
        else if (sendReceipt && message.event == Protocol::EventType::Heartbeat)
            Presentation::requestBackgroundHeartbeat();

        // Fault injection exercises retry/dedup after a lost receipt.

        if (sendReceipt && transport == Transport::ESP_NOW && DROP_FIRST_ACK_FOR_TEST &&
            !testAckAlreadyDropped)
        {
            testAckAlreadyDropped = true;

            Serial.printf("TEST: intentionally dropping ACK"
                          " | event=%u\n",
                          message.messageId);

            return true;
        }

        if (sendReceipt) sendAck(message.messageId, transport);
        return true;
    }

    // Called after Wi-Fi RX logging: queue bytes; loop() owns protocol/policy state.
    void queueReceivedData(const uint8_t *data, size_t length)
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
                                  MotionRuntime::classification() == ProximityClassification::UNKNOWN;
        if (PowerManager::peerState() == PowerManager::PeerState::ONLINE && (recoveringPeer || firstContact))
            MotionRuntime::startProximityCheck(
                uint32_t(millis())); // Existing guards and loop-owned probes remain authoritative.
    }

    void handleReceivedData(const uint8_t *data, size_t length, Transport transport = Transport::ESP_NOW)
    {

        if (length != sizeof(Protocol::Message))
        {
            Serial.printf("RX INVALID SIZE | bytes=%u\n", static_cast<unsigned int>(length));

            return;
        }

        Protocol::Message message{};

        memcpy(&message, data, sizeof(message));

        if (message.version != Protocol::VERSION)
        {
            Serial.printf("RX INVALID VERSION | version=%u\n", message.version);

            return;
        }

        if (message.sender != PEER_DEVICE)
        {
            Serial.printf("RX INVALID SENDER | sender=%u\n", static_cast<uint8_t>(message.sender));

            return;
        }

        if (message.type == Protocol::MessageType::ProximityProbe ||
            message.type == Protocol::MessageType::ProximityProbeReply)
        {
            // ESP-NOW-only best effort; never enter application or sleep reliability.
            if (transport != Transport::ESP_NOW || message.event != Protocol::EventType::None) return;
            // Replies are correlated solely by the RSSI observation's complete message.
            if (message.type == Protocol::MessageType::ProximityProbeReply) return;
            if (message.ackForMessageId != 0) return;
            if (ButtonRuntime::pending()) return; // Measurement replies yield to queued user intent.
            const auto state = PowerManager::localState();
            if (state != PowerManager::LocalState::ACTIVE) return;
            const Protocol::Message reply{
                Protocol::VERSION,         Protocol::MessageType::ProximityProbeReply,
                nextMessageId++,           LOCAL_DEVICE,
                Protocol::EventType::None, message.messageId};
            (void)ESPNowRadio::send(reinterpret_cast<const uint8_t *>(&reply), sizeof(reply));
            return;
        }

        if (Protocol::isSleepControl(message.type))
        {
            if (transport != Transport::ESP_NOW) return;
            if (message.event != Protocol::EventType::None ||
                (message.type == Protocol::MessageType::SleepRequest &&
                 message.ackForMessageId != message.messageId))
            {
                Serial.println("POWER: malformed control rejected");
                return;
            }
            // Sample fresh admission BEFORE this packet creates its own ACK TX.
            // An existing transaction retains duplicate/collision/phase handling.
            const bool freshRequest =
                message.type == Protocol::MessageType::SleepRequest && !PowerManager::transaction().active;
            const bool admitFreshRequest =
                freshRequest && SleepRuntime::productSleepEligible(
                                    uint32_t(millis()), SleepRuntime::AUTOMATIC_SLEEP_INACTIVITY_MS -
                                                            SleepRuntime::AUTOMATIC_SLEEP_PEER_GRACE_MS);
            // Receipt ACK is independent of semantic acceptance. Duplicates
            // and stale controls are acknowledged, then evaluated by the FSM.
            const bool receiptAccepted = sendAck(message.messageId);
            PowerManager::handleControl(message, millis(), admitFreshRequest);
            if (freshRequest && PowerManager::transaction().active)
                SleepRuntime::consumeAutomaticOpportunity(); // Participation also consumes our automatic
                                                             // initiation opportunity.
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

        switch (message.type)
        {
        case Protocol::MessageType::Event:

            if (!Protocol::isHeartbeat(message.event) || message.ackForMessageId != 0) break;
            handleEvent(message, true, transport);
            PowerManager::notePeerSeen();
            startPeerAvailabilityProximityCheck(previousPeer);

            break;

        case Protocol::MessageType::Ack:
        {
            if (message.event != Protocol::EventType::None) break;
            const bool heartbeatAcknowledged = waitingForAck && transport == pendingTransport &&
                                               pendingMessage.type == Protocol::MessageType::Event &&
                                               message.ackForMessageId == pendingMessage.messageId;
            handleAck(message, transport);
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

            Serial.printf("RX UNKNOWN TYPE | type=%u\n", static_cast<uint8_t>(message.type));

            break;
        }
    }

    void handleAckTimeout()
    {
        if (!waitingForAck)
        {
            return;
        }

        if (millis() - ackWaitStart < ACK_TIMEOUT_MS)
        {
            return;
        }

        Serial.printf("ACK TIMEOUT"
                      " | message=%u\n",
                      pendingMessage.messageId);

        if (retryCount < MAX_RETRIES)
        {
            retryCount++;

            // Retries preserve the original message ID and payload.
            transmitPendingMessage(true);

            return;
        }

        Serial.printf("GIVE UP"
                      " | message=%u"
                      " | retries=%u exhausted\n",
                      pendingMessage.messageId, MAX_RETRIES);

        waitingForAck = false;

        retryCount = 0;

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

        nextEventTime = millis() + automaticHeartbeatIntervalMs();
    }

    const char *transportBlockedReason(bool allowStoppedCc1101)
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

    // Keep admission and final entry strict even when the awake CC1101 runtime stopped.
    const char *sleepTransportBlockedReason()
    {
        return transportBlockedReason(false);
    }

    // One EVENT per bounded wake episode; the transmitter owns SPI until it returns.
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
        const Protocol::Message event{Protocol::VERSION, Protocol::MessageType::Event,   nextMessageId++,
                                      LOCAL_DEVICE,      Protocol::EventType::Heartbeat, 0};
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

    void receiveCc1101(const Protocol::Message &message)
    {
        handleReceivedData(reinterpret_cast<const uint8_t *>(&message), sizeof(message), Transport::CC1101);
    }

    void serviceDeferredWakeEvents()
    {
        if (!protocolReady || (CC1101WakeRecovery::awakeBusy() && !CC1101WakeRecovery::awakeStopped()))
            return;
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
        return sleepTransportBlockedReason() || SleepRuntime::draining() ||
               !PowerManager::automaticHeartbeatAllowed() ||
               (PowerManager::sleepRecoveryCheckPending() &&
                PowerManager::peerState() != PowerManager::PeerState::OFFLINE) ||
               MotionRuntime::checkState() == ProximityUpdateState::CHECKING;
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
        if (MotionRuntime::classification() == ProximityClassification::UNKNOWN)
        {
            automaticSelectionPending = false;
            return;
        }
        const Transport target = MotionRuntime::classification() == ProximityClassification::CLOSE
                                     ? Transport::ESP_NOW
                                     : Transport::CC1101;
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
                      MotionRuntime::classification() == ProximityClassification::FAR ? "FAR" : "CLOSE");
    }

    // Final sleep boundary, after packet allocation; restore only on a real deep wake.
    void saveRtcHistory()
    {
        RtcState::save({nextMessageId, haveLastPeerEvent, lastPeerEventId, PowerManager::exportHistory()});
    }

    // PowerManager must begin first; restore before enabling either runtime transport.
    bool restoreRtcHistory()
    {
        if (protocolReady || waitingForAck || controlCount != 0) return false;
        RtcState::History history{};
        if (!RtcState::load(history) || !PowerManager::restoreHistory(history.sleep)) return false;
        nextMessageId = history.nextMessageId;
        haveLastPeerEvent = history.haveLastPeerEvent;
        lastPeerEventId = history.lastPeerEventId;
        RtcState::invalidate(); // Consume only after successful application.
        return true;
    }

    // Retained wake callback: dedup/deliver before Wi-Fi startup, then return a CC1101 receipt.
    Protocol::Message handleWakeEvent(const Protocol::Message &event, bool &processed)
    {
        processed = handleEvent(event, false);
        if (processed && event.type == Protocol::MessageType::Event &&
            event.event == Protocol::EventType::UserHeartbeat)
        {
            Presentation::deferUserHeartbeat(event.messageId);
            Serial.printf("PARTNER LED | DEFERRED USER HEARTBEAT | id=%u\n", event.messageId);
        }
        return {Protocol::VERSION, Protocol::MessageType::Ack, nextMessageId++,
                LOCAL_DEVICE,      Protocol::EventType::None,  event.messageId};
    }

    bool ready()
    {
        return protocolReady;
    }
    uint16_t allocateMessageId()
    {
        return nextMessageId++;
    }
    Transport transport()
    {
        return selectedTransport;
    }
    bool fallbackPending()
    {
        return espNowFallbackPending;
    }
    void resetBootState()
    {
        recentUserEventCount = nextUserEventSlot = 0; // Retained dedup stays authoritative.
        automaticSelectionPending = espNowFallbackPending = false;
        selectedTransport = pendingTransport = Transport::ESP_NOW; // RAM-only.
    }
    void classificationCompleted(uint32_t now, bool first)
    {
        if (first) startKnownHeartbeatCadence(now);
        automaticSelectionPending = true; // Every median is fresh policy evidence.
        espNowFallbackPending = false;    // Supersedes an older delivery failure.
    }
    void startHeartbeatEvent()
    {
        startHeartbeatEvent(selectedTransport);
    }
    bool begin()
    {
        receiveQueue = xQueueCreate(RX_QUEUE_LENGTH, sizeof(Protocol::Message));
        if (receiveQueue == nullptr)
        {
            Serial.println("ESP-NOW RECEIVE QUEUE CREATION FAILED");
            return false;
        }

        if (!ESPNowRadio::begin(queueReceivedData))
        {
            Serial.println("ESP-NOW STARTUP FAILED");

            return false;
        }

        Serial.println("ESP-NOW startup successful.");

        nextEventTime = millis() + FIRST_EVENT_DELAY_MS;

        protocolReady = true;

        return true;
    }
    void drainReceiveQueue()
    {
        // Handle queued ACKs before timeouts. Bound each batch so continuous
        // incoming traffic cannot prevent retries or outgoing events.
        for (UBaseType_t i = 0; i < RX_QUEUE_LENGTH; ++i)
        {
            Protocol::Message message{};
            if (xQueueReceive(receiveQueue, &message, 0) != pdPASS)
            {
                break;
            }

            handleReceivedData(reinterpret_cast<const uint8_t *>(&message), sizeof(message));
        }
    }
    void serviceAwake()
    {
        CC1101WakeRecovery::serviceAwake(PEER_DEVICE, receiveCc1101); // RX ACK before timeout.
        serviceDeferredWakeEvents();
    }
    void servicePeriodicHeartbeat()
    {
        if (!ButtonRuntime::pending() &&
            MotionRuntime::classification() != ProximityClassification::UNKNOWN &&
            PowerManager::automaticHeartbeatAllowed() && controlCount == 0 && !waitingForAck &&
            (selectedTransport != Transport::CC1101 || !CC1101WakeRecovery::awakeBusy()) &&
            (long)(millis() - nextEventTime) >= 0)
        {
            startHeartbeatEvent();
        }
    }
} // namespace RadioRuntime
