#include "log.h"
#include "time.h"
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <filesystem>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ldr::util {

namespace {
std::mutex gMutex;
const char* LevelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        default:              return "ERR";
    }
}
} // namespace

Logger& Logger::Instance() {
    static Logger inst;
    return inst;
}

void Logger::Init(const std::wstring& baseDir, LogLevel minLevel) {
    std::lock_guard<std::mutex> lk(gMutex);
    minLevel_ = minLevel;
    dir_ = baseDir;
    std::error_code ec;
    fs::create_directories(dir_, ec);
}

void Logger::LogLine(LogLevel lvl, const std::string& line) {
    std::lock_guard<std::mutex> lk(gMutex);
    if (lvl < minLevel_) return;
    std::string full = "[" + FormatWall(WallNowMs()) + "] [" + LevelTag(lvl) + "] " + line + "\n";

#if defined(_WIN32)
    OutputDebugStringA(full.c_str());
#endif
    if (console_) {
        std::fputs(full.c_str(), lvl >= LogLevel::Warn ? stderr : stdout);
        std::fflush(lvl >= LogLevel::Warn ? stderr : stdout);
    }

    if (dir_.empty()) return;
    int date = std::atoi(DateStamp(WallNowMs()).c_str());
    if (!fileOpen_ || date != curDate_) {
        curDate_ = date;
        curPath_ = dir_ + L"\\app-" + std::to_wstring(date) + L".log";
        fileOpen_ = true;
        curSize_ = 0;
    }
    RotateIfNeeded();

    FILE* f = nullptr;
#if defined(_WIN32)
    _wfopen_s(&f, curPath_.c_str(), L"ab");
#else
    f = std::fopen(std::string(curPath_.begin(), curPath_.end()).c_str(), "ab");
#endif
    if (f) {
        std::fwrite(full.data(), 1, full.size(), f);
        std::fclose(f);
        curSize_ += full.size();
    }
}

void Logger::RotateIfNeeded() {
    if (curSize_ < 8u * 1024 * 1024) return;
    std::wstring old = dir_ + L"\\app-" + std::to_wstring(curDate_) + L"-1.log";
    std::error_code ec;
    fs::remove(old, ec);
    fs::rename(curPath_, old, ec);
    curSize_ = 0;
}

void Logger::Write(LogLevel lvl, const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    LogLine(lvl, buf);
}

void Logger::Debug(const char* fmt, ...) { va_list a; va_start(a, fmt); Write(LogLevel::Debug, fmt, a); va_end(a); }
void Logger::Info(const char* fmt, ...)  { va_list a; va_start(a, fmt); Write(LogLevel::Info,  fmt, a); va_end(a); }
void Logger::Warn(const char* fmt, ...)  { va_list a; va_start(a, fmt); Write(LogLevel::Warn,  fmt, a); va_end(a); }
void Logger::Error(const char* fmt, ...) { va_list a; va_start(a, fmt); Write(LogLevel::Error, fmt, a); va_end(a); }

} // namespace ldr::util
