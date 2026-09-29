#include "motion/Wire.h"
#include "../../src/Motion.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
HostWire Wire;
namespace MotionPlatform { bool isrAttached = false, stuckHigh = false; void (*isr)() = nullptr; }

void resetHardware()
{
    Wire = HostWire{};
    hostNow = 0;
    Serial.log.clear();
    MotionPlatform::stuckHigh = false;
    MotionPlatform::isrAttached = false;
    MotionPlatform::isr = nullptr;
    MotionPlatform::interruptCalls() = {};
    Wire.registers[0] = 0xE5;
    Wire.registers[0x2C] = 0x0A;
}
void fresh(Motion& motion)
{
    motion.pauseInterrupt(); // Reused driver must release its handler before the simulated hardware reset.
    resetHardware();
    assert(motion.begin(0, 1, 3));
    assert(MotionPlatform::isrAttached);
    assert(Wire.registers[0x24] == 12); // Tuned awake threshold, independent of sleep.
    assert(Wire.registers[0x25] == 4 && Wire.registers[0x26] == 3);
    assert(Wire.registers[0x27] == 0xFF && Wire.registers[0x31] == 0x09);
    assert(Wire.registers[0x2C] == 0x0A); // No data-rate change.
    Wire.operations = 0; Wire.writes.clear();
}
void assertAwake()
{
    assert(MotionPlatform::isrAttached);
    assert(Wire.registers[0x2D] == 0x28 && Wire.registers[0x2E] == 0x18 && Wire.registers[0x2F] == 0);
    assert(Wire.registers[0x24] == 12);
}

void testStartupRetries()
{
    Motion normal;
    resetHardware(); Wire.registers[0x30] = 0x18;
    assert(normal.begin(0, 1, 3)); assertAwake();
    assert(MotionPlatform::interruptCalls().attaches == 1 && MotionPlatform::interruptCalls().detaches == 0);
    assert(Wire.begins == 1 && Wire.identifications == 1 && hostNow == Wire.operations);
    assert(Serial.log.empty() && normal.getStartupEvent() == MotionEvent::Activity);
    const auto normalRegisters = Wire.registers;
    const auto normalWrites = Wire.writes;
    const auto normalOperations = Wire.operations;

    // A transient failure at ANY real I2C operation must cause a complete retry,
    // including re-identification and all nine configuration writes.
    for (unsigned failed = 1; failed <= normalOperations; ++failed)
    {
        Motion motion; resetHardware(); Wire.registers[0x30] = 0x18; Wire.failAt = failed;
        assert(motion.begin(0, 1, 3)); assertAwake();
        assert(MotionPlatform::interruptCalls().attaches == 1 && MotionPlatform::interruptCalls().detaches == 0);
        assert(Wire.begins == 1 && Wire.identifications == 2);
        assert(hostNow == Wire.operations + 20 && Serial.log == "MOTION INIT | retry 1/2\n");
        assert(Wire.registers == normalRegisters && motion.getStartupEvent() == MotionEvent::Activity);
        assert(Wire.writes.size() >= normalWrites.size());
        for (size_t i = 0; i < normalWrites.size(); ++i)
            assert(Wire.writes[Wire.writes.size() - normalWrites.size() + i] == normalWrites[i]);
        assert(motion.prepareForSleep());
        assert(Wire.registers[0x24] == 48 && Wire.registers[0x2D] == 8 && Wire.registers[0x2E] == 0x10);
        assert(motion.cancelSleepPreparation()); assertAwake();
        assert(MotionPlatform::interruptCalls().attaches == 2 && MotionPlatform::interruptCalls().detaches == 1);
        const auto operations = Wire.operations, begins = Wire.begins;
        const auto log = Serial.log;
        for (unsigned i = 0; i < 100; ++i) assert(motion.getEvent() == MotionEvent::None);
        assert(Wire.operations == operations && Wire.begins == begins && Serial.log == log);
    }
    for (unsigned failures : {1U, 2U})
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        Motion motion; resetHardware(); Wire.registers[0x30] = 0x08;
        if (fault == 0) Wire.beginFailures = failures;
        if (fault == 1) Wire.wrongDeviceReads = failures;
        if (fault == 2) Wire.wrongThresholdReads = failures;
        assert(motion.begin(0, 1, 3)); assertAwake();
        assert(MotionPlatform::interruptCalls().attaches == 1 && MotionPlatform::interruptCalls().detaches == 0);
        assert(hostNow == Wire.operations + failures * 20);
        assert(Wire.begins == (fault == 0 ? failures + 1 : 1));
        assert(Wire.identifications == (fault == 0 ? 1 : failures + 1));
        assert(motion.getStartupEvent() == MotionEvent::Inactivity);
        assert(Serial.log == (failures == 1 ? "MOTION INIT | retry 1/2\n" :
                             "MOTION INIT | retry 1/2\nMOTION INIT | retry 2/2\n"));
    }
    for (unsigned fault = 0; fault < 4; ++fault)
    {
        Motion motion; resetHardware();
        if (fault == 0) Wire.beginFailures = 100;
        if (fault == 1) Wire.registers[0] = 0; // Absent/wrong device.
        if (fault == 2) Wire.failAll = true;
        if (fault == 3) Wire.corruptRegister = 0x24;
        assert(!motion.begin(0, 1, 3) && !MotionPlatform::isrAttached);
        assert(MotionPlatform::interruptCalls().attaches == 0 && MotionPlatform::interruptCalls().detaches == 0);
        assert(Wire.begins == (fault == 0 ? 3U : 1U));
        assert(Wire.identifications == (fault == 0 ? 0U : 3U));
        assert(hostNow == Wire.operations + 40 && Wire.operations <= 3 * normalOperations);
        assert(Serial.log == "MOTION INIT | retry 1/2\nMOTION INIT | retry 2/2\n");
        const auto operations = Wire.operations, begins = Wire.begins, now = hostNow;
        const auto log = Serial.log;
        for (unsigned i = 0; i < 100; ++i)
        {
            assert(motion.getEvent() == MotionEvent::None);
            assert(!motion.prepareForSleep() && !motion.cancelSleepPreparation());
        }
        assert(Wire.operations == operations && Wire.begins == begins && hostNow == now && Serial.log == log);
    }
    puts("PASS: Motion first-attempt success has no retry wait; every transient I2C operation, DEVID/readback and bus-start failure retries coherently, preserving latched wake cause and awake/sleep configuration");
    puts("PASS: three total startup attempts, 20ms between failures, maximum 40ms retry wait; permanent failure stays unavailable without ISR or background retries");
}

void testInterruptLifecycle()
{
    Motion motion; resetHardware();
    auto& calls = MotionPlatform::interruptCalls();
    motion.pauseInterrupt(); motion.resumeInterrupt();
    assert(calls.attaches == 0 && calls.detaches == 0 && !MotionPlatform::isrAttached);
    assert(motion.begin(0, 1, 3));
    assert(calls.attaches == 1 && calls.detaches == 0 && MotionPlatform::isrAttached);
    const auto handler = MotionPlatform::isr;
    motion.resumeInterrupt(); motion.resumeInterrupt();
    assert(calls.attaches == 1 && MotionPlatform::isr == handler);
    motion.pauseInterrupt(); motion.pauseInterrupt();
    assert(calls.detaches == 1 && !MotionPlatform::isrAttached && MotionPlatform::isr == nullptr);
    motion.resumeInterrupt(); motion.resumeInterrupt();
    assert(calls.attaches == 2 && MotionPlatform::isrAttached && MotionPlatform::isr == handler);

    // Repeated begin detaches the previous handler once; retries never attach.
    Wire.wrongDeviceReads = 1;
    assert(motion.begin(0, 1, 3));
    assert(calls.attaches == 3 && calls.detaches == 2 && MotionPlatform::isr == handler);
    Wire.registers[0] = 0;
    assert(!motion.begin(0, 1, 3));
    assert(calls.attaches == 3 && calls.detaches == 3 && !MotionPlatform::isrAttached);
    assert(!motion.begin(0, 1, 3));
    motion.pauseInterrupt(); motion.resumeInterrupt();
    assert(calls.attaches == 3 && calls.detaches == 3 && !MotionPlatform::isrAttached);
    Wire.registers[0] = 0xE5;
    assert(motion.begin(0, 1, 3));
    assert(calls.attaches == 4 && calls.detaches == 3 && MotionPlatform::isr == handler);
    assert(motion.prepareForSleep());
    assert(calls.detaches == 4 && !MotionPlatform::isrAttached);
    assert(motion.cancelSleepPreparation()); assertAwake();
    assert(calls.attaches == 5 && MotionPlatform::isr == handler);
    assert(motion.cancelSleepPreparation()); // Repeated restore cannot attach twice.
    assert(calls.attaches == 5 && calls.detaches == 4);
    motion.pauseInterrupt();
    puts("PASS: Motion never detaches an unowned ISR; startup/retries attach only once on success, repeated pause/resume/begin and sleep restoration preserve attachment ownership");
}

void testAwakeEvents()
{
    Motion motion;
    fresh(motion);
    assert(motion.getEvent() == MotionEvent::None && Wire.operations == 0);
    for (unsigned cycle = 0; cycle < 4; ++cycle)
    {
        for (uint8_t source : {uint8_t(0x10), uint8_t(0x08), uint8_t(0x18)})
        {
            Wire.registers[0x30] = source;
            assert(MotionPlatform::isrAttached && MotionPlatform::isr);
            MotionPlatform::isr(); // Exercise the real driver's callback.
            assert(motion.getEvent() == (source & 0x10 ? MotionEvent::Activity : MotionEvent::Inactivity));
            assert(Wire.registers[0x30] == 0 && digitalRead(3) == LOW);
            const auto operations = Wire.operations;
            assert(motion.getEvent() == MotionEvent::None && Wire.operations == operations);
        }
    }
    MotionPlatform::isr(); // Spurious edge is not activity.
    assert(motion.getEvent() == MotionEvent::None);

    // A new activity latch during sleep preparation is already HIGH at reattach.
    assert(motion.prepareForSleep());
    Wire.registers[0x30] = 0x10;
    assert(!MotionPlatform::isrAttached && digitalRead(3) != LOW);
    assert(motion.cancelSleepPreparation()); assertAwake();
    // No ISR callback invoked: level fallback must consume the retained event.
    assert(motion.getEvent() == MotionEvent::Activity && digitalRead(3) == LOW);
    assert(motion.getEvent() == MotionEvent::None);

    for (unsigned failure : {1U, 2U})
    {
        Wire.registers[0x30] = 0x10;
        MotionPlatform::isr();
        Wire.operations = 0; Wire.failAt = failure;
        assert(motion.getEvent() == MotionEvent::None); // I2C error must not become MOVING.
        assert(Wire.operations == failure && digitalRead(3) != LOW);
        Wire.failAt = 0;
        assert(motion.getEvent() == MotionEvent::Activity); // Later call, no new edge needed.
        assert(motion.getEvent() == MotionEvent::None);
    }

    Wire = HostWire{}; MotionPlatform::stuckHigh = true;
    assert(!motion.begin(0, 1, 3));
    Wire.operations = 0;
    assert(motion.getEvent() == MotionEvent::None && Wire.operations == 0);
    Motion uninitialized;
    assert(uninitialized.getEvent() == MotionEvent::None && Wire.operations == 0);

    // An ACKed but incorrect awake threshold cannot establish initialization.
    Wire.registers[0] = 0xE5; Wire.corruptRegister = 0x24;
    assert(!motion.begin(0, 1, 3));
    Wire.operations = 0;
    assert(motion.getEvent() == MotionEvent::None && Wire.operations == 0);
    puts("PASS: awake ISR/level event consumption, repeated LINK events, already-HIGH restore, I2C/initialization isolation");
}

int main()
{
    testInterruptLifecycle();
    testStartupRetries();
    resetHardware();
    Motion motion;
    assert(!motion.prepareForSleep()); // Not initialized: no bus access.
    assert(Wire.operations == 0);
    fresh(motion);
    Wire.registers[0x30] = 0x18; // Prior latched activity/inactivity on HIGH INT1.
    assert(digitalRead(3) != LOW);
    assert(motion.prepareForSleep());
    const auto preparationOperations = Wire.operations;
    assert(preparationOperations < 20 && !MotionPlatform::isrAttached && digitalRead(3) == LOW);
    assert(Wire.registers[0x2D] == 8 && Wire.registers[0x2E] == 0x10 && Wire.registers[0x2F] == 0);
    assert(Wire.registers[0x24] == 48 && Wire.registers[0x25] == 4 && Wire.registers[0x26] == 3);
    assert(Wire.registers[0x27] == 0xFF && Wire.registers[0x31] == 0x09);
    assert(Wire.writes[0] == std::make_pair(uint8_t(0x2E), uint8_t(0)));
    assert(Wire.writes[1] == std::make_pair(uint8_t(0x2D), uint8_t(0))); // Standby before LINK cleared.
    assert(Wire.writes[2] == std::make_pair(uint8_t(0x24), uint8_t(48)));
    Wire.registers[0x30] = 0x08; assert(digitalRead(3) == LOW); // Inactivity cannot wake INT1.
    Wire.registers[0x30] = 0x10; assert(digitalRead(3) != LOW); // Activity remains latched.
    assert(motion.cancelSleepPreparation()); assertAwake();
    for (uint8_t prior : {uint8_t(1), uint8_t(12), uint8_t(47)})
    {
        fresh(motion); Wire.registers[0x24] = prior;
        assert(motion.prepareForSleep() && Wire.registers[0x24] == 48);
        assert(motion.cancelSleepPreparation()); assertAwake();
    }

    fresh(motion); MotionPlatform::stuckHigh = true;
    assert(!motion.prepareForSleep()); // Clearing source cannot fix externally held HIGH.
    assert(motion.cancelSleepPreparation()); assertAwake();
    for (unsigned failure = 1; failure <= preparationOperations; ++failure)
    {
        fresh(motion); Wire.failAt = failure;
        assert(!motion.prepareForSleep() && Wire.operations <= preparationOperations);
        Wire.failAt = 0;
        assert(motion.cancelSleepPreparation()); assertAwake();
    }
    for (int reg : {0x24, 0x2D, 0x2E, 0x2F})
    {
        fresh(motion); Wire.corruptRegister = reg;
        assert(!motion.prepareForSleep()); // ACKed writes are still verified by readback.
        Wire.corruptRegister = -1;
        assert(motion.cancelSleepPreparation()); assertAwake();
    }
    fresh(motion); Wire.registers[0x31] = 0x29; // Wrong interrupt polarity.
    assert(!motion.prepareForSleep());
    fresh(motion); assert(motion.prepareForSleep());
    Wire.operations = 0; Wire.failAt = 1;
    assert(!motion.cancelSleepPreparation() && MotionPlatform::isrAttached);
    assert(Wire.operations < 20);
    fresh(motion); assert(motion.prepareForSleep());
    Wire.operations = 0;
    assert(motion.cancelSleepPreparation());
    const auto restorationOperations = Wire.operations;
    assert(restorationOperations < 20);
    for (unsigned failure = 1; failure <= restorationOperations; ++failure)
    {
        fresh(motion); assert(motion.prepareForSleep());
        Wire.operations = 0; Wire.failAt = failure;
        assert(!motion.cancelSleepPreparation() && MotionPlatform::isrAttached);
        assert(Wire.operations <= restorationOperations); // Still bounded; no retry loop.
        Wire.failAt = 0;
        assert(motion.cancelSleepPreparation()); assertAwake();
    }
    fresh(motion); assert(motion.prepareForSleep());
    Wire.corruptRegister = 0x24;
    assert(!motion.cancelSleepPreparation() && MotionPlatform::isrAttached);
    Wire.corruptRegister = -1;
    assert(motion.cancelSleepPreparation()); assertAwake();
    Wire = HostWire{};
    assert(!motion.begin(0, 1, 3)); // DEVID failure prevents sleep preparation.
    Wire.operations = 0;
    assert(!motion.prepareForSleep() && Wire.operations == 0);
    puts("PASS: Motion awake=12/sleep=48/restore=12, activity-only arm, latched-source clearing, stuck HIGH, standby transition");
    puts("PASS: every preparation I2C failure, readback/polarity faults, abort restore, uninitialized/failed sensor bounded");
    testAwakeEvents();
}
