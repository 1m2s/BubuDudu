#include "CC1101WakeRecovery.h"
#include "CC1101Bus.h"
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

        bool transmitAck(const Protocol::Message& ack)
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
            uint8_t raw[64];
            const uint32_t started = micros();
            if (!select(started)) { report.reason = "FIFO_READ_FAILED"; return report; }
            SPI.transfer(0xFF);
            for (uint8_t i = 0; i < count; ++i) raw[i] = SPI.transfer(0);
            releaseBus();
            // ece6879 bounds protection plus exact size for this eight-byte protocol.
            if (count != sizeof(Protocol::Message) + 1 || raw[0] != sizeof(Protocol::Message))
                report.reason = "PACKET_LENGTH_INVALID";
            else
            {
                Protocol::Message packet{};
                memcpy(&packet, &raw[1], sizeof(packet));
                report.packetRecovered = true;
                if (packet.version != Protocol::VERSION || packet.sender != peer ||
                    packet.type != Protocol::MessageType::Event ||
                    packet.event != Protocol::EventType::Heartbeat || packet.ackForMessageId != 0)
                    report.reason = "PACKET_FORMAT_INVALID";
                else if (!historyRestored)
                    report.reason = "RTC_INVALID_EVENT_NOT_DELIVERED";
                else
                {
                    const auto ack = handler(packet, report.processed);
                    report.duplicate = !report.processed;
                    report.ackSent = transmitAck(ack);
                    report.reason = report.ackSent ? "OK" : "ACK_TX_FAILED";
                }
            }
        }
        // Delivery evidence survives failed RX restart. No reset or retry loop.
        report.rxReady = restartRx();
        return report;
    }

    void printReport(const BootInfo& boot, bool restored, const Report& report)
    {
        const char* cause = boot.cause == Cause::Gpio ? "GPIO" :
                            boot.cause == Cause::Timer ? "TIMER" : "OTHER";
        Serial.printf("DEEP WAKE | cause=%s | mask=0x%llX | CC1101_GPIO_WAKE=%d\n", cause,
                      static_cast<unsigned long long>(boot.gpioMask),
                      boot.cause == Cause::Gpio && (boot.gpioMask & (1ULL << GDO0)) != 0);
        Serial.printf("RTC RESTORE | %s\n", restored ? "OK" : "INVALID");
        Serial.printf("CC1101 WAKE | GDO0_BOOT=%d MARCSTATE=0x%02X RXBYTES=0x%02X IOCFG0=0x%02X\n",
                      boot.gdoAtBoot, report.radio.marc, report.radio.rxBytes, report.radio.iocfg0);
        Serial.printf("CC1101 WAKE PACKET | recovered=%d | reason=%s\n", report.packetRecovered, report.reason);
        Serial.printf("WAKE EVENT | processed=%d duplicate=%d\n", report.processed, report.duplicate);
        Serial.printf("WAKE ACK | sent=%d | RX_READY=%d\n", report.ackSent, report.rxReady);
    }

    void benchDeepSleep(void (*saveHistory)())
    {
        if (CC1101SleepArm::prepareForSleep().result != CC1101SleepArm::Result::Ready)
        {
            Serial.println("BENCH DEEP SLEEP | REFUSED | radio not ready");
            return;
        }
        RtcState::invalidate();
        esp_err_t result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        if (result == ESP_OK)
            result = esp_deep_sleep_enable_gpio_wakeup(1ULL << GDO0, ESP_GPIO_WAKEUP_GPIO_HIGH);
        if (result == ESP_OK) result = esp_sleep_enable_timer_wakeup(30ULL * 1000000);
        if (result == ESP_OK)
        {
            digitalWrite(CS, HIGH);
            result = gpio_hold_en(static_cast<gpio_num_t>(CS));
            if (result == ESP_OK) gpio_deep_sleep_hold_en();
        }
        if (result == ESP_OK)
        {
            Serial.println("BENCH DEEP SLEEP | ARMED | GPIO4 HIGH | timer=30s BENCH SAFETY NET");
            Serial.println("BENCH DEEP SLEEP | ENTERING | USB may disconnect; p reprints wake report");
            Serial.flush();
            saveHistory(); // Final allocator/history snapshot; no packets sent after this.
            if (digitalRead(GDO0) == LOW) esp_deep_sleep_start();
        }
        // API failure, GDO racing HIGH, or unexpected return: remain awake.
        RtcState::invalidate();
        const bool released = releaseHold();
        const auto disabled = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        Serial.printf("BENCH DEEP SLEEP | ABORTED | setup=%d GDO0=%d CS_RELEASED=%d WAKE_DISABLE=%d\n",
                      result, digitalRead(GDO0), released, disabled);
    }
}
