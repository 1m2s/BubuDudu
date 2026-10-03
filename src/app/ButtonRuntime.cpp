#include "ButtonRuntime.h"
#include "MotionRuntime.h"
#include "Presentation.h"
#include "RadioRuntime.h"
#include "SleepRuntime.h"
#include "PowerManager.h"
#include "CC1101WakeRecovery.h"
#include <Arduino.h>

namespace ButtonRuntime
{
    using namespace AppIdentity;
    using RadioRuntime::Transport;
    using MotionRuntime::ProximityClassification;

    namespace
    {
        constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
        bool buttonRawPressed = false;
        bool buttonStablePressed = false;
        uint32_t buttonChangedAt = 0;
        bool buttonHeartbeatPending = false;
        bool buttonWakeIntentHeld = false;
        bool buttonWakeRetryOnPress = false;
        uint32_t buttonWakeHeldAt = 0;
        constexpr uint32_t BUTTON_WAKE_DRAIN_TIMEOUT_MS = 3000;
        const char *lastButtonSleepBlock = nullptr;
    } // namespace

    void failButtonWake(const char *reason)
    {
        if (!buttonWakeIntentHeld) return;
        buttonWakeIntentHeld = buttonHeartbeatPending = false;
        buttonWakeRetryOnPress = true;
        Serial.printf("BUTTON WAKE | GIVE_UP | reason=%s | intent cleared | delivery=UNCONFIRMED | new "
                      "debounced press required\n",
                      reason);
    }

    void beginButton()
    {
        pinMode(BUTTON_PIN, INPUT_PULLUP);
        buttonRawPressed = digitalRead(BUTTON_PIN) == LOW;
        buttonWakeIntentHeld = SleepRuntime::boot().wokeFromGpio(BUTTON_PIN);
        // A latched wake is already one press, even if released during startup.
        // Seed the pressed state so only a stable HIGH can rearm an awake edge.
        buttonStablePressed = buttonWakeIntentHeld;
        buttonChangedAt = uint32_t(millis());
        buttonWakeHeldAt = buttonChangedAt;
        buttonWakeRetryOnPress = false;
        buttonHeartbeatPending = buttonWakeIntentHeld;
        lastButtonSleepBlock = nullptr;
        if (buttonWakeIntentHeld)
        {
            Serial.println(
                "BUTTON WAKE | GPIO5 | one UserHeartbeat intent preserved | HELD_FOR_PEER_WAKE_HANDOFF");
            SleepRuntime::noteLocalActivity(buttonChangedAt);
        }
    }

    void serviceButton()
    {
        const uint32_t now = uint32_t(millis());
        const bool pressed = digitalRead(BUTTON_PIN) == LOW;
        if (pressed != buttonRawPressed)
        {
            buttonRawPressed = pressed;
            buttonChangedAt = now;
        }
        if (buttonRawPressed == buttonStablePressed || uint32_t(now - buttonChangedAt) < BUTTON_DEBOUNCE_MS)
            return;
        buttonStablePressed = buttonRawPressed;
        if (!buttonStablePressed)
        {
            Serial.println("BUTTON | RELEASED | stable 30ms | rearmed");
            return; // Release does not clear the held wake intent or create an EVENT.
        }
        Serial.println("BUTTON | PRESSED");
        SleepRuntime::noteLocalActivity(now);
        if (!buttonHeartbeatPending)
        {
            buttonHeartbeatPending = true;
            if (buttonWakeRetryOnPress)
            {
                buttonWakeIntentHeld = true;
                buttonWakeHeldAt = now;
                Serial.println("BUTTON WAKE | RETRY_ARMED | new debounced press");
            }
            Serial.println("BUTTON HEARTBEAT | PENDING");
        }
    }

    const char *buttonSleepBlockedReason()
    {
        // A held LOW would immediately wake again. Poll at admission/final guards;
        // a complete pulse between polls or in SDK sleep entry can still be missed.
        const char *reason = digitalRead(BUTTON_PIN) == LOW ? "BUTTON_LOW"
                             : buttonWakeIntentHeld         ? "BUTTON_WAKE_INTENT_HELD"
                             : buttonHeartbeatPending       ? "BUTTON_HEARTBEAT_PENDING"
                                                            : nullptr;
        if (reason != lastButtonSleepBlock)
        {
            lastButtonSleepBlock = reason;
            if (reason) Serial.printf("BUTTON SLEEP | BLOCKED | reason=%s\n", reason);
        }
        return reason;
    }

    void serviceButtonHeartbeat()
    {
        if (!buttonHeartbeatPending) return;
        // Intent preempts measurement, but never an in-flight radio packet or ACK.
        MotionRuntime::cancelProximityCheck("BUTTON_HEARTBEAT");
        if (buttonWakeIntentHeld)
        {
            if (uint32_t(millis() - buttonWakeHeldAt) >= BUTTON_WAKE_DRAIN_TIMEOUT_MS)
            {
                failButtonWake("DRAIN_TIMEOUT");
                return;
            }
            if (CC1101WakeRecovery::awakeStopped())
            {
                failButtonWake("CC1101_RUNTIME_STOPPED");
                return;
            }
            // Drain coincident traffic first; don't skip a received user pulse
            // while the existing synchronous wake sender waits for its ACK.
            if (RadioRuntime::sleepTransportBlockedReason() || SleepRuntime::draining() ||
                !PowerManager::automaticHeartbeatAllowed() || Presentation::userHeartbeatActive())
                return;
            Serial.println("BUTTON WAKE | PEER_WAKE | one bounded episode");
            const auto result = RadioRuntime::requestPeerWake();
            const bool peerAcked = result.result == CC1101WakeTx::Result::Acked;
            Serial.printf("BUTTON WAKE | RESULT | peer_ack=%d | RX_READY=%d | attempts=%u\n", peerAcked,
                          result.rxReady, result.attempts);
            if (!peerAcked || !result.rxReady)
            {
                failButtonWake(peerAcked ? "LOCAL_RX_NOT_READY" : CC1101WakeTx::toString(result.result));
                return;
            }
            buttonWakeIntentHeld = buttonWakeRetryOnPress = false;
            Serial.println("BUTTON WAKE | HANDOFF | UserHeartbeat pending | delivery awaits application ACK");
        }
        if (RadioRuntime::sleepTransportBlockedReason() || SleepRuntime::draining() ||
            !PowerManager::automaticHeartbeatAllowed())
            return;
        RadioRuntime::serviceAutomaticTransportSelection();
        // UNKNOWN uses the long-range awake radio without asserting CLOSE/FAR.
        // submitAwake/serviceAwake retain ownership of preflight and RX recovery.
        const Transport transport = MotionRuntime::classification() == ProximityClassification::UNKNOWN
                                        ? Transport::CC1101
                                        : RadioRuntime::transport();
        buttonHeartbeatPending = false;
        Serial.printf(
            "BUTTON HEARTBEAT | SEND | via=%s | proximity=%s\n", RadioRuntime::transportName(transport),
            MotionRuntime::classification() == ProximityClassification::UNKNOWN ? "UNKNOWN" : "KNOWN");
        RadioRuntime::startHeartbeatEvent(transport, Protocol::EventType::UserHeartbeat);
    }

    bool pending()
    {
        return buttonHeartbeatPending;
    }
    bool wakeIntentHeld()
    {
        return buttonWakeIntentHeld;
    }
} // namespace ButtonRuntime
