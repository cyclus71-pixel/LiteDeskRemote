#include "config.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ldr::util {

Config& Config::I() {
    static Config inst;
    return inst;
}

#if defined(_WIN32)
static const wchar_t* kSep = L"\\";
#else
static const wchar_t* kSep = L"/";
#endif

std::wstring Config::BaseDir() const {
    if (!baseDir_.empty()) return baseDir_;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH + 1] = {0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH) return L".";
    return std::wstring(buf) + L"\\LiteDeskRemote";
#else
    const char* home = std::getenv("HOME");
    if (!home) home = ".";
    return std::wstring(home, home + std::strlen(home)) + L"/.litedesk";
#endif
}

std::wstring Config::SecurityDir() const { return BaseDir() + kSep + L"security"; }
std::wstring Config::LogDir() const      { return BaseDir() + kSep + L"logs"; }
std::wstring Config::ConfigPath() const  { return BaseDir() + kSep + L"config.json"; }
std::wstring Config::SecurityPath(const std::string& file) const {
    return SecurityDir() + kSep + std::wstring(file.begin(), file.end());
}
std::wstring Config::AuditLogPath() const      { return LogDir() + kSep + L"audit-latest.jsonl"; }
std::wstring Config::AppLogPath() const        { return LogDir() + kSep + L"app-latest.log"; }

bool Config::Load() {
    if (loaded_) return true;
    std::error_code ec;
    fs::create_directories(BaseDir(), ec);
    fs::create_directories(SecurityDir(), ec);
    fs::create_directories(LogDir(), ec);

    std::ifstream f(std::filesystem::path(ConfigPath()), std::ios::binary);
    if (f) {
        std::stringstream ss;
        ss << f.rdbuf();
        auto v = json::Parse(ss.str());
        if (v && v->IsObj()) root_ = *v;
    }
    if (!root_.IsObj()) root_ = json::Value::ObjectV();
    EnsureDefaults();
    loaded_ = true;
    return true;
}

void Config::EnsureDefaults() {
    auto ensure = [&](const char* path, json::Value v) {
        if (Resolve(path) == nullptr) ResolveOrCreate(path) = std::move(v);
    };
    ensure("network.tcp_port", json::Value::NumV(48500));
    ensure("network.discovery_enabled", json::Value::BoolV(true));
    ensure("network.discovery_port", json::Value::NumV(48501));
    ensure("network.max_sessions", json::Value::NumV(3));
    ensure("security.require_confirmation", json::Value::BoolV(true));
    ensure("security.unattended", json::Value::BoolV(false));
    ensure("security.pin_iterations", json::Value::NumV(210000));
    ensure("security.pin_length", json::Value::NumV(6));
    ensure("security.idle_timeout_min", json::Value::NumV(0));
    ensure("security.auto_accept_devices", json::Value::ArrayV());
    ensure("security.rate_limit.max_fails", json::Value::NumV(5));
    ensure("security.rate_limit.window_min", json::Value::NumV(10));
    ensure("security.rate_limit.base_lock_s", json::Value::NumV(30));
    ensure("security.rate_limit.max_lock_s", json::Value::NumV(900));
    ensure("quality.target_fps", json::Value::NumV(30));
    ensure("quality.quality_preset", json::Value::StrV("balanced"));
    ensure("quality.bitrate_kbps", json::Value::NumV(8000));
    ensure("ui.accent", json::Value::StrV("green"));
}

const json::Value* Config::Resolve(const std::string& path) const {
    const json::Value* cur = &root_;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        cur = cur->Find(part);
        if (!cur) return nullptr;
        if (dot == std::string::npos) return cur;
        start = dot + 1;
    }
}

json::Value& Config::ResolveOrCreate(const std::string& path) {
    json::Value* cur = &root_;
    size_t start = 0;
    while (true) {
        size_t dot = path.find('.', start);
        std::string part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (!cur->IsObj()) { cur->t = json::Value::Type::Obj; cur->obj.clear(); }
        json::Value* next = const_cast<json::Value*>(cur->Find(part));
        if (!next) {
            cur->Set(part, json::Value::ObjectV());
            next = const_cast<json::Value*>(cur->Find(part));
        }
        cur = next;
        if (dot == std::string::npos) return *cur;
        start = dot + 1;
    }
}

std::string Config::GetStr(const std::string& path, const std::string& def) const {
    auto v = Resolve(path);
    if (v) { auto s = v->AsStr(); if (s) return *s; }
    return def;
}
int64_t Config::GetInt(const std::string& path, int64_t def) const {
    auto v = Resolve(path);
    if (v) { auto n = v->AsInt(); if (n) return *n; }
    return def;
}
bool Config::GetBool(const std::string& path, bool def) const {
    auto v = Resolve(path);
    if (v) { auto b = v->AsBool(); if (b) return *b; }
    return def;
}

void Config::SetInt(const std::string& path, int64_t v)   { ResolveOrCreate(path) = json::Value::NumV(static_cast<double>(v)); }
void Config::SetStr(const std::string& path, const std::string& v) { ResolveOrCreate(path) = json::Value::StrV(v); }
void Config::SetBool(const std::string& path, bool v)     { ResolveOrCreate(path) = json::Value::BoolV(v); }

std::vector<std::string> Config::GetStrArray(const std::string& path) const {
    std::vector<std::string> out;
    auto v = Resolve(path);
    if (v && v->IsArr())
        for (const auto& e : v->arr) { auto s = e.AsStr(); if (s) out.push_back(*s); }
    return out;
}
void Config::SetStrArray(const std::string& path, const std::vector<std::string>& v) {
    json::Value arr = json::Value::ArrayV();
    for (const auto& s : v) arr.arr.push_back(json::Value::StrV(s));
    ResolveOrCreate(path) = std::move(arr);
}

bool Config::Save(std::string* err) {
    std::error_code ec;
    fs::create_directories(BaseDir(), ec);
    std::filesystem::path finalPath(ConfigPath());
    std::filesystem::path tmpPath = finalPath.string() + ".tmp";
    {
        std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
        if (!f) { if (err) *err = "tidak bisa membuka file config"; return false; }
        f << json::Dump(root_, 2);
        f.flush();
        if (!f.good()) { if (err) *err = "gagal menulis file config"; return false; }
    }
    fs::rename(tmpPath, finalPath, ec); // tulis atomik: tmp → rename
    if (ec) { if (err) *err = "gagal rename config"; return false; }
    return true;
}

} // namespace ldr::util
