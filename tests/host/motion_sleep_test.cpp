#include "motion/Wire.h"
#include "../../src/Motion.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
HostWire Wire;
namespace MotionPlatform { bool isrAttached = false, stuckHigh = false; void (*isr)() = nullptr; }

void fresh(Motion& motion)
{
    Wire = HostWire{};
    MotionPlatform::stuckHigh = false;
    MotionPlatform::isrAttached = false;
    MotionPlatform::isr = nullptr;
    Wire.registers[0] = 0xE5;
    Wire.registers[0x2C] = 0x0A;
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
