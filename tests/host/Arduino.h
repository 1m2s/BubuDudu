#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include <deque>
#include <string>
#include <vector>

extern uint32_t hostNow;
inline uint32_t millis() { return hostNow; }
inline void delay(uint32_t ms) { hostNow += ms; }

struct HostSerial
{
    std::string log;
    std::deque<char> input;
    void begin(unsigned long) {}
    void flush() {}
    int available() { return static_cast<int>(input.size()); }
    int read() { int c = input.front(); input.pop_front(); return c; }
    void print(const char* s) { log += s; }
    void println(const char* s = "") { log += std::string(s) + "\n"; }
    void printf(const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        log += buffer;
    }
};
extern HostSerial Serial;
