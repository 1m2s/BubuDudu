#include "RtcState.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_attr.h>
#endif

namespace RtcState
{
    namespace
    {
        constexpr uint32_t MAGIC = 0x42554255;
        constexpr uint8_t VERSION = 1;
        constexpr uint8_t HAVE_EVENT = 1, HAVE_REQUEST = 2;
        // Fixed-width serialized fields: never deserialize an arbitrary RTC byte
        // directly into a C++ bool. Magic is the publication/validity marker.
        struct Checkpoint
        {
            uint32_t magic;
            uint32_t checksum;
            uint16_t nextMessageId;
            uint16_t lastPeerEventId;
            uint16_t newestPeerRequest;
            uint8_t version;
            uint8_t flags;
        };
        static_assert(sizeof(Checkpoint) == 16, "RTC layout changed; review version");
#if defined(ARDUINO_ARCH_ESP32)
        RTC_DATA_ATTR
#endif
        volatile Checkpoint retained{};

        uint32_t checksum(const Checkpoint& data)
        {
            // FNV-1a over explicit bytes, not struct padding or native byte order.
            const uint8_t bytes[] = {
                data.version, data.flags,
                uint8_t(data.nextMessageId), uint8_t(data.nextMessageId >> 8),
                uint8_t(data.lastPeerEventId), uint8_t(data.lastPeerEventId >> 8),
                uint8_t(data.newestPeerRequest), uint8_t(data.newestPeerRequest >> 8)
            };
            uint32_t value = 2166136261UL;
            for (uint8_t byte : bytes) value = (value ^ byte) * 16777619UL;
            return value;
        }
    }

    void invalidate()
    {
        retained.magic = 0;
    }

    void save(const History& history)
    {
        Checkpoint data{};
        data.version = VERSION;
        data.nextMessageId = history.nextMessageId;
        data.lastPeerEventId = history.lastPeerEventId;
        data.newestPeerRequest = history.sleep.newestPeerRequest;
        data.flags = (history.haveLastPeerEvent ? HAVE_EVENT : 0) |
                     (history.sleep.havePeerRequest ? HAVE_REQUEST : 0);
        data.checksum = checksum(data);
        // Volatile stores keep invalidation first and publication last. This
        // single-owner snapshot is not a concurrent or power-loss journal.
        invalidate();
        retained.version = data.version;
        retained.flags = data.flags;
        retained.nextMessageId = data.nextMessageId;
        retained.lastPeerEventId = data.lastPeerEventId;
        retained.newestPeerRequest = data.newestPeerRequest;
        retained.checksum = data.checksum;
        retained.magic = MAGIC;
    }

    bool load(History& out)
    {
        const Checkpoint data{retained.magic, retained.checksum, retained.nextMessageId,
                              retained.lastPeerEventId, retained.newestPeerRequest,
                              retained.version, retained.flags};
        if (data.magic != MAGIC || data.version != VERSION ||
            (data.flags & ~(HAVE_EVENT | HAVE_REQUEST)) || data.checksum != checksum(data))
            return false;
        out = {data.nextMessageId, bool(data.flags & HAVE_EVENT), data.lastPeerEventId,
               {bool(data.flags & HAVE_REQUEST), data.newestPeerRequest}};
        return true;
    }
}
