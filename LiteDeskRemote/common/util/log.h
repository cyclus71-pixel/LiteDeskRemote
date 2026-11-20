#pragma once
// log.h — logger thread-safe: file harian + OutputDebugString (Windows), rotasi 8 MB.
#include <string>
#include <cstdint>

namespace ldr::util {

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

class Logger {
public:
    static Logger& Instance();

    // baseDir: direktori log (dibuat bila belum ada). level minimal yang dicatat.
    void Init(const std::wstring& baseDir, LogLevel minLevel);
    void SetLevel(LogLevel l) { minLevel_ = l; }
    void SetConsole(bool on) { console_ = on; } // untuk test runner
    LogLevel Level() const { return minLevel_; }

    void Debug(const char* fmt, ...);
    void Info(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);

    // Dipakai langsung oleh tests (tanpa file).
    void LogLine(LogLevel lvl, const std::string& line);

private:
    Logger() = default;
    void Write(LogLevel lvl, const char* fmt, va_list args);
    void RotateIfNeeded();

    LogLevel minLevel_ = LogLevel::Info;
    bool console_ = false;
    bool fileOpen_ = false;
    std::wstring dir_;
    std::wstring curPath_;
    int curDate_ = 0;      // yyyymmdd
    size_t curSize_ = 0;
};

} // namespace ldr::util

#define LLOG_D(...) ::ldr::util::Logger::Instance().Debug(__VA_ARGS__)
#define LLOG_I(...) ::ldr::util::Logger::Instance().Info(__VA_ARGS__)
#define LLOG_W(...) ::ldr::util::Logger::Instance().Warn(__VA_ARGS__)
#define LLOG_E(...) ::ldr::util::Logger::Instance().Error(__VA_ARGS__)
