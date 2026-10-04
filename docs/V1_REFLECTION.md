# What I learned and what comes next

v1 is a partly working prototype. I paused development on 4 October 2026.
The [final results](../FINAL_FIRMWARE_TEST.md) record its strengths and problems.

## Learning context

I study Electrical Engineering & IT at RWU Ravensburg-Weingarten and am entering
semester 4. I completed an embedded-systems course from the University of
Colorado Boulder. My RWU subjects also gave me ideas to apply in this project.

Development was AI-assisted. I want to get better at reading, explaining and
changing the C++ code myself. A useful exercise is to follow one button press
from input to packet, reply and LED output, including what happens when a step fails.

## What my RWU subjects contributed

| Subject | What I learned | Where it connects |
| --- | --- | --- |
| **Computer Technology — lecture** | Binary/hexadecimal data, registers, memory, byte order and interrupts. | The eight-byte [message](../include/Protocol.h), sensor registers and [motion interrupt](../src/Motion.cpp). |
| **Computer Technology — lab** | ARM assembly, reading/writing memory, flags, bit masks and Linux/Make tools. | Sensor settings, [wake-pin masks](../src/CC1101WakeRecovery.cpp) and understanding how source files become firmware. |
| **Digital Electronics — lecture** | Boolean logic, truth tables, logic levels, sequential circuits and state machines. | Sleep conditions, HIGH/LOW inputs, [power states](../src/PowerManager.cpp) and [LED phases](../src/LED.cpp). |
| **Digital Electronics — lab** | VHDL/FPGA logic, timed LED patterns including a heartbeat, and PWM dimming. | Planning pulse timing and brightness. BubuDudu sends colour values to a WS2812B through the NeoPixel library. |
| **Circuit Design** | Falstad simulation, voltage dividers, amplifier circuits, filtering, sampling, comparators and reference voltages. | Trying voltage scaling, threshold comparisons and LED indication in the [battery-indicator simulation](../simulations/battery_indicator_v1.txt). |

The Computer Technology and Digital Electronics labs used ARM assembly and
FPGA/VHDL hardware design. BubuDudu uses C++ on a RISC-V ESP32-C3, so I applied
the ideas on a different platform. Lab preparation and report writing also helped
me separate expected results from what I observed.

### Course material references

Lecture/lab materials by **Prof. Dr.-Ing. A. Siggelkow, RWU**.
These are document dates, not my course-completion dates:

- *Computer Technology*, 23 March 2026: sections 3.1–3.2, 4.6, 4.12 and 6.2.1.
- *Rechnertechnologie: Labor*, 14 February 2024: sections 2.3–2.6 and chapters 3–5.
- *Digital Electronics*, 30 September 2025: chapters 2–4 and 6–8.
- *Digital Electronics: Lab Notes*, 22 October 2025: chapters 2–6.

The Circuit Design connection also draws on course exercises covering sensor
bridges, signal amplification, noise filtering, sampling and a 3-bit
analog-to-digital converter (turning a voltage into one of eight digital levels).
The comparator and reference-voltage exercises were especially relevant to my
battery-indicator idea.

## Concepts I tried to put into practice

- **One shared codebase:** build Bubu and Dudu with different identity settings.
- **Reliable messages:** match replies, limit retries and avoid repeating an action.
- **Callbacks and queues:** save incoming packets, then process them in the main loop.
- **State machines:** give sleep and animation clear steps and time limits.
- **Saved state:** remember message history through deep sleep without accepting bad data.
- **Different kinds of tests:** use logs, bus captures, computer tests and device
  observations for the questions each can answer.

[Code guide](../ARCHITECTURE.md) · [Test guide](../tests/host/README.md)

## Practices worth keeping

Small changes were easier to debug. The [22 September entry](../DEVLOG.md#2026-09-22)
records a return to that approach. Git checkpoints and backups let me return to
a working version when later experiments broke things.

Keeping the tests strict helped too. A [Linux test failure](../DEVLOG.md#2026-10-03--final-integration-and-software-validation)
found a memory leak in the simulated reboot setup that local checks missed.
Fixing the test setup was better than turning the check off.

## Why physical validation came late

For much of development, I did not have 18650 batteries or a suitable 5 V DC-DC
boost module. Neither was available locally, and online delivery would not have
arrived within the time I had available. This delayed the portable power setup
needed for realistic, repeated tests of moving one device far away and bringing
it back while the other stayed still.

Firmware work continued while those physical tests were blocked. That is an
important reason the software grew faster than its physical validation. The
delayed tests included FAR/CLOSE transitions, radio fallback to CC1101 when
ESP-NOW could not deliver, and recovery when the partner returned. Both devices
ran from batteries on 3 October, shortly before I paused the project.
There was not enough time left for repeated tests, fixes and retesting.

My lesson is to treat the power supply and component availability as part of
the test plan from the beginning. A portable device needs portable power early
enough to test its intended use. If essential hardware is unavailable, I need
to keep that validation gap visible and limit how much untested behavior I add.

## What I would approach differently

I would add and physically test one change at a time across the radios, motion,
proximity, output and sleep, with time reserved for fixing what the tests reveal.

The key case left insufficiently tested was simple: **one device moves away and
returns while the other stays still**. Both need useful current state. I need
clear rules for when to refresh a result, when it is too old and when to show UNKNOWN.
The current timeout rule keeps old results, which can leave a device stuck.

Passing computer tests did not mean the devices worked reliably. I need repeated
physical tests and better motion/signal-strength calibration. More parts or
more layers of code are useful only when they solve a real problem.

## Electronics that remain unfinished

![Battery-indicator simulation](images/falstad-battery-indicator-simulation.png)

I explored a comparator battery indicator in [Falstad](../simulations/battery_indicator_v1.txt).
This was my attempt to apply what I had learned in my university Circuit Design
class. The class used Falstad to explore how circuits measure and process signals.
For BubuDudu, I tried resistor voltage dividers to scale the battery voltage,
comparators to compare that voltage with reference levels, and LEDs to show the
result. It gave me a project-specific reason to experiment with those ideas.

The simulation uses ideal components; I did not build it or finish the switching thresholds
that prevent flicker. MOSFET power switching is also unfinished. The
[19 September notes](../DEVLOG.md#2026-09-19) preserve the experiment.

Both devices ran on batteries on 3 October. I measured about 3.9 V per cell and
4.99–5.02 V at the booster, but not current draw, battery life or charging under load.

## Aspirations for a possible v2

- Fix state refresh and recovery before adding features.
- Consider UWB distance sensing; no hardware has been chosen or bought for it.
- Use a MOSFET only if there is a useful load to switch.
- Build and measure a simple comparator battery indicator.
- Measure current in each state, battery life and charging under load.
- Add gestures or extra display fields only if they improve the device.

## Saved restart point

1. Read the [results](../FINAL_FIRMWARE_TEST.md), [version notes](../README.md#which-code-was-tested)
   and [cleanup checklist](V1_AUDIT_RESOLUTION.md).
2. Check Git status and start a separate v2 branch. Keep old branches, stashes,
   backups and unrelated editor changes safe.
3. Test the one-moving-device case first. Use matching firmware, paired logs
   and clear notes on movement, screens and LEDs.
4. Make one small change, run the [software checks](../tests/host/README.md), then
   compare repeated device tests. Keep calibration and radio recovery as separate questions.

I now have ideas for improving stale proximity state and radio fallback/recovery,
but they still need implementation and testing. With the university semester
starting, I need to focus on my studies, so development stays paused. There is
no promised v2 date.
