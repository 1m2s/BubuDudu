#pragma once

#include "AppIdentity.h"
#include "CC1101WakeTx.h"
namespace RadioRuntime
{
    enum class Transport : uint8_t
    {
        ESP_NOW,
        CC1101
    };
    constexpr unsigned long EVENT_INTERVAL_MS = 2500;
    constexpr unsigned long FAR_EVENT_INTERVAL_MS = 6000;
    void resetBootState();
    bool begin();
    bool ready();
    uint16_t allocateMessageId();
    Transport transport();
    bool fallbackPending();
    const char *transportName(Transport transport);
    void classificationCompleted(uint32_t now, bool first);
    bool queueSleepControl(Protocol::MessageType type, uint16_t sleepId);
    void drainReceiveQueue();
    void serviceAwake();
    void discardObsoleteControls();
    void handleAckTimeout();
    void sendNextControl();
    void serviceAutomaticTransportSelection();
    void servicePeriodicHeartbeat();
    void startHeartbeatEvent(Transport transport, Protocol::EventType event = Protocol::EventType::Heartbeat);
    void startHeartbeatEvent();
    const char *transportBlockedReason(bool allowStoppedCc1101);
    const char *sleepTransportBlockedReason();
    CC1101WakeTx::Report requestPeerWake();
    void serviceDeferredWakeEvents();
    void saveRtcHistory();
    bool restoreRtcHistory();
    Protocol::Message handleWakeEvent(const Protocol::Message &event, bool &processed);
} // namespace RadioRuntime
