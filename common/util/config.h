#pragma once
// config.h — konfigurasi aplikasi berbasis JSON dengan default lengkap.
// Lokasi: %LOCALAPPDATA%\LiteDeskRemote\config.json (Windows) atau $HOME/.litedesk (non-Windows, untuk test).
#include "json.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ldr::util {

class Config {
public:
    static Config& I();

    // Untuk tests: arahkan semua path ke direktori sementara.
    void SetBaseDir(const std::wstring& dir) { baseDir_ = dir; loaded_ = false; }

    bool Load();                       // buat default bila belum ada
    bool Save(std::string* err = nullptr);

    std::wstring BaseDir() const;
    std::wstring SecurityDir() const;
    std::wstring LogDir() const;
    std::wstring ConfigPath() const;
    std::wstring SecurityPath(const std::string& file) const; // file: "pin.json", "identity.der", ...
    std::wstring AuditLogPath() const;
    std::wstring AppLogPath() const;

    // Getter bertitik ("network.tcp_port"), mengembalikan default bila hilang.
    std::string GetStr(const std::string& path, const std::string& def) const;
    int64_t GetInt(const std::string& path, int64_t def) const;
    bool GetBool(const std::string& path, bool def) const;

    void SetInt(const std::string& path, int64_t v);
    void SetStr(const std::string& path, const std::string& v);
    void SetBool(const std::string& path, bool v);

    // Array string (auto-accept devices)
    std::vector<std::string> GetStrArray(const std::string& path) const;
    void SetStrArray(const std::string& path, const std::vector<std::string>& v);

private:
    Config() = default;
    const json::Value* Resolve(const std::string& path) const;
    json::Value& ResolveOrCreate(const std::string& path);
    void EnsureDefaults();

    json::Value root_;
    std::wstring baseDir_;   // kosong = lokasi default OS
    bool loaded_ = false;
};

} // namespace ldr::util
