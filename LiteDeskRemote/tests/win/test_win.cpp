// test_win.cpp — test integrasi Windows (ldwin_tests).
// Mencakup:
//  1. Vektor uji kripto resmi via CNG (SHA-256, HMAC, PBKDF2) — memvalidasi jalur produksi.
//  2. Identitas TLS host (sertifikat self-signed + fingerprint).
//  3. Handshake TLS + autentikasi PIN end-to-end di localhost (SessionManager x ViewerSession).
//  4. PIN salah → kegagalan + rate limiter lockout.
//  5. Discovery UDP query/reply di loopback.
// Dijalankan di Windows oleh scripts/run_tests.ps1.
#include "host/session.h"
#include "viewer/session.h"
#include "common/security/crypto_win.h"
#include "common/util/config.h"
#include "common/util/log.h"
#include "common/util/time.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace ldr;

static int gPassed = 0, gFailed = 0;
static void Fail(const std::string& what) {
    std::fprintf(stderr, "  [GAGAL] %s\n", what.c_str());
    gFailed++;
}
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (cond) { gPassed++; }                                                \
        else Fail(std::string(__FILE__) + ":" + std::to_string(__LINE__) + "  " #cond); \
    } while (0)

template <typename Pred>
static bool Within(int ms, Pred p) {
    int64_t deadline = util::NowMs() + ms;
    while (util::NowMs() < deadline) {
        if (p()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return p();
}

// ===== 1. Vektor uji CNG =====
static void TestCngVectors() {
    std::printf("cng vectors: ");
    using sec::Sha256;
    using sec::HmacSha256;
    using sec::Pbkdf2Sha256;
    auto h1 = Sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK(util::HexEncode(h1.data(), 32) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    auto mac = HmacSha256(reinterpret_cast<const uint8_t*>("Jefe"), 4,
                          reinterpret_cast<const uint8_t*>("what do ya want for nothing?"), 28);
    CHECK(util::HexEncode(mac.data(), 32) ==
          "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
    auto d = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>("password"), 8,
                          reinterpret_cast<const uint8_t*>("salt"), 4, 4096);
    CHECK(util::HexEncode(d.data(), 32) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    uint8_t rnd[32];
    CHECK(sec::RandomBytes(rnd, sizeof(rnd)));
    std::printf("OK\n");
}

// ===== 2. Identitas TLS =====
static void TestHostIdentity(const std::wstring& secDir) {
    std::printf("host identity: ");
    std::string err;
    auto id = sec::HostIdentity::LoadOrCreate(secDir, "LD-TESTA-TESTB", &err);
    CHECK(id != nullptr);
    if (!id) { std::printf("GAGAL (%s)\n", err.c_str()); return; }
    CHECK(id->Cert() != nullptr);
    std::string fp = id->FingerprintHex();
    CHECK(fp.size() == 95); // 32 byte × 3 - 1 (spasi antar byte)
    // load ulang → fingerprint sama (persisten)
    auto id2 = sec::HostIdentity::LoadOrCreate(secDir, "LD-TESTA-TESTB", &err);
    CHECK(id2 != nullptr && id2->FingerprintHex() == fp);
    std::printf("OK\n");
}

// ===== 3+4. Handshake end-to-end =====
static void TestAuthHandshake(const std::wstring& baseDir) {
    std::printf("auth handshake (ok): ");

    // config test: port ephemeral, tanpa konfirmasi, PIN iterations rendah agar cepat
    util::Config::I().SetBaseDir(baseDir);
    util::Config::I().Load();
    util::Config::I().SetInt("network.tcp_port", 0);
    util::Config::I().SetBool("network.discovery_enabled", false);
    util::Config::I().SetBool("security.require_confirmation", false);
    util::Config::I().SetBool("security.unattended", true);
    util::Config::I().SetInt("security.pin_iterations", 10000);
    util::Config::I().Save();

    std::string err;
    CHECK(host::PinStoreInstance().SetCustom("123456", &err));

    host::SessionManager mgr;
    CHECK(mgr.Initialize(&err));
    CHECK(mgr.Start(&err));
    CHECK(mgr.Running());
    CHECK(mgr.Port() != 0);

    viewer::KnownHostStore store(baseDir + L"\\known_hosts.json");
    store.Load();
    viewer::ViewerSession vs(store);
    vs.Connect("127.0.0.1", mgr.Port(), "123456");
    bool active = Within(15000, [&]() {
        return vs.Snapshot().state == viewer::ConnState::Active;
    });
    CHECK(active);
    if (!active) {
        std::printf("GAGAL: status=%s\n", vs.Snapshot().status.c_str());
    } else {
        CHECK(vs.Snapshot().hostId == mgr.DeviceId());
        CHECK(vs.Snapshot().permissions != 0);
        // RTT terisi dalam 10 detik (ping tiap 5 detik)
        CHECK(Within(10000, [&]() { return vs.Snapshot().latencyMs >= 0; }));
    }
    vs.Disconnect();
    mgr.Stop();
    std::printf("OK\n");

    // ===== PIN salah + rate limiter =====
    std::printf("auth handshake (salah + lockout): ");
    CHECK(mgr.Start(&err));
    // 5x PIN salah → lockout
    for (int i = 0; i < 5; ++i) {
        viewer::ViewerSession v2(store);
        v2.Connect("127.0.0.1", mgr.Port(), "000000");
        Within(15000, [&]() {
            auto st = v2.Snapshot().state;
            return st == viewer::ConnState::Failed || st == viewer::ConnState::Closed;
        });
        v2.Disconnect();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    // koneksi berikutnya harus ditolak karena terkunci
    viewer::ViewerSession v3(store);
    v3.Connect("127.0.0.1", mgr.Port(), "123456");
    bool locked = Within(15000, [&]() {
        auto s = v3.Snapshot();
        return s.state == viewer::ConnState::Failed &&
               s.status.find("terkunci") != std::string::npos;
    });
    CHECK(locked);
    v3.Disconnect();
    mgr.Stop();
    std::printf("OK\n");
}

// ===== 5. Discovery =====
static void TestDiscovery(const std::wstring& baseDir) {
    std::printf("discovery loopback: ");
    util::Config::I().SetBaseDir(baseDir);
    util::Config::I().Load();
    util::Config::I().SetInt("network.discovery_port", 48777);
    util::Config::I().Save();

    net::DiscoveryResponder resp;
    std::string err;
    CHECK(resp.Start(48777, "LD-TESTA-TESTB", "test-host", 48500, &err));
    CHECK(resp.Running());

    net::DiscoveryWatcher watcher;
    CHECK(watcher.Start(48777, &err));
    watcher.RefreshNow(1500);
    std::string ip;
    uint16_t port = 0;
    CHECK(watcher.Resolve("LD-TESTA-TESTB", &ip, &port));
    CHECK(ip == "127.0.0.1");
    CHECK(port == 48500);

    watcher.Stop();
    resp.Stop();
    std::printf("OK\n");
}

int main() {
    util::Logger::Instance().SetConsole(true);
    util::Logger::Instance().SetLevel(util::LogLevel::Info);
    net::WsaInit wsa;

    std::filesystem::path base = std::filesystem::temp_directory_path() /
                                 ("litedesk-wtest-" + std::to_string(std::rand()));
    std::error_code ec;
    std::filesystem::create_directories(base, ec);

    TestCngVectors();
    TestHostIdentity(base.wstring() + L"\\security");
    TestAuthHandshake(base.wstring());
    TestDiscovery(base.wstring());

    std::printf("\n%d lolos, %d gagal\n", gPassed, gFailed);
    std::filesystem::remove_all(base, ec);
    if (gFailed > 0) {
        std::printf("HASIL: GAGAL\n");
        return 1;
    }
    std::printf("HASIL: SEMUA TEST LOLOS\n");
    return 0;
}
