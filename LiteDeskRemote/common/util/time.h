// time.h — waktu monotonic & wall-clock lintas-platform.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace ldr::util {

// Monotonic — untuk timeout, RTT, rate limiter (bebas dari perubahan jam sistem).
inline int64_t NowMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Wall-clock unix ms — untuk log dan audit.
inline int64_t WallNowMs() {
    return static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

inline std::string FormatWall(int64_t wallMs) {
    time_t t = static_cast<time_t>(wallMs / 1000);
    char buf[64];
    tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return buf;
}

inline std::string DateStamp(int64_t wallMs) {
    time_t t = static_cast<time_t>(wallMs / 1000);
    char buf[32];
    tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

} // namespace ldr::util
