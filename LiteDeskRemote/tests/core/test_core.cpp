// test_core.cpp — unit test inti lintas-platform (ldcore_tests).
// Mencakup: framing (roundtrip, partial feed, guard ukuran), semua pesan protokol,
// JSON, base32/hex/consttime, vektor uji SHA-256/HMAC/PBKDF2 resmi, rate limiter,
// format device id, konfigurasi. Berjalan di OS mana pun (Linux/CI & Windows).
#include "common/protocol/messages.h"
#include "common/security/security.h"
#include "common/security/rate_limiter.h"
#include "common/util/config.h"
#include "common/util/log.h"
#include "common/util/time.h"

#if defined(_WIN32)
#include "common/security/crypto_win.h"
#else
#include "common/security/crypto_portable.h"
#endif

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace ldr;

static int gPassed = 0, gFailed = 0;
static void Fail(const std::string& what) {
    std::fprintf(stderr, "  [GAGAL] %s\n", what.c_str());
    gFailed++;
}
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) {                                                            \
            gPassed++;                                                         \
        } else {                                                               \
            Fail(std::string(__FILE__) + ":" + std::to_string(__LINE__) + "  " #cond); \
        }                                                                      \
    } while (0)
#define CHECK_EQ(a, b)                                                          \
    do {                                                                        \
        auto va_ = (a); auto vb_ = (b);                                         \
        if (va_ == vb_) gPassed++;                                              \
        else Fail(std::string(__FILE__) + ":" + std::to_string(__LINE__) +       \
                  " " #a " != " #b);                                            \
    } while (0)
#define CHECK_NE(a, b)                                                          \
    do {                                                                        \
        auto va_ = (a); auto vb_ = (b);                                         \
        if (va_ != vb_) gPassed++;                                              \
        else Fail(std::string(__FILE__) + ":" + std::to_string(__LINE__) +       \
                  " " #a " == " #b " (harus beda)");                            \
    } while (0)

// ===== Framing =====
static void TestFraming() {
    std::printf("framing: ");
    using namespace proto;
    auto payload = std::vector<uint8_t>{1, 2, 3, 4, 5};
    auto frame = BuildFrame(MsgType::Hello, payload);
    CHECK_EQ(frame.size(), kFrameHeaderSize + payload.size());

    FrameHeader h;
    CHECK(proto::ParseHeader(frame.data(), frame.size(), &h));
    CHECK(h.type == MsgType::Hello);
    CHECK_EQ(h.payloadSize, payload.size());
    CHECK_EQ(h.version, kProtocolVersion);

    // parser streaming: feed byte-per-byte
    FramedReader rd;
    for (uint8_t b : frame) {
        rd.Feed(&b, 1);
        CHECK(!rd.Failed());
    }
    auto frames = rd.TakeFrames();
    CHECK_EQ(frames.size(), 1u);
    CHECK(frames[0].payload == payload);

    // burst 3 frame sekaligus
    rd.Reset();
    auto f1 = BuildFrame(MsgType::Ping, EncodePing(proto::Ping{123}));
    auto f2 = BuildFrame(MsgType::Pong, EncodePong(proto::Pong{123, 456, 789}));
    auto f3 = BuildFrame(MsgType::Hello, payload);
    std::vector<uint8_t> all;
    all.insert(all.end(), f1.begin(), f1.end());
    all.insert(all.end(), f2.begin(), f2.end());
    all.insert(all.end(), f3.begin(), f3.end());
    // feed potongan acak
    std::mt19937 rng(12345);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t chunk = 1 + rng() % 7;
        if (chunk > all.size() - pos) chunk = all.size() - pos;
        rd.Feed(all.data() + pos, chunk);
        pos += chunk;
        CHECK(!rd.Failed());
    }
    frames = rd.TakeFrames();
    CHECK_EQ(frames.size(), 3u);
    auto p1 = DecodePing(frames[0].payload.data(), frames[0].payload.size());
    auto p2 = DecodePong(frames[1].payload.data(), frames[1].payload.size());
    CHECK(p1 && p1->tSend == 123);
    CHECK(p2 && p2->tRecv == 456 && p2->tNow == 789);

    // magic salah → error
    rd.Reset();
    auto bad = frame;
    bad[0] = 'X';
    rd.Feed(bad);
    CHECK(rd.Failed());

    // payload > batas → error
    rd.Reset();
    auto big = BuildFrame(MsgType::ControlStatus, std::vector<uint8_t>(10, 7));
    util::PutU32LE(big.data() + 8, proto::kMaxPayload + 1);
    rd.Feed(big);
    CHECK(rd.Failed());

    // header saja tanpa payload → menunggu, tidak error
    rd.Reset();
    rd.Feed(f1.data(), proto::kFrameHeaderSize + 2);
    CHECK(!rd.Failed());
    CHECK(rd.TakeFrames().empty());

    std::printf("OK\n");
}

// ===== Messages =====
static void TestMessages() {
    std::printf("messages: ");
    using namespace proto;
    {
        Hello m;
        m.protoVer = 1; m.features = 0xFF00FF00; m.deviceId = "LD-AAAAA-BBBBB";
        m.deviceName = "PC Kantor"; m.kind = 0;
        auto b = EncodeHello(m);
        auto d = DecodeHello(b.data(), b.size());
        CHECK(d && d->deviceName == "PC Kantor" && d->features == 0xFF00FF00 && d->kind == 0);
    }
    {
        HelloAck m;
        m.deviceId = "LD-XXXXX-YYYYY"; m.hostName = "WIN11-HOST"; m.sessionId = 0xDEADBEEFCAFEULL;
        auto b = EncodeHelloAck(m);
        auto d = DecodeHelloAck(b.data(), b.size());
        CHECK(d && d->sessionId == 0xDEADBEEFCAFEULL && d->hostName == "WIN11-HOST");
    }
    {
        AuthChallenge m;
        m.serverNonce.fill(7); m.salt.fill(9); m.iterations = 210000;
        auto b = EncodeAuthChallenge(m);
        CHECK_EQ(b.size(), 32u + 16u + 4u);
        auto d = DecodeAuthChallenge(b.data(), b.size());
        CHECK(d && d->iterations == 210000 && d->serverNonce[5] == 7 && d->salt[0] == 9);
    }
    {
        AuthResponse m;
        m.clientNonce.fill(1); m.proof.fill(2); m.deviceId = "LD-VVVVV-WWWWW";
        m.deviceName = "Laptop"; m.requestedPerms = 0x1F;
        auto b = EncodeAuthResponse(m);
        auto d = DecodeAuthResponse(b.data(), b.size());
        CHECK(d && d->requestedPerms == 0x1F && d->deviceId == "LD-VVVVV-WWWWW");
        CHECK(d->clientNonce[0] == 1 && d->proof[31] == 2);
    }
    {
        AuthResult m; m.ok = true; m.reason = ""; m.permissions = 0x17;
        auto b = EncodeAuthResult(m);
        auto d = DecodeAuthResult(b.data(), b.size());
        CHECK(d && d->ok && d->permissions == 0x17);
    }
    {
        SessionClosed m; m.reasonCode = proto::reason::IdleTimeout; m.text = "idle";
        auto b = EncodeSessionClosed(m);
        auto d = DecodeSessionClosed(b.data(), b.size());
        CHECK(d && d->reasonCode == proto::reason::IdleTimeout && d->text == "idle");
    }
    // payload terpotong → decode gagal (bukan crash)
    {
        Hello m; m.deviceId = "x"; m.deviceName = "y";
        auto b = EncodeHello(m);
        CHECK(!DecodeHello(b.data(), b.size() - 1));
        CHECK(!DecodeHello(nullptr, 0));
        // string panjang hampir batas u16
        Hello big; big.deviceName = std::string(2000, 'a');
        auto bb = EncodeHello(big);
        auto dd = DecodeHello(bb.data(), bb.size());
        CHECK(dd && dd->deviceName.size() == 1024); // di-clamp ke 1024
    }
    std::printf("OK\n");
}

// ===== JSON =====
static void TestJson() {
    std::printf("json: ");
    using namespace util::json;
    auto v = Parse(R"({"a":1,"b":[true,false,null],"c":"teks\n\"kutip\"","d":{"e":2.5}})");
    CHECK(v && v->IsObj());
    CHECK_EQ(v->Find("a")->AsInt().value_or(-1), 1);
    CHECK(v->Find("b")->arr[0].AsBool().value_or(false));
    CHECK(v->Find("b")->arr[2].IsNull());
    CHECK_EQ(v->Find("c")->AsStr().value_or(""), "teks\n\"kutip\"");
    CHECK_EQ(v->Find("d")->Find("e")->AsNum().value_or(0), 2.5);

    // roundtrip
    std::string dump = Dump(*v, 0);
    auto v2 = Parse(dump);
    CHECK(v2 && v2->Find("a")->AsInt().value_or(-1) == 1);

    // unicode escape + surrogate pair (😀 = U+1F600)
    auto u = Parse("\"\\ud83d\\ude00\"");
    CHECK(u && u->AsStr() && *u->AsStr() == "\xF0\x9F\x98\x80");

    // invalid
    CHECK(!Parse("{"));
    CHECK(!Parse("{\"a\":}"));
    CHECK(!Parse("nul"));
    CHECK(Parse("[1,2]") && Parse("[1,2]")->IsArr());

    // Set/Find pada objek kosong
    Value o = Value::ObjectV();
    o.Set("k", Value::NumV(42));
    CHECK_EQ(o.Find("k")->AsInt().value_or(0), 42);
    o.Set("k", Value::NumV(43));
    CHECK_EQ(o.Find("k")->AsInt().value_or(0), 43);
    CHECK_EQ(o.obj.size(), 1u);
    std::printf("OK\n");
}

// ===== Codec =====
static void TestCodec() {
    std::printf("codec: ");
    // base32 RFC 4648 vektor resmi
    using util::Base32Encode;
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>(""), 0), std::string(""));
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>("f"), 1), std::string("MY"));
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>("fo"), 2), std::string("MZXQ"));
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>("foo"), 3), std::string("MZXW6"));
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>("foob"), 4), std::string("MZXW6YQ"));
    CHECK_EQ(Base32Encode(reinterpret_cast<const uint8_t*>("foobar"), 6),
             std::string("MZXW6YTBOI"));

    // hex
    uint8_t h[4] = {0x0a, 0xff, 0x01, 0x9e};
    CHECK_EQ(util::HexEncode(h, 4), std::string("0aff019e"));
    CHECK_EQ(util::HexEncode(h, 4, true), std::string("0a ff 01 9e"));

    // consttime equal
    CHECK(util::ConstTimeEqual(h, h, 4));
    uint8_t h2[4] = {0x0a, 0xff, 0x01, 0x9f};
    CHECK(!util::ConstTimeEqual(h, h2, 4));

    // endian roundtrip
    uint8_t b8[8];
    util::PutU64LE(b8, 0x0123456789ABCDEFULL);
    CHECK_EQ(util::GetU64LE(b8), 0x0123456789ABCDEFULL);
    std::printf("OK\n");
}

// ===== Vektor uji kripto resmi =====
static void TestCryptoVectors() {
    std::printf("crypto vectors: ");
    using sec::Sha256;
    using sec::HmacSha256;
    using sec::Pbkdf2Sha256;

    // SHA-256 ("abc") — FIPS 180-4
    auto h1 = Sha256(reinterpret_cast<const uint8_t*>("abc"), 3);
    CHECK_EQ(util::HexEncode(h1.data(), 32),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));

    // SHA-256 string kosong
    auto h2 = Sha256(reinterpret_cast<const uint8_t*>(""), 0);
    CHECK_EQ(util::HexEncode(h2.data(), 32),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));

    // SHA-256 1.000.000x 'a' — diverifikasi silang dengan openssl dgst & hashlib Python
    {
        std::vector<uint8_t> big(1000000, 'a');
        auto h3 = Sha256(big.data(), big.size());
        CHECK_EQ(util::HexEncode(h3.data(), 32),
                 std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    }

    // HMAC-SHA256 — RFC 4231 test case 2 (key="Jefe", data="what do ya want for nothing?")
    {
        const char* key = "Jefe";
        const char* data = "what do ya want for nothing?";
        auto mac = HmacSha256(reinterpret_cast<const uint8_t*>(key), 4,
                              reinterpret_cast<const uint8_t*>(data), strlen(data));
        CHECK_EQ(util::HexEncode(mac.data(), 32),
                 std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    }

    // PBKDF2-HMAC-SHA256 — RFC 7914 §11 (P="password", S="salt")
    {
        auto d1 = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>("password"), 8,
                               reinterpret_cast<const uint8_t*>("salt"), 4, 1);
        CHECK_EQ(util::HexEncode(d1.data(), 32),
                 std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
        auto d2 = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>("password"), 8,
                               reinterpret_cast<const uint8_t*>("salt"), 4, 2);
        CHECK_EQ(util::HexEncode(d2.data(), 32),
                 std::string("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
        auto d3 = Pbkdf2Sha256(reinterpret_cast<const uint8_t*>("password"), 8,
                               reinterpret_cast<const uint8_t*>("salt"), 4, 4096);
        CHECK_EQ(util::HexEncode(d3.data(), 32),
                 std::string("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
    }
    std::printf("OK\n");
}

// ===== Auth proof =====
static void TestAuthProof() {
    std::printf("auth proof: ");
    uint8_t digest[32], sn[32], cn[32], cert[32];
    for (int i = 0; i < 32; ++i) {
        digest[i] = static_cast<uint8_t>(i);
        sn[i] = static_cast<uint8_t>(i + 32);
        cn[i] = static_cast<uint8_t>(i + 64);
        cert[i] = static_cast<uint8_t>(i + 96);
    }
    auto proof = sec::ComputeAuthProof(digest, sn, cn, 0x1234, cert);
    CHECK(sec::VerifyAuthProof(digest, proof.data(), sn, cn, 0x1234, cert));
    // sessionId beda → gagal
    CHECK(!sec::VerifyAuthProof(digest, proof.data(), sn, cn, 0x1235, cert));
    // cert beda → gagal
    uint8_t cert2[32];
    std::memcpy(cert2, cert, 32);
    cert2[0] ^= 1;
    CHECK(!sec::VerifyAuthProof(digest, proof.data(), sn, cn, 0x1234, cert2));
    // digest beda → gagal
    uint8_t digest2[32];
    std::memcpy(digest2, digest, 32);
    digest2[31] ^= 1;
    CHECK(!sec::VerifyAuthProof(digest2, proof.data(), sn, cn, 0x1234, cert));
    std::printf("OK\n");
}

// ===== Rate limiter =====
static void TestRateLimiter() {
    std::printf("rate limiter: ");
    int64_t clock = 0;
    sec::RateLimiterOptions o;
    o.maxFails = 3;
    o.windowMs = 60000;
    o.baseLockMs = 30000;
    o.maxLockMs = 120000;
    sec::RateLimiter lim(o, [&]() { return clock; });

    CHECK(lim.Allowed("1.2.3.4"));
    lim.Fail("1.2.3.4");
    lim.Fail("1.2.3.4");
    CHECK(lim.Allowed("1.2.3.4")); // 2 fail < 3
    lim.Fail("1.2.3.4");
    CHECK(!lim.Allowed("1.2.3.4")); // lockout
    clock += 20000;
    CHECK(!lim.Allowed("1.2.3.4")); // masih terkunci (30 s)
    clock += 15000; // total 35 s
    CHECK(lim.Allowed("1.2.3.4")); // lockout pertama lepas
    // lockout berikutnya eksponensial: 3 fail lagi → 60 s
    lim.Fail("1.2.3.4");
    lim.Fail("1.2.3.4");
    lim.Fail("1.2.3.4");
    CHECK(!lim.Allowed("1.2.3.4"));
    clock += 50000;
    CHECK(!lim.Allowed("1.2.3.4"));
    clock += 15000; // total 65 s
    CHECK(lim.Allowed("1.2.3.4"));

    // success reset
    lim.Success("1.2.3.4");
    CHECK(lim.Allowed("1.2.3.4"));

    // challenge budget
    for (int i = 0; i < 10; ++i) CHECK(lim.ChallengeAllowed("5.6.7.8"));
    CHECK(!lim.ChallengeAllowed("5.6.7.8"));
    clock += 61000;
    CHECK(lim.ChallengeAllowed("5.6.7.8"));
    std::printf("OK\n");
}

// ===== Device ID =====
static void TestDeviceId() {
    std::printf("device id: ");
    uint8_t raw[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::string id = sec::FormatDeviceId(raw);
    CHECK_EQ(id.size(), 14u); // LD- + 5 + - + 5
    CHECK_EQ(id.substr(0, 3), std::string("LD-"));
    CHECK_EQ(id.substr(8, 1), std::string("-"));
    // dua raw beda (dalam 6 byte pertama) → id beda
    uint8_t raw2[10] = {0, 1, 2, 3, 4, 7, 6, 7, 8, 9};
    CHECK_NE(id, sec::FormatDeviceId(raw2));
    // byte ke-7 dst tidak memengaruhi ID (hanya 6 byte pertama dipakai)
    uint8_t raw3[10] = {0, 1, 2, 3, 4, 5, 99, 99, 99, 99};
    CHECK_EQ(id, sec::FormatDeviceId(raw3));
    // compute (non-Windows pakai hostname stub; Windows pakai registry+volume)
    auto computed = sec::ComputeDeviceId();
    CHECK_EQ(computed.size(), 14u);
    auto again = sec::ComputeDeviceId();
    CHECK_EQ(computed, again); // stabil
    std::printf("OK\n");
}

// ===== Config =====
static void TestConfig() {
    std::printf("config: ");
    std::filesystem::path tmp = std::filesystem::temp_directory_path() /
                                ("litedesk-test-" + std::to_string(std::rand()));
    {
        util::Config::I().SetBaseDir(tmp.wstring());
        util::Config::I().Load();
        CHECK_EQ(util::Config::I().GetInt("network.tcp_port", 0), 48500);
        CHECK(util::Config::I().GetBool("security.require_confirmation", false));
        util::Config::I().SetInt("network.tcp_port", 50000);
        util::Config::I().SetStrArray("security.auto_accept_devices", {"LD-A", "LD-B"});
        CHECK(util::Config::I().Save());

        // re-load dari file
        util::Config::I().SetBaseDir(tmp.wstring());
        // loaded_ masih true; reset lewat instance baru tak bisa — pakai trik: baca file langsung
        std::string content;
        {
            std::ifstream f(tmp / "config.json");
            std::stringstream ss;
            ss << f.rdbuf();
            content = ss.str();
        }
        auto v = util::json::Parse(content);
        CHECK(v && v->Find("network") && v->Find("network")->Find("tcp_port")->AsInt() == 50000);
        CHECK(v->Find("security")->Find("auto_accept_devices")->arr.size() == 2);
    }
    std::filesystem::remove_all(tmp);
    std::printf("OK\n");
}

int main() {
    util::Logger::Instance().SetConsole(true);
    util::Logger::Instance().SetLevel(util::LogLevel::Error);
    TestFraming();
    TestMessages();
    TestJson();
    TestCodec();
    TestCryptoVectors();
    TestAuthProof();
    TestRateLimiter();
    TestDeviceId();
    TestConfig();
    std::printf("\n%d lolos, %d gagal\n", gPassed, gFailed);
    if (gFailed > 0) {
        std::printf("HASIL: GAGAL\n");
        return 1;
    }
    std::printf("HASIL: SEMUA TEST LOLOS\n");
    return 0;
}
