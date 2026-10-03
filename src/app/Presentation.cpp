#include "Presentation.h"
#include "MotionRuntime.h"
#include "RadioRuntime.h"
#include "SleepRuntime.h"
#include "PowerManager.h"
#include "CC1101WakeRecovery.h"
#include "Display.h"
#include "LED.h"
#include <Arduino.h>
#include <cstring>

namespace Presentation
{
    using namespace AppIdentity;
    using MotionRuntime::MovementState;
    using MotionRuntime::ProximityUpdateState;
    using MotionRuntime::ProximityClassification;
    using RadioRuntime::FAR_EVENT_INTERVAL_MS;

    namespace
    {
        Display display;
        LED led;
        bool displayReady = false;
        // Presentation strings only; update this snapshot AFTER a safe redraw.
        struct DisplaySnapshot
        {
            const char *peer;
            const char *distance;
            const char *radio;
            const char *state;
            const char *motion;
        };
        DisplaySnapshot displayedStatus{};
        bool retainedUserAnimationPending = false;
        uint16_t retainedUserAnimationId = 0;
        bool haveFarBackgroundHeartbeat = false;
        uint32_t lastFarBackgroundHeartbeatAt = 0;
#ifdef DEVICE_DUDU
        // Temporary observation only; attempts include awake/final-sleep frames,
        // not initialization, and cannot confirm delivery to the physical panel.
        struct DisplayDiagnostic
        {
            bool sampled = false;
            uint32_t sampledAt = 0, attempts = 0, lastAttemptAt = 0;
        } displayDiagnostic;
#endif
    } // namespace

#ifdef DEVICE_DUDU
    void noteDisplayAttempt()
    {
        ++displayDiagnostic.attempts;
        displayDiagnostic.lastAttemptAt = uint32_t(millis());
    }
#endif

    bool backgroundHeartbeatAllowed()
    {
        return MotionRuntime::classification() != ProximityClassification::UNKNOWN &&
               PowerManager::automaticHeartbeatAllowed();
    }

    void requestBackgroundHeartbeat()
    {
        if (!backgroundHeartbeatAllowed()) return;
        if (MotionRuntime::classification() == ProximityClassification::FAR)
        {
            if (led.userHeartbeatActive()) return;
            const uint32_t now = uint32_t(millis());
            // Share one FAR cadence across outgoing and incoming background events.
            // Suppressed requests neither restart the pulse nor postpone the next one.
            if (haveFarBackgroundHeartbeat &&
                uint32_t(now - lastFarBackgroundHeartbeatAt) < FAR_EVENT_INTERVAL_MS)
                return;
            haveFarBackgroundHeartbeat = true;
            lastFarBackgroundHeartbeatAt = now;
        }
        led.requestHeartbeat();
    }

    void showDeepSleepStatus()
    {
        if (!displayReady) return;
#ifdef DEVICE_DUDU
        noteDisplayAttempt();
#endif
        display.showDeepSleep(DEVICE_NAME);
        // An aborted entry must repaint even if the awake fields are unchanged.
        displayedStatus = {};
    }

    const char *displayPeerName(PowerManager::PeerState peer)
    {
        if (peer == PowerManager::PeerState::SLEEP_PENDING || peer == PowerManager::PeerState::SLEEPING)
            return "SLEEP";
        return PowerManager::toString(peer);
    }

    const char *displayDistanceName()
    {
        if (MotionRuntime::checkState() == ProximityUpdateState::CHECKING) return "CHECKING";
        switch (MotionRuntime::classification())
        {
        case ProximityClassification::CLOSE:
            return "CLOSE";
        case ProximityClassification::FAR:
            return "FAR";
        default:
            return "UNKNOWN";
        }
    }

    const char *displayStateName(PowerManager::LocalState state)
    {
        if (state == PowerManager::LocalState::SLEEP_NEGOTIATING) return "SLEEP NEG";
        if (state == PowerManager::LocalState::SLEEPING) return "SLEEP";
        return PowerManager::toString(state);
    }

    const char *displayMotionName()
    {
        if (!MotionRuntime::ready()) return "N/A";
        switch (MotionRuntime::movement())
        {
        case MovementState::MOVING:
            return "MOVING";
        case MovementState::WAITING:
            return "SETTLING";
        default:
            return "STILL";
        }
    }

#ifdef DEVICE_DUDU
    void reportDisplayDecision(const DisplaySnapshot &current, const DisplaySnapshot &cached,
                               const char *reason)
    {
        const auto local = PowerManager::localState();
        if (local != PowerManager::LocalState::ACTIVE) return;
        const uint32_t now = uint32_t(millis());
        if (displayDiagnostic.sampled && uint32_t(now - displayDiagnostic.sampledAt) < 1000) return;
        displayDiagnostic.sampled = true;
        displayDiagnostic.sampledAt = now; // A dropped line also consumes this opportunity; never wait/retry.
        const char *classification =
            MotionRuntime::classification() == ProximityClassification::UNKNOWN ? "UNKNOWN"
            : MotionRuntime::classification() == ProximityClassification::CLOSE ? "CLOSE"
                                                                                : "FAR";
        // Tuple order: peer/distance/radio/local-state/motion. Cache is the input
        // to this decision, not evidence of a delivered or visible frame.
        char line[256];
        const int length = snprintf(
            line, sizeof(line),
            "OLED %s ms=%lu power=%s/%s prox=%s/%s want=%s/%s/%s/%s/%s cache=%s/%s/%s/%s/%s "
            "why=%s cc_stop=%u attempted=%lu last_ms=%lu\n",
            DEVICE_NAME, static_cast<unsigned long>(now), PowerManager::toString(local),
            PowerManager::toString(PowerManager::peerState()), classification,
            MotionRuntime::checkState() == ProximityUpdateState::CHECKING ? "CHECKING" : "READY",
            current.peer, current.distance, current.radio, current.state, current.motion,
            cached.peer ? cached.peer : "-", cached.distance ? cached.distance : "-",
            cached.radio ? cached.radio : "-", cached.state ? cached.state : "-",
            cached.motion ? cached.motion : "-", reason, unsigned(CC1101WakeRecovery::awakeStopped()),
            static_cast<unsigned long>(displayDiagnostic.attempts),
            static_cast<unsigned long>(displayDiagnostic.lastAttemptAt));
        // One bounded best-effort write; no flush, backlog or activity/sleep bookkeeping.
        if (length > 0 && size_t(length) < sizeof(line) && Serial.availableForWrite() >= length)
            (void)Serial.write(reinterpret_cast<const uint8_t *>(line), size_t(length));
    }
#endif

    void serviceDisplayStatus()
    {
        // No framebuffer transfer during protocol deadlines, sleep transitions
        // or an outstanding probe reply. Passive CHECKING may be shown when idle.
        // Only display permits STOPPED/busy; all other guards retain their order.
        const char *reason = !displayReady ? "DISPLAY_NOT_READY" : RadioRuntime::transportBlockedReason(true);
        if (!reason && SleepRuntime::draining()) reason = "SLEEP_DRAIN";
        if (!reason && !PowerManager::automaticHeartbeatAllowed()) reason = "POWER_NOT_AWAKE";
        if (!reason && MotionRuntime::probePending()) reason = "PROBE_PENDING";
        const DisplaySnapshot current{displayPeerName(PowerManager::peerState()), displayDistanceName(),
                                      RadioRuntime::transportName(RadioRuntime::transport()),
                                      displayStateName(PowerManager::localState()), displayMotionName()};
#ifdef DEVICE_DUDU
        const auto cached = displayedStatus;
#endif
        if (!reason)
        {
            if (displayedStatus.peer && strcmp(current.peer, displayedStatus.peer) == 0 &&
                strcmp(current.distance, displayedStatus.distance) == 0 &&
                strcmp(current.radio, displayedStatus.radio) == 0 &&
                strcmp(current.state, displayedStatus.state) == 0 &&
                strcmp(current.motion, displayedStatus.motion) == 0)
                reason = "UNCHANGED";
            else
            {
#ifdef DEVICE_DUDU
                noteDisplayAttempt();
#endif
                display.showStatus(DEVICE_NAME, current.peer, current.distance, current.radio, current.state,
                                   current.motion);
                displayedStatus = current;
                reason = "ATTEMPTED";
            }
        }
#ifdef DEVICE_DUDU
        reportDisplayDecision(current, cached, reason);
#endif
    }

    void resetBootState()
    {
        retainedUserAnimationPending = false;
        displayReady = false;
        displayedStatus = {}; // Initial redraw only when runtime is awake and drained.
#ifdef DEVICE_DUDU
        displayDiagnostic = {};
#endif
    }
    void begin()
    {
        displayReady = display.begin();
        if (!displayReady) Serial.println("DISPLAY INIT | FAILED | continuing");
#ifdef DEVICE_DUDU
        // Keep Motion's configuration result separate from these later address checks.
        MotionRuntime::reportStartupI2cHealth(display.diagnosticI2cAddress());
#endif
        led.begin(); // Retained wake recovery and shared-I2C hardware initialization are complete.
        if (retainedUserAnimationPending)
        {
            retainedUserAnimationPending = false;
            led.requestUserHeartbeat();
            Serial.printf("PARTNER LED | USER HEARTBEAT | id=%u | via=CC1101 | retained=1\n",
                          retainedUserAnimationId);
        }
    }
    void updateLed(uint32_t now)
    {
        led.update(now, backgroundHeartbeatAllowed());
    }
    void requestUserHeartbeat()
    {
        led.requestUserHeartbeat();
    }
    bool userHeartbeatActive()
    {
        return led.userHeartbeatActive();
    }
    void off()
    {
        led.off();
    }
    void deferUserHeartbeat(uint16_t messageId)
    {
        retainedUserAnimationId = messageId;
        retainedUserAnimationPending = true; // One retained packet, before its receipt ACK.
    }
    void serviceFarHeartbeat()
    {
        // Once a background EVENT starts FAR output, service its shared visual clock
        // even between packets. ACK/retry completion and an offset peer must not
        // stretch the six-second rhythm. User animation and power guards still win.
        if (MotionRuntime::classification() == ProximityClassification::FAR && haveFarBackgroundHeartbeat)
            requestBackgroundHeartbeat();
    }
} // namespace Presentation
