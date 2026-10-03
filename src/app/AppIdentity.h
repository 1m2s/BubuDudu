#pragma once
#include "Config.h"
#include "Protocol.h"

namespace AppIdentity
{
#ifdef DEVICE_BUBU
    constexpr Protocol::DeviceId LOCAL_DEVICE = Protocol::DeviceId::Bubu;
    constexpr Protocol::DeviceId PEER_DEVICE = Protocol::DeviceId::Dudu;
#else
    constexpr Protocol::DeviceId LOCAL_DEVICE = Protocol::DeviceId::Dudu;
    constexpr Protocol::DeviceId PEER_DEVICE = Protocol::DeviceId::Bubu;
#endif
    inline const char *deviceName(Protocol::DeviceId device)
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
} // namespace AppIdentity
