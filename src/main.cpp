#include <Arduino.h>
#include <cstring>

#include "Config.h"
#include "CC1101Radio.h"
#include "Protocol.h"


// ============================================================
// Device configuration
// ============================================================

#ifdef DEVICE_BUBU

constexpr Protocol::DeviceId LOCAL_DEVICE =
    Protocol::DeviceId::Bubu;

constexpr Protocol::DeviceId PEER_DEVICE =
    Protocol::DeviceId::Dudu;

// Stagger first transmissions so both radios do not
// deliberately start at exactly the same instant.
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


// ============================================================
// Timing / reliability configuration
// ============================================================

constexpr unsigned long EVENT_INTERVAL_MS = 4000;

constexpr unsigned long ACK_TIMEOUT_MS = 300;

constexpr uint8_t MAX_RETRIES = 2;

constexpr unsigned long PEER_OFFLINE_TIMEOUT_MS = 7000;


// ============================================================
// Duplicate test
// ============================================================

/*
 * For ONE test, Dudu deliberately drops the first ACK it
 * would normally send.
 *
 * Result:
 *
 * Bubu sends EVENT id=X
 * Dudu receives and processes X
 * Dudu intentionally drops ACK
 * Bubu times out
 * Bubu retries SAME id=X
 * Dudu recognizes X as duplicate
 * Dudu does NOT process event again
 * Dudu sends ACK again
 *
 * This proves duplicate protection automatically.
 *
 * After we prove it works, change this to false.
 */

constexpr bool ENABLE_DUPLICATE_TEST = true;

bool duplicateTestAckDropped = false;


// ============================================================
// Runtime state
// ============================================================

bool radioReady = false;


// Local packet sequence number.
//
// EVENTs and ACKs both get their own message ID.
uint16_t nextMessageId = 1;


// ------------------------------------------------------------
// Outgoing EVENT state
// ------------------------------------------------------------

Protocol::Message pendingMessage;

bool waitingForAck = false;

uint8_t retryCount = 0;

unsigned long ackWaitStart = 0;

unsigned long nextEventTime = 0;


// ------------------------------------------------------------
// Duplicate detection
// ------------------------------------------------------------

bool haveLastPeerEvent = false;

uint16_t lastPeerEventId = 0;


// ------------------------------------------------------------
// Peer availability
// ------------------------------------------------------------

bool peerOnline = false;

unsigned long lastPeerSeenTime = 0;


// ============================================================
// Small helpers
// ============================================================

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


// ============================================================
// Raw Protocol::Message transmission
// ============================================================

bool sendProtocolMessage(
    const Protocol::Message& message
)
{
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(&message);


    bool transmitted =
        CC1101Radio::sendPacket(
            bytes,
            sizeof(message)
        );


    /*
     * sendPacket() finishes with the CC1101 in IDLE.
     *
     * Every BubuDudu device should normally be listening,
     * so immediately return to RX.
     */

    if (!CC1101Radio::startReceive())
    {
        Serial.println(
            "ERROR: failed to return CC1101 to RX"
        );

        radioReady = false;

        return false;
    }


    return transmitted;
}


// ============================================================
// ACK transmission
// ============================================================

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
     * Short turnaround guard.
     *
     * Gives the other CC1101 time to finish TX and return
     * to RX before we transmit the ACK.
     */
    delay(5);


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


// ============================================================
// EVENT transmission
// ============================================================

bool transmitPendingEvent(bool retry)
{
    bool success =
        sendProtocolMessage(
            pendingMessage
        );


    /*
     * Even if the low-level transmission reports failure,
     * keep the reliability timer running.
     *
     * The timeout/retry mechanism will decide what to do
     * next instead of creating a new message immediately.
     */

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


    return success;
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


// ============================================================
// Received ACK handling
// ============================================================

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
            millis() + EVENT_INTERVAL_MS;


        return;
    }


    /*
     * Example:
     *
     * We already gave up message 10 and are waiting
     * for message 11, but an old ACK for 10 arrives.
     *
     * It must NOT acknowledge message 11.
     */

    Serial.println(
        "ACK IGNORED: does not match pending message"
    );
}


// ============================================================
// Received EVENT handling
// ============================================================

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
         * Critical reliability behavior:
         *
         * Do NOT execute the event again.
         *
         * But DO ACK it again because the sender probably
         * retried because our previous ACK was lost.
         */

        sendAck(
            message.messageId
        );


        return;
    }


    // This is a new event.

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
     * THIS is eventually where the BubuDudu reaction goes:
     *
     * Heartbeat event
     *      ↓
     * LED heartbeat animation
     *
     * For this radio/protocol test we only print it.
     */


    // --------------------------------------------------------
    // One automatic duplicate-detection test
    // --------------------------------------------------------

#ifdef DEVICE_DUDU

    if (
        ENABLE_DUPLICATE_TEST &&
        !duplicateTestAckDropped
    )
    {
        duplicateTestAckDropped = true;


        Serial.printf(
            "TEST: intentionally dropping ACK for message %u\n",
            message.messageId
        );


        /*
         * We intentionally do NOT send the ACK.
         *
         * Bubu should retry the SAME message ID.
         */

        return;
    }

#endif


    sendAck(
        message.messageId
    );
}


// ============================================================
// Receive dispatcher
// ============================================================

void handleIncomingMessage()
{
    uint8_t buffer[32];

    uint8_t length = 0;


    if (
        !CC1101Radio::receivePacket(
            buffer,
            sizeof(buffer),
            length
        )
    )
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


    // --------------------------------------------------------
    // Basic protocol validation
    // --------------------------------------------------------

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
     * A valid packet from our peer means the peer is alive.
     */

    notePeerSeen();


    // --------------------------------------------------------
    // Dispatch by message type
    // --------------------------------------------------------

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


// ============================================================
// Timeout + retry
// ============================================================

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


    // --------------------------------------------------------
    // Retry available
    // --------------------------------------------------------

    if (
        retryCount <
        MAX_RETRIES
    )
    {
        retryCount++;


        /*
         * IMPORTANT:
         *
         * We transmit pendingMessage again.
         *
         * We do NOT generate a new message ID.
         */

        transmitPendingEvent(true);


        return;
    }


    // --------------------------------------------------------
    // No retries left
    // --------------------------------------------------------

    Serial.printf(
        "GIVE UP | message=%u"
        " | retries=%u exhausted\n",
        pendingMessage.messageId,
        MAX_RETRIES
    );


    waitingForAck = false;

    retryCount = 0;


    /*
     * Multiple failed delivery attempts are strong evidence
     * that the peer is currently unavailable.
     */

    setPeerOnline(false);


    nextEventTime =
        millis() + EVENT_INTERVAL_MS;
}


// ============================================================
// Peer availability timeout
// ============================================================

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


// ============================================================
// Setup
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1500);


    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "CC1101 reliable bidirectional protocol test"
    );

    Serial.println(
        "========================================"
    );


    Serial.printf(
        "Device: %s\n",
        DEVICE_NAME
    );


    CC1101Radio::begin();


    // --------------------------------------------------------
    // Reset
    // --------------------------------------------------------

    if (!CC1101Radio::reset())
    {
        Serial.println(
            "RESET FAILED"
        );

        return;
    }


    Serial.println(
        "RESET OK"
    );


    // --------------------------------------------------------
    // Radio configuration
    // --------------------------------------------------------

    if (
        !CC1101Radio::configureForPacketTest()
    )
    {
        Serial.println(
            "RADIO CONFIG FAILED"
        );

        return;
    }


    Serial.println(
        "RADIO CONFIG OK"
    );


    // --------------------------------------------------------
    // Enter receive mode
    // --------------------------------------------------------

    if (
        !CC1101Radio::startReceive()
    )
    {
        Serial.println(
            "FAILED TO ENTER RX"
        );

        return;
    }


    Serial.println(
        "RX OK"
    );


    radioReady = true;


    nextEventTime =
        millis() +
        FIRST_EVENT_DELAY_MS;


    Serial.println();

    Serial.printf(
        "Local device: %s\n",
        deviceName(LOCAL_DEVICE)
    );

    Serial.printf(
        "Peer device: %s\n",
        deviceName(PEER_DEVICE)
    );

    Serial.printf(
        "ACK timeout: %lu ms\n",
        ACK_TIMEOUT_MS
    );

    Serial.printf(
        "Maximum retries: %u\n",
        MAX_RETRIES
    );


#ifdef DEVICE_DUDU

    if (ENABLE_DUPLICATE_TEST)
    {
        Serial.println(
            "Duplicate test ENABLED:"
            " first ACK will be intentionally dropped."
        );
    }

#endif


    Serial.println();

    Serial.println(
        "Reliable bidirectional messaging running..."
    );
}


// ============================================================
// Main loop
// ============================================================

void loop()
{
    if (!radioReady)
    {
        return;
    }


    // Receive EVENTs and ACKs from peer.
    handleIncomingMessage();


    // Check whether our outgoing EVENT timed out.
    handleAckTimeout();


    // Track peer availability.
    handlePeerAvailability();


    /*
     * Stop-and-wait protocol:
     *
     * Only create a new EVENT when we do not already
     * have one waiting for acknowledgement.
     */

    if (
        !waitingForAck &&
        (long)(
            millis() - nextEventTime
        ) >= 0
    )
    {
        startHeartbeatEvent();
    }


    delay(2);
}