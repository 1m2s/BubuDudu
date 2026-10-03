#pragma once
// Include production implementations for white-box assertions only. Firmware
// builds compile these separately and see only their narrow module headers.
#include "../../../src/app/MotionRuntime.cpp"
#include "../../../src/app/ButtonRuntime.cpp"
#include "../../../src/app/Presentation.cpp"
#include "../../../src/app/SleepRuntime.cpp"
#include "../../../src/app/RadioRuntime.cpp"
#include "../../../src/main.cpp"
using namespace AppIdentity;
using namespace MotionRuntime;
using namespace ButtonRuntime;
using namespace Presentation;
using namespace SleepRuntime;
using namespace RadioRuntime;
