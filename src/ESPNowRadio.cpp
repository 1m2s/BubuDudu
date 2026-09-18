#include "ESPNowRadio.h"

#include <Arduino.h>
#include <WiFi.h>

#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>

#include "Config.h"


namespace
{
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
    }
}


// ==========================================================
// ESPNowRadio public interface
// ==========================================================

namespace ESPNowRadio
{
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
        esp_err_t result =
            esp_now_send(
                PEER_MAC,
                data,
                length
            );


        return result == ESP_OK;
    }
}