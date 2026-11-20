// net.h — TCP socket/listener dengan timeout, discovery UDP, utilitas jaringan.
// Windows-only (Winsock2). Socket blocking + timeout berbasis select — cukup untuk
// jumlah koneksi kecil dan jauh lebih mudah dibuktikan benar daripada IOCP.
#pragma once
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// windows.h telah diinclude oleh file yang menggunakan header ini (semua .cpp win-only).
#include <winsock2.h>
#include <ws2tcpip.h>

namespace ldr::net {

// RAII WSAStartup berhitungan referensi (instansikan sekali di main / test).
struct WsaInit {
    WsaInit();
    ~WsaInit();
};

class TcpSocket {
public:
    TcpSocket() = default;
    TcpSocket(SOCKET s, uint32_t peerIp, uint16_t peerPort);
    TcpSocket(TcpSocket&& o) noexcept;
    TcpSocket& operator=(TcpSocket&& o) noexcept;
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    ~TcpSocket();

    static bool Connect(TcpSocket& out, const std::string& ip, uint16_t port,
                        int timeoutMs, std::string* err);

    bool Valid() const { return s_ != INVALID_SOCKET; }
    SOCKET Handle() const { return s_; }
    std::string PeerIp() const { return peerIp_; }
    uint16_t PeerPort() const { return peerPort_; }

    // >0: byte; 0: koneksi ditutup; -1: error/timeout (cek err).
    int Read(uint8_t* dst, size_t cap, std::string* err = nullptr);
    bool WriteAll(const uint8_t* src, size_t n, std::string* err = nullptr);
    bool WaitReadable(int ms);
    void SetNoDelay();
    void SetTimeouts(int ms); // SO_RCVTIMEO / SO_SNDTIMEO
    void Shutdown();
    void Close();

private:
    SOCKET s_ = INVALID_SOCKET;
    std::string peerIp_;
    uint16_t peerPort_ = 0;
};

class TcpListener {
public:
    bool Listen(const std::string& bindIp, uint16_t port, std::string* err);
    // Menunggu koneksi masuk maks waitMs. true bila ada koneksi baru di out.
    bool Accept(TcpSocket& out, int waitMs, std::string* err);
    void Stop();
    bool Listening() const { return listening_; }
    uint16_t ActualPort() const { return port_; }

private:
    SOCKET s_ = INVALID_SOCKET;
    uint16_t port_ = 0;
    bool listening_ = false;
    bool WaitReadableInternal(int ms);
};

// ===== Discovery UDP (query/reply, port default 48501) =====

struct DeviceAnnounce {
    std::string deviceId;
    std::string hostname;
    std::string ip;
    uint16_t tcpPort = 0;
    int64_t tsMs = 0;
};

namespace disc {
constexpr uint16_t kDefaultPort = 48501;
constexpr uint32_t kMagic = 0x4C445044u; // "LDPD"
constexpr uint16_t kTypeQuery = 1;
constexpr uint16_t kTypeReply = 2;

std::vector<uint8_t> BuildQuery();
std::vector<uint8_t> BuildReply(const std::string& deviceId, const std::string& hostname,
                                uint16_t tcpPort);
// isQuery → type. out diisi hanya untuk reply.
bool Parse(const uint8_t* data, size_t len, bool* isQuery, DeviceAnnounce* out);
} // namespace disc

// Host: menjawab query discovery (throttle 1 reply / 300 ms / IP).
class DiscoveryResponder {
public:
    ~DiscoveryResponder();
    bool Start(uint16_t port, const std::string& deviceId, const std::string& hostname,
               uint16_t tcpPort, std::string* err);
    void Stop();
    bool Running() const { return running_; }

private:
    void Loop();
    SOCKET s_ = INVALID_SOCKET;
    std::string deviceId_, hostname_;
    uint16_t tcpPort_ = 0;
    bool running_ = false;
};

// Viewer: query broadcast + kumpulkan reply (dipanggil on-demand / periodik).
class DiscoveryWatcher {
public:
    ~DiscoveryWatcher();
    bool Start(uint16_t port, std::string* err);
    void Stop();
    // Kirim query broadcast dan tunggu replies timeoutMs, gabungkan ke cache.
    void RefreshNow(int timeoutMs = 1500);
    std::vector<DeviceAnnounce> Snapshot() const;
    // Resolver untuk ViewerSession: deviceId → (ip, port)
    bool Resolve(const std::string& deviceId, std::string* ip, uint16_t* port);

private:
    mutable std::mutex mu_;
    std::vector<DeviceAnnounce> cache_;
    SOCKET s_ = INVALID_SOCKET;
    uint16_t port_ = disc::kDefaultPort;
    bool running_ = false;
    bool WaitReadableUdp(int ms);
};

std::vector<std::string> MyIps();
std::string HostNameUtf8();

} // namespace ldr::net
