#include "motion/Wire.h"
#include "../../src/Motion.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
HostWire Wire;
namespace MotionPlatform { bool isrAttached = false, stuckHigh = false; }

void fresh(Motion& motion)
{
    Wire = HostWire{};
    MotionPlatform::stuckHigh = false;
    MotionPlatform::isrAttached = false;
    Wire.registers[0] = 0xE5;
    assert(motion.begin(0, 1, 3));
    assert(MotionPlatform::isrAttached);
    Wire.operations = 0; Wire.writes.clear();
}
void assertAwake()
{
    assert(MotionPlatform::isrAttached);
    assert(Wire.registers[0x2D] == 0x28 && Wire.registers[0x2E] == 0x18 && Wire.registers[0x2F] == 0);
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
    Wire.registers[0x30] = 0x08; assert(digitalRead(3) == LOW); // Inactivity cannot wake INT1.
    Wire.registers[0x30] = 0x10; assert(digitalRead(3) != LOW); // Activity remains latched.
    assert(motion.cancelSleepPreparation()); assertAwake();

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
    for (int reg : {0x2D, 0x2E, 0x2F})
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
    Wire = HostWire{};
    assert(!motion.begin(0, 1, 3)); // DEVID failure prevents sleep preparation.
    Wire.operations = 0;
    assert(!motion.prepareForSleep() && Wire.operations == 0);
    puts("PASS: Motion activity-only arm, latched-source clearing, stuck HIGH, thresholds unchanged, standby transition");
    puts("PASS: every preparation I2C failure, readback/polarity faults, abort restore, uninitialized/failed sensor bounded");
}
