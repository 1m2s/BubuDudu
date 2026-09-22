#include "RadioTask.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_attr.h>

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

    // Temporary bench mode suppresses only automatic heartbeat generation.
    // All commands execute here, keeping RadioTask the sole radio owner.
    bool wakeTestMode = false;
    constexpr gpio_num_t WAKE_GPIO = static_cast<gpio_num_t>(CC1101_GDO0_GPIO);
    constexpr uint64_t WAKE_TEST_TIMEOUT_US = 30ULL * 1000000;
    struct WakeReport
    {
        bool available = false;
        esp_err_t result = ESP_OK;
        esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
        int gdoLevel = LOW;
        int64_t elapsedUs = 0;
    } lastWake;

    // Only this deliberate sleep checkpoint survives reboot. No transaction
    // is pending at entry; uptime-based timers and peer-online state restart.
    constexpr uint32_t DEEP_CHECKPOINT_MAGIC = 0x43433144;
    RTC_DATA_ATTR struct
    {
        uint32_t magic;
        uint16_t nextId;
        uint16_t lastPeerId;
        bool havePeerId;
    } deepCheckpoint = {};

    struct IncomingResult
    {
        bool packetCopied;
        bool peerEventProcessed;
        bool ackSent;
    };

    struct DeepWakeReport
    {
        bool available = false;
        bool checkpointRestored = false;
        esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
        uint64_t gpioMask = 0;
        int gdoAtBoot = LOW;
        int gdoBeforeFifo = LOW;
        uint8_t marc = 0xFF;
        uint8_t rxBytes = 0xFF;
        uint8_t iocfg0 = 0xFF;
        IncomingResult packet = {false, false, false};
    } deepWake;


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

    bool sendAck(uint16_t receivedMessageId)
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
        return success;
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

    bool handleEvent(
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
            return sendAck(
                message.messageId
            );
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


        return sendAck(
            message.messageId
        );
    }


    // ========================================================
    // Receive one packet and dispatch it
    // ========================================================

    IncomingResult handleIncomingMessage()
    {
        IncomingResult delivery = {false, false, false};
        if (radioState != RadioState::Ready)
        {
            return delivery;
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
            return delivery;
        }
        delivery.packetCopied = true;


        if (
            length !=
            sizeof(Protocol::Message)
        )
        {
            Serial.printf(
                "RX INVALID SIZE | bytes=%u\n",
                length
            );


            return delivery;
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


            return delivery;
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


            return delivery;
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

                delivery.peerEventProcessed = true;
                delivery.ackSent = handleEvent(message);

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
        return delivery;
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
    // Temporary remote-wake bench commands
    // ========================================================

    void printDeepWakeReport()
    {
        if (!deepWake.available)
            return;
        const char* cause = deepWake.cause == ESP_SLEEP_WAKEUP_GPIO ? "GPIO" :
                            deepWake.cause == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "OTHER";
        const bool electrical = deepWake.cause == ESP_SLEEP_WAKEUP_GPIO &&
                                (deepWake.gpioMask & (1ULL << CC1101_GDO0_GPIO));
        Serial.printf("DEEP WAKE: cause=%s(%d) GPIO_mask=0x%llX "
                      "GDO0_at_boot=%d GDO0_before_FIFO=%d checkpoint=%s\n",
                      cause, static_cast<int>(deepWake.cause),
                      static_cast<unsigned long long>(deepWake.gpioMask),
                      deepWake.gdoAtBoot, deepWake.gdoBeforeFifo,
                      deepWake.checkpointRestored ? "restored" : "missing");
        Serial.printf("DEEP RADIO BEFORE FIFO: MARCSTATE=0x%02X RXBYTES=0x%02X "
                      "IOCFG0=0x%02X\n", deepWake.marc, deepWake.rxBytes, deepWake.iocfg0);
        Serial.printf("ELECTRICAL DEEP-WAKE SUCCESS=%s\n", electrical ? "YES" : "NO");
        Serial.printf("DEEP PACKET: copied=%d peer_event_processed=%d ack_tx=%d\n",
                      deepWake.packet.packetCopied, deepWake.packet.peerEventProcessed,
                      deepWake.packet.ackSent);
        Serial.println(electrical && deepWake.packet.peerEventProcessed && deepWake.packet.ackSent ?
                       "FULL PROTOCOL DEEP-WAKE: receiver steps passed; require sender ACK MATCHED" :
                       "FULL PROTOCOL DEEP-WAKE: NOT VERIFIED");
    }


    void resumeRadioAfterDeepSleep()
    {
        wakeTestMode = true;
        if (deepCheckpoint.magic == DEEP_CHECKPOINT_MAGIC)
        {
            nextMessageId = deepCheckpoint.nextId;
            lastPeerEventId = deepCheckpoint.lastPeerId;
            haveLastPeerEvent = deepCheckpoint.havePeerId;
            deepWake.checkpointRestored = true;
        }
        deepCheckpoint.magic = 0; // Consume once; never restore after a cold reset.

        // MCU SPI/pin setup only: no SRES, SIDLE, SFRX, SRX or register writes.
        if (!CC1101Radio::begin())
        {
            requestRecovery("deep-wake SPI/pin setup failed");
            return;
        }
        deepWake.gdoBeforeFifo = digitalRead(WAKE_GPIO);
        deepWake.marc = CC1101Radio::readMarcState();
        deepWake.rxBytes = CC1101Radio::readRxBytes();
        deepWake.iocfg0 = CC1101Radio::readRegister(0x02);

        // Snapshot first. Existing bounded FIFO reader copies a complete packet
        // BEFORE its RX restart; dispatch and ACK then use the normal protocol.
        if (deepWake.marc != 0xFF && deepWake.rxBytes != 0xFF && deepWake.iocfg0 == 0x07)
        {
            deepWake.packet = handleIncomingMessage();
            const uint8_t state = deepWake.marc & 0x1F;
            if (state != 0x01 && state != 0x0D && state != 0x11)
                requestRecovery("unexpected retained radio state after deep wake");
        }
        else
        {
            requestRecovery("deep-wake register inspection failed");
        }
        // Successful retained RX needs no full reset. Any fault uses the
        // existing three-attempt recovery AFTER the inspection above.
    }


    void deepSleepForRadioPacket()
    {
        if (!wakeTestMode || radioState != RadioState::Ready || waitingForAck)
        {
            Serial.println("DEEP SLEEP REFUSED: use t; wait for pending ACK/retries; radio must be ready");
            return;
        }
        const uint8_t state = CC1101Radio::readMarcState();
        const uint8_t output = CC1101Radio::readRegister(0x02);
        if (state == 0xFF || output != 0x07)
        {
            requestRecovery("deep-wake entry register check failed");
            return;
        }
        if (digitalRead(WAKE_GPIO) == HIGH || (state & 0x1F) != 0x0D)
        {
            Serial.println("DEEP SLEEP REFUSED: require GDO LOW and CC1101 RX; retry d");
            return;
        }

        esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        if (result == ESP_OK)
            result = esp_deep_sleep_enable_gpio_wakeup(1ULL << CC1101_GDO0_GPIO,
                                                       ESP_GPIO_WAKEUP_GPIO_HIGH);
        if (result == ESP_OK)
            result = esp_sleep_enable_timer_wakeup(WAKE_TEST_TIMEOUT_US);
        if (result != ESP_OK)
        {
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            Serial.printf("DEEP SLEEP SETUP FAILED: %d\n", result);
            return;
        }
        if (!CC1101Radio::holdChipSelectForDeepSleep(true))
        {
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            Serial.println("DEEP SLEEP REFUSED: cannot hold radio CS HIGH");
            return;
        }

        Serial.println("DEEP WAKE ARMED: GDO0 -> GPIO4 HIGH; CC1101 RX; ESP32 DEEP SLEEP; timer=30s");
        Serial.println("ESP32 will reboot. Send w on peer; reconnect USB and use g for saved evidence.");
        Serial.flush();
        if (digitalRead(WAKE_GPIO) == HIGH)
        {
            if (!CC1101Radio::holdChipSelectForDeepSleep(false))
                requestRecovery("failed to release CS after cancelled deep sleep");
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            Serial.println("DEEP SLEEP CANCELLED: packet arrived before sleep");
            return;
        }

        deepCheckpoint.nextId = nextMessageId;
        deepCheckpoint.lastPeerId = lastPeerEventId;
        deepCheckpoint.havePeerId = haveLastPeerEvent;
        deepCheckpoint.magic = DEEP_CHECKPOINT_MAGIC;
        esp_deep_sleep_start(); // Does not return; setup captures the wake.
    }


    void printWakeReport()
    {
        if (!lastWake.available)
        {
            Serial.println("WAKE: no sleep attempt recorded");
            return;
        }
        const char* cause = lastWake.cause == ESP_SLEEP_WAKEUP_GPIO ? "GPIO" :
                            lastWake.cause == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "OTHER";
        const bool cc1101Wake = lastWake.result == ESP_OK &&
                               lastWake.cause == ESP_SLEEP_WAKEUP_GPIO &&
                               lastWake.gdoLevel == HIGH;
        Serial.printf("WAKE: result=%d cause=%s(%d) GDO0_at_return=%d "
                      "CC1101_GDO_WAKE=%s elapsed_ms=%llu\n",
                      lastWake.result, cause, static_cast<int>(lastWake.cause),
                      lastWake.gdoLevel, cc1101Wake ? "YES" : "NO",
                      static_cast<unsigned long long>(lastWake.elapsedUs / 1000));
    }


    void sleepForRadioPacket()
    {
        // Never interrupt a reliable outgoing transaction or recovery episode.
        if (!wakeTestMode || radioState != RadioState::Ready || waitingForAck)
        {
            Serial.println("SLEEP REFUSED: use t; wait for pending ACK/retries; radio must be ready");
            return;
        }

        const uint8_t state = CC1101Radio::readMarcState();
        const uint8_t output = CC1101Radio::readRegister(0x02);
        if (state == 0xFF || output != 0x07)
        {
            requestRecovery("wake-test register check failed");
            return;
        }
        if (digitalRead(WAKE_GPIO) == HIGH || (state & 0x1F) != 0x0D)
        {
            // Do not flush an already received packet just to enter sleep.
            Serial.println("SLEEP REFUSED: GDO high or RX not idle-listening; retry s");
            return;
        }

        // This standalone test owns the sleep sources; GPIO4 is the sole GPIO.
        esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        if (result == ESP_OK)
            result = gpio_wakeup_enable(WAKE_GPIO, GPIO_INTR_HIGH_LEVEL);
        if (result == ESP_OK)
            result = esp_sleep_enable_gpio_wakeup();
        if (result == ESP_OK)
            result = esp_sleep_enable_timer_wakeup(WAKE_TEST_TIMEOUT_US);
        if (result != ESP_OK)
        {
            gpio_wakeup_disable(WAKE_GPIO);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            Serial.printf("SLEEP SETUP FAILED: %d\n", result);
            return;
        }

        Serial.println("WAKE ARMED: GDO0 -> GPIO4 HIGH; CC1101 RX; ESP32 LIGHT SLEEP; timer=30s");
        Serial.println("Send w on peer. USB may pause; g reprints the saved wake result afterward.");
        Serial.flush();

        // A packet can arrive during Serial output. Leave it for normal RX.
        if (digitalRead(WAKE_GPIO) == HIGH)
        {
            gpio_wakeup_disable(WAKE_GPIO);
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            Serial.println("SLEEP CANCELLED: packet arrived before sleep");
            return;
        }

        const int64_t started = esp_timer_get_time();
        result = esp_light_sleep_start();
        // Capture evidence before reading/flushing the radio FIFO clears GDO.
        lastWake.result = result;
        lastWake.cause = result == ESP_OK ? esp_sleep_get_wakeup_cause() :
                                          ESP_SLEEP_WAKEUP_UNDEFINED;
        lastWake.gdoLevel = digitalRead(WAKE_GPIO);
        lastWake.elapsedUs = esp_timer_get_time() - started;
        lastWake.available = true;
        gpio_wakeup_disable(WAKE_GPIO);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        // No radio reset or RX flush here: consume the wake packet and ACK it
        // through the existing handler before lengthy diagnostic output.
        handleIncomingMessage();
        printWakeReport();
        Serial.println("WAKE TEST: communication resumed; manual mode remains active (n restores auto)");
    }


    void handleTestCommand()
    {
        // One character per iteration keeps RX and timeout processing running.
        if (Serial.available() == 0)
            return;

        const char command = Serial.read();
        switch (command)
        {
            case 't':
                wakeTestMode = true;
                Serial.println("WAKE TEST: manual mode; automatic heartbeats paused; ACK/retries still active");
                break;
            case 'n':
                wakeTestMode = false;
                nextEventTime = millis() + EVENT_INTERVAL_MS;
                Serial.println("WAKE TEST: normal automatic heartbeats restored");
                break;
            case 's':
                sleepForRadioPacket();
                break;
            case 'd':
                deepSleepForRadioPacket();
                break;
            case 'w':
                if (!wakeTestMode || radioState != RadioState::Ready || waitingForAck)
                {
                    Serial.println("WAKE TX REFUSED: use t; wait for pending ACK/retries; radio must be ready");
                    break;
                }
                Serial.println("WAKE TX: ordinary heartbeat; same ID on retries, 300ms timeout, max 2 retries");
                startHeartbeatEvent();
                break;
            case 'g':
                Serial.printf("GDO0 GPIO%u=%d mode=%s radio=%s pendingAck=%d\n",
                              CC1101_GDO0_GPIO, digitalRead(WAKE_GPIO),
                              wakeTestMode ? "manual" : "auto",
                              radioState == RadioState::Ready ? "ready" :
                              radioState == RadioState::Recovering ? "recovering" : "fault",
                              waitingForAck);
                if (radioState == RadioState::Ready)
                {
                    // Read-only diagnostics never consume or flush the FIFO.
                    const uint8_t state = CC1101Radio::readMarcState();
                    const uint8_t output = CC1101Radio::readRegister(0x02);
                    Serial.printf("RADIO: MARCSTATE=0x%02X IOCFG0=0x%02X (expected 0x07)\n",
                                  state, output);
                    if (state == 0xFF || output != 0x07)
                        requestRecovery("diagnostic register check failed");
                }
                printWakeReport();
                printDeepWakeReport();
                break;
            case '?':
                Serial.println("Commands: t=manual test, s=light sleep, d=deep sleep, w=wake heartbeat, "
                               "g=GDO/state/last wake, n=normal traffic");
                break;
            default:
                break;
        }
    }


    // ========================================================
    // FreeRTOS Radio Task
    // ========================================================

    void radioTask(void* parameter)
    {
        (void)parameter;

        // Do this before banners and normal initialization can delay the ACK
        // or destroy the external radio's retained FIFO/configuration.
        if (deepWake.available)
            resumeRadioAfterDeepSleep();

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

        pinMode(WAKE_GPIO, INPUT_PULLDOWN);
        if (deepWake.available)
        {
            printDeepWakeReport();
            Serial.println("DEEP WAKE: manual mode retained; g reprints evidence; n restores auto");
        }
        else if (!CC1101Radio::begin() || !initializeRadio())
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
        Serial.println("Remote-wake bench commands: ? for help. Use t on BOTH boards before s/d/w.");


        // ====================================================
        // Task loop
        // ====================================================

        while (true)
        {
            handleRadioRecovery();
            handleIncomingMessage();


            handleAckTimeout();

            handleTestCommand();


            handlePeerAvailability();
            if (radioState == RadioState::Fault)
            {
                setPeerOnline(false);
            }


            if (
                !wakeTestMode &&
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
    bool captureBootWake()
    {
        // No Serial or SPI before this snapshot. Configure the MCU input only;
        // reading GDO does not acknowledge or clear the CC1101 packet latch.
        deepWake.available = esp_reset_reason() == ESP_RST_DEEPSLEEP;
        if (deepWake.available)
        {
            deepWake.cause = esp_sleep_get_wakeup_cause();
            deepWake.gpioMask = deepWake.cause == ESP_SLEEP_WAKEUP_GPIO ?
                                esp_sleep_get_gpio_wakeup_status() : 0;
            pinMode(WAKE_GPIO, INPUT_PULLDOWN);
            deepWake.gdoAtBoot = digitalRead(WAKE_GPIO);
        }
        else
        {
            deepCheckpoint.magic = 0;
        }
        return deepWake.available;
    }


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
