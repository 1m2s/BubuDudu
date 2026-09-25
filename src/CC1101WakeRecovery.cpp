#include "CC1101WakeRecovery.h"
#include "CC1101Bus.h"
#include "Config.h"
#include "RtcState.h"
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <cstring>

namespace CC1101WakeRecovery
{
    namespace
    {
        using namespace CC1101Bus;
        constexpr uint8_t SIDLE = 0x36, SFRX = 0x3A, SRX = 0x34;
        constexpr uint8_t SFTX = 0x3B, STX = 0x35, MARCSTATE = 0x35;

        bool releaseHold()
        {
            digitalWrite(CS, HIGH);
            pinMode(CS, OUTPUT);
            gpio_deep_sleep_hold_dis();
            return gpio_hold_dis(static_cast<gpio_num_t>(CS)) == ESP_OK;
        }

        bool strobe(uint8_t command, uint32_t started)
        {
            if (!select(started)) return false;
            SPI.transfer(command); releaseBus();
            return withinBudget(started);
        }

        bool waitState(uint8_t target, uint32_t started)
        {
            while (withinBudget(started))
            {
                uint8_t state;
                if (!read(MARCSTATE, state, started) || state == 0xFF) return false;
                if ((state & 0x1F) == target) return true;
                delayMicroseconds(100);
            }
            return false;
        }

        bool restartRx()
        {
            // Only called AFTER retained FIFO inspection/copy. One bounded try.
            const uint32_t started = micros();
            return strobe(SIDLE, started) && waitState(1, started) &&
                   strobe(SFRX, started) && strobe(SRX, started) && waitState(0x0D, started);
        }

        // One retained receipt, not a general awake CC1101 transport. It remains
        // valid even if ESP-NOW advances the application's last EVENT history.
        Protocol::Message wakeAck{};
        bool haveWakeAck = false;
        enum class AwakeState { Listening, AckTx, Stopped };
        AwakeState awakeState = AwakeState::Listening;
        uint32_t awakeTxStarted = 0;

        bool startAck(const Protocol::Message& ack)
        {
            const uint32_t started = micros();
            if (!strobe(SIDLE, started) || !waitState(1, started) || !strobe(SFTX, started))
                return false;
            // The receive-only arm checkpoint omitted TX power. Proven ece6879
            // PATABLE=0x60 is installed here, AFTER the wake packet was copied.
            if (!select(started)) return false;
            SPI.transfer(0x3E); SPI.transfer(0x60); releaseBus();
            if (!select(started)) return false;
            SPI.transfer(0x7F); SPI.transfer(sizeof(ack));
            const auto* bytes = reinterpret_cast<const uint8_t*>(&ack);
            for (size_t i = 0; i < sizeof(ack); ++i) SPI.transfer(bytes[i]);
            releaseBus();
            if (!strobe(STX, started)) return false;
            return true;
        }

        bool transmitAck(const Protocol::Message& ack)
        {
            if (!startAck(ack)) return false;
            delayMicroseconds(1000); // Proven TX/calibration settling interval.
            const uint32_t txStarted = micros();
            while (uint32_t(micros() - txStarted) < 200000)
            {
                uint8_t state;
                if (!read(MARCSTATE, state, micros()) || state == 0xFF) return false;
                if ((state & 0x1F) == 1) return true;
                if ((state & 0x1F) == 0x16) return false;
                delayMicroseconds(100);
            }
            return false;
        }

        // Caller has checked overflow/count and established a complete IDLE
        // packet. The profile disables appended status: length + eight bytes.
        enum class PacketRead { Unavailable, Rejected, Valid };
        PacketRead readWakePacket(Report& report, Protocol::DeviceId peer, Protocol::Message& packet)
        {
            const auto count = report.radio.rxBytes & 0x7F;
            uint8_t raw[64];
            const uint32_t started = micros();
            if (!select(started)) { report.reason = "FIFO_READ_FAILED"; return PacketRead::Unavailable; }
            SPI.transfer(0xFF);
            for (uint8_t i = 0; i < count; ++i) raw[i] = SPI.transfer(0);
            releaseBus();
            if (count != sizeof(packet) + 1 || raw[0] != sizeof(packet))
            {
                report.reason = "PACKET_LENGTH_INVALID";
                return PacketRead::Rejected;
            }
            memcpy(&packet, &raw[1], sizeof(packet));
            report.packetRecovered = true;
            if (packet.version != Protocol::VERSION || packet.sender != peer ||
                packet.type != Protocol::MessageType::Event ||
                packet.event != Protocol::EventType::Heartbeat || packet.ackForMessageId != 0)
            {
                report.reason = "PACKET_FORMAT_INVALID";
                return PacketRead::Rejected;
            }
            return PacketRead::Valid;
        }

        void finishAwake(const char* reason, bool duplicate, bool ackSent)
        {
            // One existing bounded RX restart, never an automatic recovery loop.
            const bool ready = restartRx();
            awakeState = ready ? AwakeState::Listening : AwakeState::Stopped;
            Serial.printf("CC1101 AWAKE | reason=%s | processed=0 duplicate=%d | ACK_SENT=%d | RX_READY=%d\n",
                          reason, duplicate, ackSent, ready);
            if (!ready) Serial.println("CC1101 AWAKE | STOPPED | RX restart failed");
        }

    }

    BootInfo captureBoot()
    {
        BootInfo boot;
        boot.deep = esp_reset_reason() == ESP_RST_DEEPSLEEP;
        if (!boot.deep) return boot;
        const auto cause = esp_sleep_get_wakeup_cause();
        boot.cause = cause == ESP_SLEEP_WAKEUP_GPIO ? Cause::Gpio :
                     cause == ESP_SLEEP_WAKEUP_TIMER ? Cause::Timer : Cause::Other;
        if (boot.cause == Cause::Gpio) boot.gpioMask = esp_sleep_get_gpio_wakeup_status();
        pinMode(GDO0, INPUT_PULLDOWN);
        boot.gdoAtBoot = digitalRead(GDO0);
        pinMode(MOTION_INT1_PIN, INPUT);
        boot.motionAtBoot = digitalRead(MOTION_INT1_PIN);
        return boot;
    }

    Report recover(bool historyRestored, Protocol::DeviceId peer, EventHandler handler)
    {
        Report report;
        if (!releaseHold()) { report.reason = "CS_RELEASE_FAILED"; return report; }
        CC1101SleepArm::attachRetained(); // NO SRES/SIDLE/SFRX/configuration.
        report.radio = CC1101SleepArm::prepareForSleep();
        using Result = CC1101SleepArm::Result;
        if (report.radio.result == Result::RadioUnavailable || report.radio.result == Result::WrongConfig)
        {
            report.reason = CC1101SleepArm::toString(report.radio.result);
            return report; // Unknown configuration: don't consume or flush FIFO.
        }
        const auto count = report.radio.rxBytes & 0x7F;
        const auto state = report.radio.marc & 0x1F;
        if ((report.radio.rxBytes & 0x80) || state == 0x11)
            report.reason = "RX_OVERFLOW";
        else if (count > 64)
            report.reason = "RX_COUNT_INVALID";
        else if (count == 0)
        {
            report.reason = "EMPTY_FIFO";
            if (state == 0x0D && report.radio.gdo == LOW)
            {
                report.rxReady = true; // Timer fallback in healthy RX: leave alone.
                return report;
            }
        }
        else if (state != 1)
        {
            report.reason = "PACKET_NOT_COMPLETE";
            return report; // Preserve an in-progress packet; no blind flush.
        }
        else
        {
            Protocol::Message packet{};
            const auto readResult = readWakePacket(report, peer, packet);
            if (readResult == PacketRead::Unavailable) return report;
            if (readResult == PacketRead::Valid)
            {
                if (!historyRestored)
                    report.reason = "RTC_INVALID_EVENT_NOT_DELIVERED";
                else
                {
                    wakeAck = handler(packet, report.processed);
                    haveWakeAck = true; // Save before TX: its ACK may fail or be lost.
                    report.duplicate = !report.processed;
                    report.ackSent = transmitAck(wakeAck);
                    report.reason = report.ackSent ? "OK" : "ACK_TX_FAILED";
                }
            }
        }
        // Delivery evidence survives failed RX restart. No reset or retry loop.
        report.rxReady = restartRx();
        return report;
    }

    bool awakeBusy()
    {
        return awakeState == AwakeState::AckTx;
    }

    void serviceAwake(Protocol::DeviceId peer)
    {
        if (awakeState == AwakeState::Stopped) return;
        if (awakeState == AwakeState::AckTx)
        {
            const uint32_t elapsed = uint32_t(micros() - awakeTxStarted);
            if (elapsed < 1000) return; // Same TX settling interval as boot, without waiting.
            uint8_t state;
            if (!read(MARCSTATE, state, micros()) || state == 0xFF)
                finishAwake("ACK_TX_FAILED", true, false);
            else if (elapsed >= 200000)
                finishAwake("ACK_TX_TIMEOUT", true, false);
            else if ((state & 0x1F) == 1)
                finishAwake("REACK", true, true);
            else if ((state & 0x1F) == 0x16)
                finishAwake("ACK_TX_UNDERFLOW", true, false);
            return; // At most one status sample; ESP-NOW/FSM work runs every loop.
        }
        if (digitalRead(GDO0) == LOW) return; // No SPI traffic or waiting without a packet.
        Report report;
        report.radio = CC1101SleepArm::prepareForSleep();
        using Result = CC1101SleepArm::Result;
        if (report.radio.result == Result::RadioUnavailable || report.radio.result == Result::WrongConfig)
        {
            awakeState = AwakeState::Stopped; // Preserve unknown FIFO/configuration; no repeated fault work.
            Serial.printf("CC1101 AWAKE | STOPPED | reason=%s\n", CC1101SleepArm::toString(report.radio.result));
            return;
        }
        const auto count = report.radio.rxBytes & 0x7F;
        const auto state = report.radio.marc & 0x1F;
        if ((report.radio.rxBytes & 0x80) || state == 0x11 || count > 64)
        {
            finishAwake("RX_COUNT_OR_OVERFLOW", false, false);
            return;
        }
        if (state != 1) return; // In-progress data must not be consumed or flushed.
        if (count == 0)
        {
            finishAwake("EMPTY_FIFO", false, false);
            return;
        }
        Protocol::Message packet{};
        const auto readResult = readWakePacket(report, peer, packet);
        if (readResult == PacketRead::Unavailable)
        {
            awakeState = AwakeState::Stopped; // FIFO was not copied; never flush it blindly.
            Serial.println("CC1101 AWAKE | STOPPED | reason=FIFO_READ_FAILED");
        }
        else if (readResult == PacketRead::Rejected)
            finishAwake(report.reason, false, false);
        else if (!haveWakeAck || packet.messageId != wakeAck.ackForMessageId)
            finishAwake("NOT_WAKE_RETRY", false, false); // No new awake application behavior.
        else if (!startAck(wakeAck))
            finishAwake("ACK_TX_FAILED", true, false);
        else
        {
            awakeTxStarted = micros();
            awakeState = AwakeState::AckTx;
            Serial.printf("CC1101 AWAKE RETRY | id=%u | re-ACK started\n", packet.messageId);
        }
    }

    void printReport(const BootInfo& boot, bool restored, const Report& report)
    {
        const char* cause = boot.cause == Cause::Gpio ? "GPIO" :
                            boot.cause == Cause::Timer ? "TIMER" : "OTHER";
        const bool radioWake = boot.wokeFromGpio(GDO0);
        const bool motionWake = boot.wokeFromGpio(MOTION_INT1_PIN);
        const char* source = radioWake && motionWake ? "CC1101+MOTION" :
                             radioWake ? "CC1101" : motionWake ? "MOTION" : cause;
        Serial.printf("DEEP WAKE | cause=%s | mask=0x%llX | source=%s | CC1101_GPIO_WAKE=%d MOTION_GPIO_WAKE=%d\n",
                      cause, static_cast<unsigned long long>(boot.gpioMask), source, radioWake, motionWake);
        Serial.printf("MOTION WAKE | GPIO3_BOOT=%d\n", boot.motionAtBoot);
        Serial.printf("RTC RESTORE | %s\n", restored ? "OK" : "INVALID");
        Serial.printf("CC1101 WAKE | GDO0_BOOT=%d MARCSTATE=0x%02X RXBYTES=0x%02X IOCFG0=0x%02X\n",
                      boot.gdoAtBoot, report.radio.marc, report.radio.rxBytes, report.radio.iocfg0);
        Serial.printf("CC1101 WAKE PACKET | recovered=%d | reason=%s\n", report.packetRecovered, report.reason);
        Serial.printf("WAKE EVENT | processed=%d duplicate=%d\n", report.processed, report.duplicate);
        Serial.printf("WAKE ACK | sent=%d | RX_READY=%d\n", report.ackSent, report.rxReady);
    }

    void enterDeepSleep(void (*saveHistory)(), const char* (*blockedReason)(), bool coordinated)
    {
        const char* label = coordinated ? "COORDINATED DEEP SLEEP" : "BENCH DEEP SLEEP";
        RtcState::invalidate();
        const auto arm = CC1101SleepArm::prepareForSleep();
        Serial.printf("CC1101 SLEEP ARM | %s | reason=%s | MARCSTATE=0x%02X RXBYTES=0x%02X GDO0=%d\n",
                      arm.result == CC1101SleepArm::Result::Ready ? "READY" : "FAILED",
                      CC1101SleepArm::toString(arm.result), arm.marc, arm.rxBytes, arm.gdo);
        const char* reason = arm.result == CC1101SleepArm::Result::Ready ? blockedReason() :
                             CC1101SleepArm::toString(arm.result);
        esp_err_t result = ESP_OK;
        if (!reason)
        {
            result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            if (result == ESP_OK)
                result = esp_deep_sleep_enable_gpio_wakeup((1ULL << GDO0) | (1ULL << MOTION_INT1_PIN),
                                                           ESP_GPIO_WAKEUP_GPIO_HIGH);
            if (result == ESP_OK) result = esp_sleep_enable_timer_wakeup(30ULL * 1000000);
            if (result == ESP_OK)
            {
                digitalWrite(CS, HIGH);
                result = gpio_hold_en(static_cast<gpio_num_t>(CS));
                if (result == ESP_OK) gpio_deep_sleep_hold_en();
            }
            reason = "SETUP_FAILED";
            if (result == ESP_OK)
            {
                Serial.printf("%s | ARMED | GPIO4+GPIO3 HIGH | mask=0x18 | timer=30s INTEGRATION SAFETY TIMER\n", label);
                Serial.printf("%s | ENTERING | USB may disconnect; p reprints wake report\n", label);
                Serial.flush();
                // Recheck AFTER arm inspection, wake-source setup and logging.
                reason = blockedReason();
                if (!reason)
                {
                    saveHistory(); // Final allocator/history snapshot, no subsequent send.
                    reason = blockedReason(); // Catch RX arriving while RTC was written.
                    if (!reason)
                    {
                        reason = "GDO_HIGH";
                        if (digitalRead(GDO0) == LOW)
                        {
                            esp_deep_sleep_start(); // Success does not return.
                            reason = "DEEP_SLEEP_RETURNED";
                        }
                    }
                }
            }
        }
        // Any refusal/API failure/GDO race/return: discard checkpoint and holds.
        RtcState::invalidate();
        const bool released = releaseHold();
        const auto disabled = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        Serial.printf("%s | ABORTED | reason=%s | setup=%d CS_RELEASED=%d WAKE_DISABLE=%d\n",
                      label, reason, result, released, disabled);
    }
}
