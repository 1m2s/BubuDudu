// Real Motion/Display startup and diagnostic, with only Wire/U8g2/GPIO replaced.
#include "motion/Wire.h"
#include "Config.h"
#include "../../src/Motion.cpp"
#include "../../src/Display.cpp"

uint32_t hostNow = 0;
HostSerial Serial;
HostWire Wire;
namespace MotionPlatform { bool isrAttached = false, stuckHigh = false; void (*isr)() = nullptr; }

void startupCase(unsigned beginFailures, bool sensorFails, uint8_t sensorResult,
                 const char* sensorMeaning, uint8_t oledResult, const char* oledMeaning)
{
    Motion motion;
    Display display;
    Wire = HostWire{}; hostOled() = {}; hostNow = 0; Serial.log.clear();
    MotionPlatform::isrAttached = false; MotionPlatform::isr = nullptr;
    MotionPlatform::interruptCalls() = {};
    Wire.registers[0] = 0xE5;
    Wire.registers[0x30] = 0x08;
    Wire.beginFailures = beginFailures;
    Wire.failAll = sensorFails;
    Wire.addressResults[0x53] = sensorResult;
    Wire.addressResults[0x3C] = oledResult;

    const bool motionReady = motion.begin(MOTION_SDA_PIN, MOTION_SCL_PIN, MOTION_INT1_PIN);
    assert(motionReady == (beginFailures < 3 && !sensorFails));
    const unsigned begins = beginFailures < 3 ? beginFailures + 1 : 3;
    assert(Wire.begins == begins && Wire.addressChecks.empty());
    assert(MotionPlatform::isrAttached == motionReady);
    assert(motion.getStartupEvent() == (motionReady ? MotionEvent::Inactivity : MotionEvent::None));
    if (motionReady)
        assert(Wire.registers[0x24] == 10 && Wire.registers[0x2D] == 0x28 && Wire.registers[0x2E] == 0x18);
    if (beginFailures >= 3) assert(Wire.operations == 0 && hostNow == 40);

    std::string expected;
    const unsigned attempts = motionReady ? beginFailures + 1 : 3;
    for (unsigned attempt = 0; attempt < attempts; ++attempt)
    {
        if (attempt) expected += "MOTION INIT | retry " + std::to_string(attempt) + "/2\n";
#ifdef DEVICE_DUDU
        if (attempt < begins)
        {
            expected += "I2C STARTUP DUDU | Wire.begin SDA=0 SCL=1 | result=";
            expected += attempt < beginFailures ? "0 (FAILED)\n" : "1 (READY)\n";
        }
#endif
    }
    assert(Serial.log == expected); // Every actual begin result, no sensor-retry bus reinitialization.

    const bool displayReady = display.begin();
    assert(displayReady); // U8g2 begin's existing result is not a panel acknowledgement.
    assert(Wire.begins == begins && hostOled().begins == 1 && hostOled().hardwareInitializations == 0);
    assert(Wire.clock == 100000 && hostOled().clock == 100000 && hostOled().address == 0x78);
    // A new latched interrupt must survive diagnostics untouched.
    Wire.registers[0x30] = 0x18;
    const auto registers = Wire.registers;
    const auto writes = Wire.writes;
    const auto operations = Wire.operations, now = hostNow;
    const auto startup = motion.getStartupEvent();
    const auto interrupts = MotionPlatform::interruptCalls();
#ifdef DEVICE_DUDU
    assert(display.diagnosticI2cAddress() == 0x3C);
    motion.reportStartupI2cHealth(display.diagnosticI2cAddress());
    const struct { const char* name; const char* address; uint8_t result; const char* meaning; } devices[]{
        {"ADXL345", "0x53", sensorResult, sensorMeaning}, {"OLED", "0x3C", oledResult, oledMeaning}
    };
    for (const auto& device : devices)
    {
        expected += std::string("I2C ADDRESS DUDU | ") + device.name + " addr7=" + device.address + " | ";
        expected += beginFailures >= 3 ? "SKIPPED (Wire.begin failed)\n" :
            std::string("result=") + std::to_string(device.result) + " (" + device.meaning + ")\n";
    }
    if (beginFailures < 3)
        assert((Wire.addressChecks == std::vector<uint8_t>{0x53, 0x3C}));
    else assert(Wire.addressChecks.empty());
#else
    (void)sensorMeaning; (void)oledMeaning;
    assert(Wire.addressChecks.empty());
#endif
    assert(Serial.log == expected);
    assert(Wire.registers == registers && Wire.writes == writes && Wire.operations == operations);
    assert(Wire.begins == begins && Wire.clock == 100000 && hostOled().clock == 100000);
    assert(hostNow == now + Wire.addressChecks.size()); // Only the two modeled transfers, no added delays.
    assert(MotionPlatform::isrAttached == motionReady && motion.getStartupEvent() == startup);
    assert(MotionPlatform::interruptCalls().attaches == interrupts.attaches);
    assert(MotionPlatform::interruptCalls().detaches == interrupts.detaches);
    motion.pauseInterrupt();
}

int main()
{
    for (bool sensorFails : {false, true})
    {
        startupCase(0, sensorFails, 0, "ACK", 0, "ACK");
        startupCase(0, sensorFails, 2, "NACK_OR_ESP_FAIL", 0, "ACK");
        startupCase(0, sensorFails, 0, "ACK", 2, "NACK_OR_ESP_FAIL");
        startupCase(0, sensorFails, 2, "NACK_OR_ESP_FAIL", 2, "NACK_OR_ESP_FAIL");
        startupCase(0, sensorFails, 4, "OTHER_ERROR", 5, "TIMEOUT");
        startupCase(0, sensorFails, 5, "TIMEOUT", 4, "OTHER_ERROR");
        for (unsigned failedBegins : {1U, 2U, 3U})
            startupCase(failedBegins, sensorFails, 0, "ACK", 0, "ACK");
    }
    printf("PASS %s: startup I2C ACK/NACK/ESP_FAIL/error/timeout and failed/retried Wire.begin; Dudu-only address checks skip failed bus, preserve Motion/Display results, settings and latched interrupts\n", DEVICE_NAME);
}
