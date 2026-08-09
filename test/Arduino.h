#pragma once

#include <iostream>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdarg>

// Types basiques d'Arduino
typedef uint8_t byte;
typedef bool boolean;

// Stubs de temps
inline uint32_t micros() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

inline uint32_t millis() {
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// Stub Serial
class HardwareSerial {
public:
    template<typename T>
    void print(T msg) { std::cout << msg; }
    template<typename T>
    void println(T msg) { std::cout << msg << std::endl; }
    void printf(const char* format, ...) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
};

extern HardwareSerial Serial;