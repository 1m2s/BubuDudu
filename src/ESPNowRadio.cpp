#include "ESPNowRadio.h"

#include <Arduino.h>
#include <WiFi.h>

#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <cstring>
#include <atomic>

#include "Config.h"


namespace
{
    std::atomic<unsigned> pendingTx{0};
    std::atomic<unsigned> activeRx{0};
    // ======================================================
    // ESP-NOW configuration
    // ======================================================

    constexpr uint8_t WIFI_CHANNEL = 1;


    // ======================================================
    // Peer MAC address
    // ======================================================

#ifdef DEVICE_BUBU

    constexpr uint8_t PEER_MAC[6] =
    {
        0xE8,
        0xF6,
        0x0A,
        0x12,
        0x5B,
        0x84
    };

#elif defined(DEVICE_DUDU)

    constexpr uint8_t PEER_MAC[6] =
    {
        0xE8,
        0xF6,
        0x0A,
        0x12,
        0x4C,
        0xA4
    };

#else

#error "Device identity not configured"

#endif

    constexpr UBaseType_t RSSI_QUEUE_LENGTH = 4;
    QueueHandle_t rssiQueue = nullptr;
    std::atomic<bool> rssiEnabled{false};
    uint8_t observationLocalMac[6]{};

    void observeRssi(void* buffer, wifi_promiscuous_pkt_type_t type)
    {
        if (!rssiEnabled.load() || !buffer || type != WIFI_PKT_MGMT) return;
        const auto* packet = static_cast<const wifi_promiscuous_pkt_t*>(buffer);
        // IDF 4.4.7: sig_len includes FCS. Known unencrypted ESP-NOW v1 only:
        // MAC(24), category/OUI/random(8), vendor header(7), message(8), FCS(4).
        constexpr size_t BODY_OFFSET = 39;
        constexpr size_t FRAME_LENGTH = BODY_OFFSET + sizeof(Protocol::Message) + 4;
        if (packet->rx_ctrl.rx_state != 0 || packet->rx_ctrl.sig_len != FRAME_LENGTH) return;
        const uint8_t* frame = packet->payload; // All accesses below fit FRAME_LENGTH.
        constexpr uint8_t oui[] = {0x18, 0xFE, 0x34};
        constexpr uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        // Action frame, no DS/fragment/protection/order fields changing the layout.
        // A retry bit is allowed: each RF reception is an independent observation.
        if (frame[0] != 0xD0 || (frame[1] & 0xC7) != 0 || (frame[22] & 0x0F) != 0 ||
            memcmp(frame + 4, observationLocalMac, 6) != 0 ||
            memcmp(frame + 10, PEER_MAC, 6) != 0 ||
            memcmp(frame + 16, broadcast, 6) != 0 ||
            frame[24] != 127 || memcmp(frame + 25, oui, 3) != 0 ||
            frame[32] != 221 || frame[33] != 5 + sizeof(Protocol::Message) ||
            memcmp(frame + 34, oui, 3) != 0 || frame[37] != 4 || frame[38] != 1)
            return;

        ESPNowRadio::RssiObservation observation{};
        memcpy(&observation.message, frame + BODY_OFFSET, sizeof(observation.message));
#ifdef DEVICE_BUBU
        constexpr auto peer = Protocol::DeviceId::Dudu;
#else
        constexpr auto peer = Protocol::DeviceId::Bubu;
#endif
        if (observation.message.version != Protocol::VERSION || observation.message.sender != peer)
            return;
        memcpy(observation.sourceMac, frame + 10, sizeof(observation.sourceMac));
        observation.rssi = static_cast<int8_t>(packet->rx_ctrl.rssi);
        observation.receivedAt = uint32_t(millis());
        // Wi-Fi task: no logging, allocation, policy, ACK, or wait. Drop if full.
        (void)xQueueSend(rssiQueue, &observation, 0);
    }

    void beginRssiObservation(const uint8_t* localMac)
    {
        // Startup only. Queue storage lasts until reboot, including failed setup,
        // so even a registered callback can never access freed queue storage.
        rssiEnabled.store(false);
        memcpy(observationLocalMac, localMac, sizeof(observationLocalMac));
        if (!rssiQueue) rssiQueue = xQueueCreate(RSSI_QUEUE_LENGTH, sizeof(ESPNowRadio::RssiObservation));
        if (!rssiQueue)
        {
            Serial.println("ESPNOW RSSI | DISABLED | observation queue allocation failed");
            return;
        }
        const wifi_promiscuous_filter_t filter{WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_err_t result = esp_wifi_set_promiscuous_filter(&filter);
        if (result == ESP_OK) result = esp_wifi_set_promiscuous_rx_cb(observeRssi);
        if (result == ESP_OK)
        {
            rssiEnabled.store(true); // Publish queue/MAC before the first callback.
            result = esp_wifi_set_promiscuous(true);
        }
        if (result != ESP_OK)
        {
            rssiEnabled.store(false);
            const esp_err_t disabled = esp_wifi_set_promiscuous(false);
            Serial.printf("ESPNOW RSSI | DISABLED | setup_error=%d disable_result=%d | ESP-NOW continues\n",
                          result, disabled);
            return;
        }
        Serial.println("ESPNOW RSSI | OBSERVER READY | diagnostics only");
    }


    // ======================================================
    // Application receive handler
    // ======================================================

    ESPNowRadio::ReceiveHandler applicationReceiveHandler =
        nullptr;


    // ======================================================
    // Print MAC address
    // ======================================================

    void printMacAddress(
        const uint8_t* mac
    )
    {
        Serial.printf(
            "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0],
            mac[1],
            mac[2],
            mac[3],
            mac[4],
            mac[5]
        );
    }


    // ======================================================
    // ESP-NOW send callback
    // ======================================================

    void onDataSent(
        const uint8_t* macAddress,
        esp_now_send_status_t status
    )
    {
        Serial.print(
            "TX CALLBACK | peer="
        );

        printMacAddress(
            macAddress
        );

        Serial.print(
            " | "
        );


        if (
            status ==
            ESP_NOW_SEND_SUCCESS
        )
        {
            Serial.println(
                "SUCCESS"
            );
        }
        else
        {
            Serial.println(
                "FAILED"
            );
        }
        // Last callback operation: logging must finish before sleep is allowed.
        pendingTx.fetch_sub(1);
    }


    // ======================================================
    // ESP-NOW receive callback
    // ======================================================

    void onDataReceived(
        const uint8_t* macAddress,
        const uint8_t* data,
        int length
    )
    {
        activeRx.fetch_add(1);
        Serial.print(
            "ESP-NOW RX | from="
        );

        printMacAddress(
            macAddress
        );

        Serial.printf(
            " | bytes=%d\n",
            length
        );


        /*
         * ESPNowRadio does NOT interpret the packet.
         *
         * It simply passes the bytes upward to whoever
         * registered the receive handler.
         */
        if (
            applicationReceiveHandler !=
            nullptr
        )
        {
            applicationReceiveHandler(
                data,
                static_cast<size_t>(
                    length
                )
            );
        }
        activeRx.fetch_sub(1);
    }
}


// ==========================================================
// ESPNowRadio public interface
// ==========================================================

namespace ESPNowRadio
{
    bool takeRssiObservation(RssiObservation& observation)
    {
        return rssiEnabled.load() && xQueueReceive(rssiQueue, &observation, 0) == pdPASS;
    }

    unsigned txInFlight() { return pendingTx.load(); }
    bool receiveCallbackActive() { return activeRx.load() != 0; }
    bool begin(
        ReceiveHandler receiveHandler
    )
    {
        applicationReceiveHandler =
            receiveHandler;


        Serial.println();
        Serial.println(
            "========================================"
        );

        Serial.println(
            "ESP-NOW initialization"
        );

        Serial.println(
            "========================================"
        );


        Serial.printf(
            "Device: %s\n",
            DEVICE_NAME
        );


        // --------------------------------------------------
        // Enable Wi-Fi station interface.
        // --------------------------------------------------

        WiFi.mode(
            WIFI_STA
        );


        // --------------------------------------------------
        // Proven ESP32-C3 Super Mini configuration.
        // --------------------------------------------------

        if (!WiFi.setSleep(false))
        {
            Serial.println(
                "FAILED TO DISABLE WIFI SLEEP"
            );

            return false;
        }


        if (
            !WiFi.setTxPower(
                WIFI_POWER_8_5dBm
            )
        )
        {
            Serial.println(
                "FAILED TO SET WIFI TX POWER"
            );

            return false;
        }


        Serial.println(
            "Wi-Fi sleep: disabled"
        );

        Serial.println(
            "Wi-Fi TX power: 8.5 dBm"
        );


        // --------------------------------------------------
        // Both devices use channel 1.
        // --------------------------------------------------

        esp_err_t channelResult =
            esp_wifi_set_channel(
                WIFI_CHANNEL,
                WIFI_SECOND_CHAN_NONE
            );


        if (
            channelResult !=
            ESP_OK
        )
        {
            Serial.printf(
                "FAILED TO SET WIFI CHANNEL | error=%d\n",
                channelResult
            );

            return false;
        }


        Serial.printf(
            "Wi-Fi channel: %u\n",
            WIFI_CHANNEL
        );


        // --------------------------------------------------
        // Read local Wi-Fi MAC.
        // --------------------------------------------------

        uint8_t localMac[6];


        esp_err_t macResult =
            esp_wifi_get_mac(
                WIFI_IF_STA,
                localMac
            );


        if (
            macResult !=
            ESP_OK
        )
        {
            Serial.printf(
                "FAILED TO READ LOCAL MAC | error=%d\n",
                macResult
            );

            return false;
        }


        Serial.print(
            "Local MAC: "
        );

        printMacAddress(
            localMac
        );

        Serial.println();


        // --------------------------------------------------
        // Initialize ESP-NOW.
        // --------------------------------------------------

        esp_err_t initResult =
            esp_now_init();


        if (
            initResult !=
            ESP_OK
        )
        {
            Serial.printf(
                "ESP-NOW INIT FAILED | error=%d\n",
                initResult
            );

            return false;
        }


        Serial.println(
            "ESP-NOW initialized"
        );


        // --------------------------------------------------
        // Register ESP-NOW callbacks.
        // --------------------------------------------------

        if (
            esp_now_register_send_cb(
                onDataSent
            ) != ESP_OK
        )
        {
            Serial.println(
                "SEND CALLBACK REGISTRATION FAILED"
            );

            return false;
        }


        if (
            esp_now_register_recv_cb(
                onDataReceived
            ) != ESP_OK
        )
        {
            Serial.println(
                "RECEIVE CALLBACK REGISTRATION FAILED"
            );

            return false;
        }


        Serial.println(
            "Callbacks registered"
        );


        // --------------------------------------------------
        // Register opposite BubuDudu device.
        // --------------------------------------------------

        esp_now_peer_info_t peerInfo = {};


        memcpy(
            peerInfo.peer_addr,
            PEER_MAC,
            sizeof(PEER_MAC)
        );


        peerInfo.channel =
            WIFI_CHANNEL;


        peerInfo.ifidx =
            WIFI_IF_STA;


        peerInfo.encrypt =
            false;


        Serial.print(
            "Peer: "
        );

        printMacAddress(
            PEER_MAC
        );

        Serial.println();


        esp_err_t peerResult =
            esp_now_add_peer(
                &peerInfo
            );


        if (
            peerResult !=
            ESP_OK
        )
        {
            Serial.printf(
                "PEER REGISTRATION FAILED | error=%d\n",
                peerResult
            );

            return false;
        }


        Serial.println(
            "Peer registered"
        );

        beginRssiObservation(localMac); // Optional: never changes normal begin() success.

        return true;
    }


    // ======================================================
    // Send raw bytes
    // ======================================================

    bool send(
        const uint8_t* data,
        size_t length
    )
    {
        // Reserve BEFORE the driver call: the Wi-Fi task may deliver its
        // callback before esp_now_send returns. Rejected calls roll back the
        // reservation; only accepted sends remain until their one callback.
        pendingTx.fetch_add(1);
        esp_err_t result =
            esp_now_send(
                PEER_MAC,
                data,
                length
            );

        if (result != ESP_OK) pendingTx.fetch_sub(1);
        return result == ESP_OK;
    }
}
