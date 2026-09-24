#include "CC1101WakeTx.h"
#include "CC1101SleepArm.h"
#include "CC1101Bus.h"
#include <cstring>

namespace CC1101WakeTx
{
    namespace
    {
        using namespace CC1101Bus;
        constexpr uint8_t SIDLE = 0x36, SFTX = 0x3B, STX = 0x35, SRX = 0x34, SFRX = 0x3A;
        constexpr uint32_t ACK_US = 300000, TX_US = 200000;
        constexpr uint8_t MAX_RETRIES = 2;

        struct Status { uint8_t state, bytes; };
        bool inspect(Status& status, uint32_t started)
        {
            if (!read(0x35, status.state, started) || !read(0x3B, status.bytes, started) ||
                status.state == 0xFF || status.bytes == 0xFF) return false;
            status.state &= 0x1F;
            return true;
        }
        bool overflow(const Status& status) { return (status.bytes & 0x80) || status.state == 0x11; }
        bool pending(const Status& status) { return status.bytes != 0 || digitalRead(GDO0) == HIGH; }
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
                if (!read(0x35, state, started) || state == 0xFF) return false;
                if ((state & 0x1F) == target) return true;
                delayMicroseconds(100);
            }
            return false;
        }

        enum class RxResult { Ready, Pending, Failed };
        RxResult waitRx(uint32_t started)
        {
            while (withinBudget(started))
            {
                Status status;
                if (!inspect(status, started)) return RxResult::Failed;
                if (overflow(status)) return RxResult::Failed;
                if (pending(status)) return RxResult::Pending;
                if (status.state == 0x0D) return RxResult::Ready;
                delayMicroseconds(100);
            }
            return RxResult::Failed;
        }

        RxResult restoreRx(bool& recoveryUsed)
        {
            uint32_t started = micros();
            Status status;
            if (inspect(status, started))
            {
                // Never flush a complete or in-progress unrelated packet.
                if (!overflow(status) && pending(status)) return RxResult::Pending;
                if (!overflow(status) && status.bytes == 0)
                {
                    if (status.state == 0x0D) return RxResult::Ready;
                    if (status.state == 1 && strobe(SRX, started))
                    {
                        const auto rx = waitRx(started);
                        if (rx != RxResult::Failed) return rx;
                    }
                }
            }
            if (recoveryUsed) return RxResult::Failed;
            recoveryUsed = true; // One budget for the entire command, not per retry.
            Serial.println("CC1101 WAKE TX | RX_RECOVERY | attempt=1");
            started = micros();
            if (!inspect(status, started)) return RxResult::Failed;
            if (!overflow(status) && pending(status)) return RxResult::Pending;
            if (!strobe(SIDLE, started) || !waitState(1, started)) return RxResult::Failed;
            // Recheck after IDLE; a packet may have arrived during the transition.
            if (!inspect(status, started)) return RxResult::Failed;
            if (!overflow(status) && pending(status)) return RxResult::Pending;
            if (!strobe(SFRX, started) || !strobe(SRX, started)) return RxResult::Failed;
            return waitRx(started);
        }

        enum class TxResult { Sent, Busy, Failed };
        TxResult transmit(const Protocol::Message& event)
        {
            const uint32_t started = micros();
            Status status;
            if (!inspect(status, started)) return TxResult::Failed;
            if (pending(status) || status.state != 0x0D) return TxResult::Busy;
            if (!strobe(SIDLE, started) || !waitState(1, started)) return TxResult::Failed;
            if (!inspect(status, started)) return TxResult::Failed;
            if (pending(status)) return TxResult::Busy;
            // Minimum proven ece6879 / retained-wake ACK TX sequence. Keep the
            // verified boot recovery independent of this manual sender.
            if (!strobe(SFTX, started) || !select(started)) return TxResult::Failed;
            SPI.transfer(0x3E); SPI.transfer(0x60); releaseBus(); // PATABLE
            if (!select(started)) return TxResult::Failed;
            SPI.transfer(0x7F); SPI.transfer(sizeof(event));
            const auto* bytes = reinterpret_cast<const uint8_t*>(&event);
            for (size_t i = 0; i < sizeof(event); ++i) SPI.transfer(bytes[i]);
            releaseBus();
            if (!strobe(STX, started)) return TxResult::Failed;
            delayMicroseconds(1000);
            const uint32_t txStarted = micros();
            while (uint32_t(micros() - txStarted) < TX_US)
            {
                uint8_t state;
                if (!read(0x35, state, micros()) || state == 0xFF) return TxResult::Failed;
                if ((state & 0x1F) == 1) return TxResult::Sent;
                if ((state & 0x1F) == 0x16) return TxResult::Failed;
                delayMicroseconds(100);
            }
            return TxResult::Failed;
        }

        Result waitAck(const Protocol::Message& event, Protocol::DeviceId peer, bool& recoveryUsed)
        {
            const uint32_t started = micros();
            bool invalid = false;
            while (uint32_t(micros() - started) < ACK_US)
            {
                Status status;
                if (!inspect(status, micros())) return Result::RadioUnavailable;
                if (overflow(status) || (status.bytes & 0x7F) > 64)
                {
                    invalid = true;
                    if (restoreRx(recoveryUsed) != RxResult::Ready) return Result::InvalidAck;
                }
                else if (status.state == 1 && status.bytes != 0)
                {
                    // IDLE means complete with our existing MCSM1 profile.
                    // Copy before restarting RX; no blind FIFO flush.
                    uint8_t raw[64];
                    const uint32_t readStarted = micros();
                    if (!select(readStarted)) return Result::RadioUnavailable;
                    SPI.transfer(0xFF);
                    for (uint8_t i = 0; i < status.bytes; ++i) raw[i] = SPI.transfer(0);
                    releaseBus();
                    Protocol::Message ack{};
                    if (status.bytes == sizeof(ack) + 1 && raw[0] == sizeof(ack))
                    {
                        memcpy(&ack, raw + 1, sizeof(ack));
                        if (uint32_t(micros() - started) < ACK_US &&
                            ack.version == Protocol::VERSION && ack.sender == peer &&
                            ack.type == Protocol::MessageType::Ack &&
                            ack.event == Protocol::EventType::None && ack.ackForMessageId == event.messageId)
                            return Result::Acked;
                    }
                    invalid = true;
                    Serial.printf("CC1101 WAKE ACK | id=%u | REJECTED\n", event.messageId);
                    if (restoreRx(recoveryUsed) == RxResult::Failed) return Result::InvalidAck;
                }
                else if (status.state != 0x0D && restoreRx(recoveryUsed) == RxResult::Failed)
                    return Result::RadioUnavailable;
                delayMicroseconds(100);
            }
            return invalid ? Result::InvalidAck : Result::AckTimeout;
        }
    }

    Report send(const Protocol::Message& event, Protocol::DeviceId peer)
    {
        Report report;
        const auto ready = CC1101SleepArm::prepareForSleep().result;
        if (ready != CC1101SleepArm::Result::Ready)
        {
            report.result = ready == CC1101SleepArm::Result::RadioUnavailable ||
                            ready == CC1101SleepArm::Result::WrongConfig ? Result::RadioUnavailable : Result::Busy;
            return report; // Read-only refusal; preserve retained FIFO/configuration.
        }
        bool recoveryUsed = false;
        for (uint8_t attempt = 0; attempt <= MAX_RETRIES; ++attempt)
        {
            Serial.printf("CC1101 WAKE TX | id=%u | attempt=%u\n", event.messageId, attempt);
            ++report.attempts;
            const auto tx = transmit(event);
            if (tx == TxResult::Busy)
            {
                report.result = Result::Busy;
                report.rxReady = false;
                return report;
            }
            auto rx = restoreRx(recoveryUsed);
            report.rxReady = rx == RxResult::Ready;
            report.result = tx == TxResult::Sent ? Result::RadioUnavailable : Result::TxFailed;
            if (rx == RxResult::Failed) break;
            if (tx == TxResult::Sent) report.result = waitAck(event, peer, recoveryUsed);
            rx = restoreRx(recoveryUsed);
            report.rxReady = rx == RxResult::Ready;
            // ACK evidence remains valid even if the final RX restart fails.
            if (report.result == Result::Acked || rx == RxResult::Failed) break;
        }
        return report;
    }

    const char* toString(Result result)
    {
        switch (result)
        {
            case Result::Acked: return "ACKED";
            case Result::RadioUnavailable: return "RADIO_UNAVAILABLE";
            case Result::Busy: return "BUSY";
            case Result::TxFailed: return "TX_FAILED";
            case Result::AckTimeout: return "ACK_TIMEOUT";
            case Result::InvalidAck: return "INVALID_ACK";
        }
        return "INVALID";
    }
}
