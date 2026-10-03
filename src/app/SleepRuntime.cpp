#include "SleepRuntime.h"
#include "ButtonRuntime.h"
#include "MotionRuntime.h"
#include "Presentation.h"
#include "RadioRuntime.h"
#include "PowerManager.h"
#include "RtcState.h"
#include "CC1101SleepArm.h"
#include <Arduino.h>

namespace SleepRuntime
{
    using namespace AppIdentity;
    using MotionRuntime::MovementState;
    using MotionRuntime::ProximityUpdateState;
    using MotionRuntime::ProximityClassification;

    namespace
    {
        CC1101WakeRecovery::BootInfo bootInfo;
        CC1101WakeRecovery::Report wakeReport;
        bool rtcRestored = false;
        bool sleepDrainWaiting = false;
        uint32_t sleepDrainStarted = 0;
        constexpr uint32_t SLEEP_DRAIN_TIMEOUT_MS = 3000;
        uint32_t lastMeaningfulActivity = 0;
        bool automaticSleepArmed = false;
    } // namespace

    void noteLocalActivity(uint32_t now)
    {
        lastMeaningfulActivity = now;
        automaticSleepArmed = true;
        PowerManager::injectActivity(now);
    }

    const char *sleepEntryBlockedReason()
    {
        if (const char *reason = ButtonRuntime::buttonSleepBlockedReason()) return reason;
        if (const char *reason = RadioRuntime::sleepTransportBlockedReason()) return reason;
        if (digitalRead(MOTION_INT1_PIN) != 0) return "MOTION_INT1_HIGH";
        return nullptr;
    }

    bool productSleepEligible(uint32_t now, uint32_t requiredInactivityMs)
    {
        return uint32_t(now - lastMeaningfulActivity) >= requiredInactivityMs &&
               ButtonRuntime::buttonSleepBlockedReason() == nullptr && MotionRuntime::ready() &&
               MotionRuntime::movement() == MovementState::READY &&
               MotionRuntime::checkState() != ProximityUpdateState::CHECKING &&
               PowerManager::automaticHeartbeatAllowed() && PowerManager::cooldownLeftMs(now) == 0 &&
               RadioRuntime::sleepTransportBlockedReason() == nullptr;
    }

    void serviceAutomaticSleep()
    {
        const uint32_t now = uint32_t(millis());
        if (!automaticSleepArmed || !productSleepEligible(now)) return;
        if (PowerManager::requestSleep(RadioRuntime::allocateMessageId(), now)) automaticSleepArmed = false;
    }

    void enterPhysicalSleep()
    {
        Presentation::off();            // Sleep wins immediately; animation is never a drain condition.
        MotionRuntime::resetMovement(); // An aborted attempt must not retain an old settle timer.
        MotionRuntime::cancelProximityCheck("SLEEP");
        if (MotionRuntime::prepareForSleep())
        {
            Serial.println(
                "MOTION SLEEP ARM | READY | GPIO3 LOW | activity only | INT_ENABLE=0x10 POWER_CTL=0x08");
            CC1101WakeRecovery::enterDeepSleep(RadioRuntime::saveRtcHistory, sleepEntryBlockedReason,
                                               Presentation::showDeepSleepStatus);
        }
        else
        {
            RtcState::invalidate();
            Serial.println("COORDINATED DEEP SLEEP | ABORTED | reason=MOTION_NOT_READY_OR_INT1_HIGH");
        }
        // Successful deep sleep reboots. Every return restores awake sensing.
        Serial.printf("MOTION SLEEP ARM | CANCELLED | awake_restore=%s\n",
                      MotionRuntime::cancelSleepPreparation() ? "OK" : "FAILED");
    }

    void serviceSleepExecution()
    {
        if (PowerManager::localState() != PowerManager::LocalState::SLEEPING)
        {
            sleepDrainWaiting = false;
            return;
        }
        if (!sleepDrainWaiting)
        {
            sleepDrainWaiting = true;
            sleepDrainStarted = millis();
        }
        if (const char *reason = RadioRuntime::sleepTransportBlockedReason())
        {
            if (uint32_t(millis() - sleepDrainStarted) >= SLEEP_DRAIN_TIMEOUT_MS)
            {
                Serial.printf("COORDINATED DEEP SLEEP | ABORTED | reason=DRAIN_TIMEOUT/%s\n", reason);
                PowerManager::notifySleepExecutionFailed(millis());
                sleepDrainWaiting = false;
            }
            return;
        }
        // Catch Activity arriving while this iteration processed radio work.
        // Existing activity semantics revoke an unconsumed SLEEPING decision.
        MotionRuntime::serviceMotion();
        ButtonRuntime::serviceButton();
        if (PowerManager::localState() != PowerManager::LocalState::SLEEPING)
        {
            sleepDrainWaiting = false;
            return;
        }
        if (ButtonRuntime::buttonSleepBlockedReason())
            return; // Keep raw LOW/pending intent ahead of decision consumption.
        PowerManager::SleepDecision decision{};
        if (!PowerManager::takeSleepDecision(decision)) return;
        Serial.printf("SLEEP EXECUTION READY | sleepId=%u | role=%s\n", decision.sleepId,
                      PowerManager::toString(decision.role));
        Serial.println("SLEEP TRANSPORT DRAINED");
        enterPhysicalSleep();
        // Successful deep sleep reboots. A return always means entry failed.
        PowerManager::notifySleepExecutionFailed(millis());
        sleepDrainWaiting = false;
    }

    void captureBoot()
    {
        bootInfo = CC1101WakeRecovery::captureBoot();
    }
    const CC1101WakeRecovery::BootInfo &boot()
    {
        return bootInfo;
    }
    bool draining()
    {
        return sleepDrainWaiting;
    }
    void consumeAutomaticOpportunity()
    {
        automaticSleepArmed = false;
    }
    void resetBootState()
    {
        automaticSleepArmed = false;
        lastMeaningfulActivity = 0;
    }
    void beginWake()
    {
        if (bootInfo.wokeFromGpio(BUTTON_PIN))
            Serial.printf("BUTTON WAKE | DETECTED | GPIO5 | captured mask=0x%02llX\n",
                          static_cast<unsigned long long>(bootInfo.gpioMask));
        PowerManager::begin(LOCAL_DEVICE, RadioRuntime::queueSleepControl);
        if (bootInfo.deep)
        {
            // No USB delay, normal CC1101 begin, reset or configuration here.
            rtcRestored = RadioRuntime::restoreRtcHistory();
            if (!rtcRestored) RtcState::invalidate();
            // GPIO mask identifies the wake SOURCE, not whether FIFO data exists.
            // Inspect retained RX even for motion/timer (empty is normal); never
            // reset away a coincident packet. All GPIO wake inputs use this same path.
            wakeReport = CC1101WakeRecovery::recover(rtcRestored, PEER_DEVICE, RadioRuntime::handleWakeEvent);
            CC1101WakeRecovery::printReport(bootInfo, rtcRestored, wakeReport);
        }
        else
        {
            RtcState::invalidate();
            delay(1500);
            Serial.println("BOOT | COLD");
            const auto armInit = CC1101SleepArm::begin();
            Serial.printf("CC1101 ARM INIT | %s | CPU stays awake\n", CC1101SleepArm::toString(armInit));
        }
    }
    void finishStartup()
    {
        // One-shot boot policy, after retained recovery, Motion and runtime setup.
        // GPIO4 is CC1101 GDO0: its participation suppresses a return wake, even
        // when GPIO3 also fired. Never re-evaluate this from loop() or rearm on failure.
        if (!ButtonRuntime::wakeIntentHeld() && bootInfo.wokeFromGpio(MOTION_INT1_PIN) &&
            !bootInfo.wokeFromGpio(4))
        {
            Serial.println("MOTION PEER WAKE | one-shot request");
            RadioRuntime::requestPeerWake();
        }

        // One bootstrap attempt per runtime startup, even before peer ONLINE.
        // UNKNOWN suppresses heartbeats; this bounded check supplies its own probes.
        if (!ButtonRuntime::pending() && MotionRuntime::classification() == ProximityClassification::UNKNOWN)
            MotionRuntime::startProximityCheck(
                uint32_t(millis()));                 // Existing ACTIVE/overlap guards; no loop-based restart.
        lastMeaningfulActivity = uint32_t(millis()); // Fresh runtime, including a completely stationary boot.
        automaticSleepArmed = true;
    }
} // namespace SleepRuntime
