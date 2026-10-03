#include "CC1101SleepArm.h"

#include <Arduino.h>
#include "CC1101Bus.h"

namespace CC1101SleepArm
{
    namespace
    {
        // Pin mapping, SPI mode/rate, reset sequence and RX profile adapted
        // from ece6879: CC1101Radio::begin/reset/configureForPacketTest.
        using namespace CC1101Bus;
        constexpr uint8_t IOCFG0 = 0x02, PARTNUM = 0x30, VERSION = 0x31;
        constexpr uint8_t MARCSTATE = 0x35, RXBYTES = 0x3B;
        constexpr uint8_t SRES = 0x30, SRX = 0x34;
        bool initialized = false;

        struct Setting { uint8_t address; uint8_t value; };
        const Setting profile[] = {
            {0x03, 0x47}, // FIFOTHR
            {0x02, 0x07}, // IOCFG0: CRC-valid packet latch
            {0x04, 0xD3}, {0x05, 0x91}, // SYNC1/0
            {0x06, 61}, {0x07, 0x08}, {0x08, 0x05}, // PKTLEN, PKTCTRL1/0
            {0x0A, 0x00}, {0x0B, 0x06}, {0x0C, 0x00}, // CHANNR, FSCTRL1/0
            {0x0D, 0x10}, {0x0E, 0xB0}, {0x0F, 0x71}, // 433.92 MHz
            {0x10, 0xCA}, {0x11, 0x83}, {0x12, 0x13}, // MDMCFG4/3/2
            {0x13, 0x22}, {0x14, 0xF8}, {0x15, 0x35}, // MDMCFG1/0, DEVIATN
            {0x17, 0x30}, {0x18, 0x18}, // IDLE after packet; auto-calibrate RX
            {0x19, 0x16}, {0x1A, 0x6C}, // FOCCFG, BSCFG
            {0x1B, 0x43}, {0x1C, 0x40}, {0x1D, 0x91}, // AGCCTRL2/1/0
            {0x21, 0x56}, {0x22, 0x10}, // FREND1/0
            {0x23, 0xE9}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F}, // FSCAL3..0
            {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09} // TEST2/1/0
        };

        bool identity(Report& report, uint32_t started)
        {
            return read(PARTNUM, report.part, started) && read(VERSION, report.version, started) &&
                   report.part == 0 && report.version != 0 && report.version != 0xFF;
        }

        Report inspect(uint32_t started)
        {
            Report report;
            if (!identity(report, started)) return report;
            for (const auto& setting : profile)
            {
                // Calibration updates FSCAL registers; don't compare those
                // against their pre-calibration seed values after entering RX.
                if (setting.address >= 0x23 && setting.address <= 0x26) continue;
                uint8_t value;
                if (!read(setting.address, value, started)) return report;
                if (setting.address == IOCFG0) report.iocfg0 = value;
                if (value != setting.value)
                {
                    report.result = Result::WrongConfig;
                    return report;
                }
            }
            // Two conservative samples of changing status; any nonempty FIFO
            // or non-RX sample refuses readiness, without touching its contents.
            for (unsigned sample = 0; sample < 2; ++sample)
            {
                if (!read(MARCSTATE, report.marc, started) || !read(RXBYTES, report.rxBytes, started))
                    return report;
                report.gdo = digitalRead(GDO0);
                if (report.marc == 0xFF || report.rxBytes == 0xFF) return report;
                if (report.rxBytes & 0x80) report.result = Result::RxOverflow;
                else if (report.gdo == HIGH) report.result = Result::GdoHigh;
                else if (report.rxBytes & 0x7F) report.result = Result::RxPending;
                else if ((report.marc & 0x1F) != 0x0D) report.result = Result::NotInRx;
                else continue;
                return report;
            }
            report.result = Result::Ready;
            return report;
        }
    }

    Result begin()
    {
        initialized = false;
        CC1101Bus::setupPins();
        const uint32_t started = micros();
        Report probe;
        if (!identity(probe, started)) return Result::RadioUnavailable;

        // Cold boot ONLY. No reset/flush is reachable from prepareForSleep().
        SPI.beginTransaction(settings());
        digitalWrite(CS, HIGH); delayMicroseconds(5);
        digitalWrite(CS, LOW); delayMicroseconds(10);
        digitalWrite(CS, HIGH); delayMicroseconds(40);
        digitalWrite(CS, LOW);
        bool ready = waitReady(started);
        if (ready) { SPI.transfer(SRES); ready = waitReady(started); }
        releaseBus();
        if (!ready) return Result::RadioUnavailable;

        for (const auto& setting : profile)
        {
            if (!select(started)) return Result::RadioUnavailable;
            SPI.transfer(setting.address); SPI.transfer(setting.value);
            releaseBus();
            uint8_t value;
            if (!read(setting.address, value, started)) return Result::RadioUnavailable;
            if (value != setting.value) return Result::WrongConfig;
        }
        if (!select(started)) return Result::RadioUnavailable;
        SPI.transfer(SRX);
        releaseBus();
        // One bounded calibration/RX-entry wait, not a recovery loop.
        while (withinBudget(started))
        {
            uint8_t state;
            if (!read(MARCSTATE, state, started))
                return withinBudget(started) ? Result::RadioUnavailable : Result::NotInRx;
            if (state == 0xFF) return Result::RadioUnavailable;
            if ((state & 0x1F) == 0x0D)
            {
                initialized = true;
                return Result::Ready;
            }
            delayMicroseconds(100);
        }
        return Result::NotInRx;
    }

    void attachRetained()
    {
        // Caller releases retained CS hold with the output latch HIGH first.
        // MCU-side setup only; subsequent inspection verifies radio readiness.
        CC1101Bus::setupPins();
        initialized = true;
    }

    Report prepareForSleep()
    {
        if (!initialized) return Report{};
        return inspect(micros());
    }

    const char* toString(Result result)
    {
        switch (result)
        {
            case Result::Ready: return "READY";
            case Result::RadioUnavailable: return "RADIO_UNAVAILABLE";
            case Result::WrongConfig: return "WRONG_CONFIG";
            case Result::NotInRx: return "NOT_IN_RX";
            case Result::GdoHigh: return "GDO_HIGH";
            case Result::RxPending: return "RX_PENDING";
            case Result::RxOverflow: return "RX_OVERFLOW";
        }
        return "INVALID";
    }
}
