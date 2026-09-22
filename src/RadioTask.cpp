#include "RadioTask.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "Config.h"
#include "CC1101Radio.h"
#include "Protocol.h"


namespace
{
    // ========================================================
    // Device configuration
    // ========================================================

#ifdef DEVICE_BUBU

    constexpr Protocol::DeviceId LOCAL_DEVICE =
        Protocol::DeviceId::Bubu;

    constexpr Protocol::DeviceId PEER_DEVICE =
        Protocol::DeviceId::Dudu;

    constexpr unsigned long FIRST_EVENT_DELAY_MS = 1000;

#elif defined(DEVICE_DUDU)

    constexpr Protocol::DeviceId LOCAL_DEVICE =
        Protocol::DeviceId::Dudu;

    constexpr Protocol::DeviceId PEER_DEVICE =
        Protocol::DeviceId::Bubu;

    constexpr unsigned long FIRST_EVENT_DELAY_MS = 2500;

#else

#error "Device identity not configured"

#endif


    // ========================================================
    // Reliability settings
    // ========================================================

    constexpr unsigned long EVENT_INTERVAL_MS = 4000;
    constexpr unsigned long ACK_TIMEOUT_MS = 300;
    constexpr unsigned long PEER_OFFLINE_TIMEOUT_MS = 7000;

    constexpr uint8_t MAX_RETRIES = 2;


    // ========================================================
    // Protocol state
    // ========================================================

    uint16_t nextMessageId = 1;

    Protocol::Message pendingMessage;

    bool waitingForAck = false;

    uint8_t retryCount = 0;

    unsigned long ackWaitStart = 0;
    unsigned long nextEventTime = 0;


    // ========================================================
    // Duplicate detection
    // ========================================================

    bool haveLastPeerEvent = false;

    uint16_t lastPeerEventId = 0;


    // ========================================================
    // Peer availability
    // ========================================================

    bool peerOnline = false;

    unsigned long lastPeerSeenTime = 0;


    // ========================================================
    // FreeRTOS state
    // ========================================================

    TaskHandle_t radioTaskHandle = nullptr;

    // Only RadioTask owns this state and performs recovery.
    enum class RadioState { Ready, Recovering, Fault };
    RadioState radioState = RadioState::Ready;
    constexpr uint8_t MAX_RECOVERY_ATTEMPTS = 3;
    constexpr unsigned long RECOVERY_INTERVAL_MS = 250;
    uint8_t recoveryAttempts = 0;
    unsigned long lastRecoveryAttempt = 0;


    bool initializeRadio()
    {
        if (!CC1101Radio::reset())
        {
            Serial.println("RADIO: reset failed");
            return false;
        }
        if (!CC1101Radio::configureForPacketTest())
        {
            Serial.println("RADIO: configuration failed");
            return false;
        }
        if (!CC1101Radio::startReceive())
        {
            Serial.println("RADIO: RX start failed");
            return false;
        }
        return true;
    }


    void requestRecovery(const char* reason)
    {
        // Repeated failures during the same episode must not reset its budget.
        if (radioState != RadioState::Ready)
        {
            return;
        }
        Serial.printf("RADIO RECOVERY REQUIRED: %s\n", reason);
        radioState = RadioState::Recovering;
        recoveryAttempts = 0;
        lastRecoveryAttempt = millis();
    }


    void handleRadioRecovery()
    {
        if (radioState != RadioState::Recovering ||
            millis() - lastRecoveryAttempt < RECOVERY_INTERVAL_MS)
        {
            return;
        }

        ++recoveryAttempts;
        Serial.printf("RADIO RECOVERY: attempt %u/%u\n",
                      recoveryAttempts, MAX_RECOVERY_ATTEMPTS);
        // Reset only the radio hardware. Keep pending IDs and duplicate history.
        if (initializeRadio())
        {
            radioState = RadioState::Ready;
            Serial.println("RADIO RECOVERY SUCCEEDED: RX ready");
        }
        else if (recoveryAttempts >= MAX_RECOVERY_ATTEMPTS)
        {
            radioState = RadioState::Fault;
            Serial.println("RADIO PERSISTENT FAULT: recovery exhausted; "
                           "radio I/O stopped until device restart");
        }
        lastRecoveryAttempt = millis();
    }


    // ========================================================
    // Helpers
    // ========================================================

    const char* deviceName(Protocol::DeviceId device)
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


    void setPeerOnline(bool online)
    {
        if (peerOnline == online)
        {
            return;
        }


        peerOnline = online;


        if (online)
        {
            Serial.printf(
                "PEER ONLINE: %s\n",
                deviceName(PEER_DEVICE)
            );
        }
        else
        {
            Serial.printf(
                "PEER OFFLINE: %s\n",
                deviceName(PEER_DEVICE)
            );
        }
    }


    void notePeerSeen()
    {
        lastPeerSeenTime = millis();

        setPeerOnline(true);
    }


    // ========================================================
    // Send one protocol packet
    // ========================================================

    bool sendProtocolMessage(
        const Protocol::Message& message
    )
    {
        if (radioState != RadioState::Ready)
        {
            // Do not bypass recovery or its attempt limit with a TX attempt.
            return false;
        }

        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(
                &message
            );


        bool transmitted =
            CC1101Radio::sendPacket(
                bytes,
                sizeof(message)
            );


        /*
         * sendPacket() finishes in IDLE.
         *
         * The radio should normally be listening,
         * so immediately return to RX.
         */
        if (!CC1101Radio::startReceive())
        {
            requestRecovery("RX restart after TX failed");
        }

        // TX completion and subsequent RX readiness are independent.
        return transmitted;
    }


    // ========================================================
    // ACK transmission
    // ========================================================

    void sendAck(uint16_t receivedMessageId)
    {
        Protocol::Message ack;

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


        /*
         * Short TX/RX turnaround guard.
         *
         * Unlike delay(), vTaskDelay() puts THIS task
         * to sleep and lets FreeRTOS run another task.
         */
        vTaskDelay(
            pdMS_TO_TICKS(5)
        );


        bool success =
            sendProtocolMessage(ack);


        if (success)
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
                "TX ACK FAILED | ackFor=%u\n",
                receivedMessageId
            );
        }
    }


    // ========================================================
    // EVENT transmission
    // ========================================================

    void transmitPendingEvent(bool retry)
    {
        bool success =
            sendProtocolMessage(
                pendingMessage
            );


        ackWaitStart = millis();


        if (retry)
        {
            Serial.printf(
                "RETRY %u/%u | id=%u | waiting for ACK\n",
                retryCount,
                MAX_RETRIES,
                pendingMessage.messageId
            );
        }
        else
        {
            Serial.printf(
                "TX EVENT | sender=%s | id=%u | waiting for ACK\n",
                deviceName(LOCAL_DEVICE),
                pendingMessage.messageId
            );
        }


        if (!success)
        {
            Serial.println(
                "WARNING: CC1101 TX attempt reported failure"
            );
        }
    }


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

        pendingMessage.ackForMessageId = 0;


        retryCount = 0;

        waitingForAck = true;


        transmitPendingEvent(false);
    }


    // ========================================================
    // ACK handling
    // ========================================================

    void handleAck(
        const Protocol::Message& message
    )
    {
        Serial.printf(
            "RX ACK | from=%s | ackFor=%u | ackMessageId=%u\n",
            deviceName(message.sender),
            message.ackForMessageId,
            message.messageId
        );


        if (
            waitingForAck &&
            message.ackForMessageId ==
                pendingMessage.messageId
        )
        {
            Serial.printf(
                "ACK MATCHED | message=%u\n",
                pendingMessage.messageId
            );


            waitingForAck = false;

            retryCount = 0;


            nextEventTime =
                millis() +
                EVENT_INTERVAL_MS;


            return;
        }


        Serial.println(
            "ACK IGNORED: does not match pending message"
        );
    }


    // ========================================================
    // EVENT handling
    // ========================================================

    void handleEvent(
        const Protocol::Message& message
    )
    {
        bool duplicate =
            haveLastPeerEvent &&
            message.messageId ==
                lastPeerEventId;


        if (duplicate)
        {
            Serial.printf(
                "RX DUPLICATE | sender=%s | id=%u"
                " | event ignored | ACK again\n",
                deviceName(message.sender),
                message.messageId
            );


            /*
             * Do NOT execute the event again.
             *
             * But ACK it again because our previous
             * ACK may have been lost.
             */
            sendAck(
                message.messageId
            );


            return;
        }


        haveLastPeerEvent = true;

        lastPeerEventId =
            message.messageId;


        Serial.printf(
            "RX NEW EVENT | sender=%s | id=%u | event=%u\n",
            deviceName(message.sender),
            message.messageId,
            static_cast<uint8_t>(
                message.event
            )
        );


        /*
         * IMPORTANT:
         *
         * Do not trigger LED code directly from here.
         *
         * Soon RadioTask will put this event into
         * a FreeRTOS queue for the application/LED side.
         */


        sendAck(
            message.messageId
        );
    }


    // ========================================================
    // Receive one packet and dispatch it
    // ========================================================

    void handleIncomingMessage()
    {
        if (radioState != RadioState::Ready)
        {
            return;
        }

        uint8_t buffer[32];

        uint8_t length = 0;


        const CC1101Radio::ReceiveResult result =
            CC1101Radio::receivePacket(
                buffer,
                sizeof(buffer),
                length
            );
        if (!result.rxReady)
        {
            requestRecovery("receive poll/RX restart failed");
        }
        // A copied packet remains valid even if its RX restart failed.
        if (!result.packetReceived)
        {
            return;
        }


        if (
            length !=
            sizeof(Protocol::Message)
        )
        {
            Serial.printf(
                "RX INVALID SIZE | bytes=%u\n",
                length
            );


            return;
        }


        Protocol::Message message;


        memcpy(
            &message,
            buffer,
            sizeof(message)
        );


        // ----------------------------------------------------
        // Protocol validation
        // ----------------------------------------------------

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


        /*
         * Any valid packet proves that the peer is alive.
         */
        notePeerSeen();


        // ----------------------------------------------------
        // Message type dispatch
        // ----------------------------------------------------

        switch (message.type)
        {
            case Protocol::MessageType::Event:

                handleEvent(message);

                break;


            case Protocol::MessageType::Ack:

                handleAck(message);

                break;


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


    // ========================================================
    // ACK timeout + retry
    // ========================================================

    void handleAckTimeout()
    {
        if (!waitingForAck)
        {
            return;
        }


        if (
            millis() - ackWaitStart <
            ACK_TIMEOUT_MS
        )
        {
            return;
        }


        Serial.printf(
            "ACK TIMEOUT | message=%u\n",
            pendingMessage.messageId
        );


        if (
            retryCount <
            MAX_RETRIES
        )
        {
            retryCount++;


            /*
             * Retry the SAME pendingMessage.
             *
             * Therefore the message ID stays identical.
             */
            transmitPendingEvent(true);


            return;
        }


        Serial.printf(
            "GIVE UP | message=%u"
            " | retries=%u exhausted\n",
            pendingMessage.messageId,
            MAX_RETRIES
        );


        waitingForAck = false;

        retryCount = 0;


        setPeerOnline(false);


        nextEventTime =
            millis() +
            EVENT_INTERVAL_MS;
    }


    // ========================================================
    // Peer availability
    // ========================================================

    void handlePeerAvailability()
    {
        if (!peerOnline)
        {
            return;
        }


        if (
            millis() - lastPeerSeenTime >=
            PEER_OFFLINE_TIMEOUT_MS
        )
        {
            setPeerOnline(false);
        }
    }


    // ========================================================
    // FreeRTOS Radio Task
    // ========================================================

    void radioTask(void* parameter)
    {
        (void)parameter;


        Serial.println();
        Serial.println(
            "========================================"
        );

        Serial.println(
            "CC1101 FreeRTOS RadioTask"
        );

        Serial.println(
            "========================================"
        );


        Serial.printf(
            "Device: %s\n",
            DEVICE_NAME
        );


        // ----------------------------------------------------
        // CC1101 initialization
        // ----------------------------------------------------

        if (!CC1101Radio::begin() || !initializeRadio())
        {
            requestRecovery("initialization failed");
        }
        else
        {
            Serial.println("RADIO CONFIG OK | RX OK");
        }


        nextEventTime =
            millis() +
            FIRST_EVENT_DELAY_MS;


        Serial.println();
        Serial.println(
            "RadioTask running."
        );


        // ====================================================
        // Task loop
        // ====================================================

        while (true)
        {
            handleRadioRecovery();
            handleIncomingMessage();


            handleAckTimeout();


            handlePeerAvailability();
            if (radioState == RadioState::Fault)
            {
                setPeerOnline(false);
            }


            if (
                radioState == RadioState::Ready &&
                !waitingForAck &&
                (long)(
                    millis() -
                    nextEventTime
                ) >= 0
            )
            {
                startHeartbeatEvent();
            }


            /*
             * Yield CPU time to the other FreeRTOS tasks.
             *
             * Later this is where LED, motion, display,
             * etc. get their own CPU time.
             */
            vTaskDelay(
                pdMS_TO_TICKS(2)
            );
        }
    }
}


// ============================================================
// Public RadioTask interface
// ============================================================

namespace RadioTask
{
    bool begin()
    {
        /*
         * Prevent accidentally creating two radio tasks.
         */
        if (radioTaskHandle != nullptr)
        {
            return true;
        }


        BaseType_t result =
            xTaskCreate(
                radioTask,
                "RadioTask",

                // Temporary generous stack.
                // We can measure and tune this later.
                6144,

                nullptr,

                // Priority
                2,

                &radioTaskHandle
            );


        return result == pdPASS;
    }
}
