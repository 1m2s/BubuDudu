#include <Arduino.h>

#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Config.h"
#include "ESPNowRadio.h"
#include "Protocol.h"


namespace
{
    constexpr UBaseType_t RX_QUEUE_LENGTH = 8;
    QueueHandle_t receiveQueue = nullptr;
    bool protocolReady = false;


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
    // Transmit pending EVENT
    // ======================================================

    void transmitPendingEvent(
        bool retry
    )
    {
        bool accepted =
            sendProtocolMessage(
                pendingMessage
            );


        /*
         * Start / restart the ACK timeout timer.
         */
        ackWaitStart =
            millis();


        if (retry)
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


        transmitPendingEvent(
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

    void handleEvent(
        const Protocol::Message& message
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
            sendAck(
                message.messageId
            );


            return;
        }


        // --------------------------------------------------
        // New EVENT
        // --------------------------------------------------

        haveLastPeerEvent =
            true;


        lastPeerEventId =
            message.messageId;


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
            DROP_FIRST_ACK_FOR_TEST &&
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


            return;
        }


        // --------------------------------------------------
        // Normal ACK
        // --------------------------------------------------

        sendAck(
            message.messageId
        );
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


        // --------------------------------------------------
        // Dispatch by message type
        // --------------------------------------------------

        switch (
            message.type
        )
        {
            case Protocol::MessageType::Event:

                handleEvent(
                    message
                );

                break;


            case Protocol::MessageType::Ack:

                handleAck(
                    message
                );

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
            transmitPendingEvent(
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


        nextEventTime =
            millis() +
            EVENT_INTERVAL_MS;
    }
}


// ==========================================================
// Arduino setup
// ==========================================================

void setup()
{
    Serial.begin(
        115200
    );


    delay(
        1500
    );


    Serial.println();
    Serial.println(
        "Starting BubuDudu ESP-NOW reliability test..."
    );


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

    handleAckTimeout();


    // ------------------------------------------------------
    // Only create a new EVENT if there is no previous EVENT
    // still waiting for its ACK.
    // ------------------------------------------------------

    if (
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
