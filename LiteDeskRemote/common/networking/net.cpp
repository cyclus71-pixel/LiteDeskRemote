// net.cpp — implementasi Winsock: TCP connect/accept dengan timeout, discovery UDP.
#include "net.h"
#include "../util/codec.h"
#include "../util/log.h"
#include "../util/time.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iphlpapi.h>
#include <thread>

#include <algorithm>
#include <map>

namespace ldr::net {

namespace {

std::string LastWinsockError(const char* ctx) {
    wchar_t buf[256] = {0};
    DWORD e = WSAGetLastError();
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   e, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, 255, nullptr);
    std::wstring w(buf);
    std::string msg(w.begin(), w.end());
    LLOG_E("%s gagal (WSA err %lu): %s", ctx, static_cast<unsigned long>(e), msg.c_str());
    return ctx + std::string(" gagal (err ") + std::to_string(e) + ")";
}

std::string IpToString(uint32_t ipNet) {
    char buf[INET_ADDRSTRLEN] = {0};
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = ipNet;
    inet_ntop(AF_INET, &sa.sin_addr, buf, sizeof(buf));
    return buf;
}

} // namespace

// ===== WsaInit =====
namespace {
int gWsaRefs = 0;
} // namespace

WsaInit::WsaInit() {
    if (gWsaRefs++ == 0) {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) {
            LLOG_E("WSAStartup gagal");
        }
    }
}
WsaInit::~WsaInit() {
    if (--gWsaRefs == 0) WSACleanup();
}

// ===== TcpSocket =====
TcpSocket::TcpSocket(SOCKET s, uint32_t peerIp, uint16_t peerPort)
    : s_(s), peerIp_(IpToString(peerIp)), peerPort_(peerPort) {}

TcpSocket::TcpSocket(TcpSocket&& o) noexcept { *this = std::move(o); }
TcpSocket& TcpSocket::operator=(TcpSocket&& o) noexcept {
    if (this != &o) {
        Close();
        s_ = o.s_;
        peerIp_ = std::move(o.peerIp_);
        peerPort_ = o.peerPort_;
        o.s_ = INVALID_SOCKET;
    }
    return *this;
}
TcpSocket::~TcpSocket() { Close(); }

void TcpSocket::Close() {
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

void TcpSocket::Shutdown() {
    if (s_ != INVALID_SOCKET) shutdown(s_, SD_BOTH);
}

void TcpSocket::SetNoDelay() {
    BOOL one = TRUE;
    setsockopt(s_, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
}

void TcpSocket::SetTimeouts(int ms) {
    DWORD t = static_cast<DWORD>(ms);
    setsockopt(s_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    setsockopt(s_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
}

bool TcpSocket::Connect(TcpSocket& out, const std::string& ip, uint16_t port,
                        int timeoutMs, std::string* err) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &sa.sin_addr) != 1) {
        if (err) *err = "alamat IP tidak valid: " + ip;
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { if (err) *err = LastWinsockError("socket()"); return false; }

    // connect non-blocking + select → timeout terkendali
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    int rc = connect(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        if (err) *err = "tidak bisa tersambung ke " + ip + ":" + std::to_string(port);
        return false;
    }
    fd_set wset, eset;
    FD_ZERO(&wset); FD_ZERO(&eset);
    FD_SET(s, &wset); FD_SET(s, &eset);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    rc = select(0, nullptr, &wset, &eset, &tv);
    if (rc <= 0) {
        closesocket(s);
        if (err) *err = "timeout menyambung ke " + ip + ":" + std::to_string(port);
        return false;
    }
    int soErr = 0;
    int len = sizeof(soErr);
    getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &len);
    if (soErr != 0) {
        closesocket(s);
        if (err) *err = "koneksi ke " + ip + ":" + std::to_string(port) + " gagal";
        return false;
    }
    u_long blocking = 0;
    ioctlsocket(s, FIONBIO, &blocking);

    out.Close();
    out = TcpSocket(s, sa.sin_addr.s_addr, port);
    out.SetNoDelay();
    out.SetTimeouts(15000);
    return true;
}

bool TcpSocket::WaitReadable(int ms) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s_, &rset);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &rset, nullptr, nullptr, &tv) > 0;
}

int TcpSocket::Read(uint8_t* dst, size_t cap, std::string* err) {
    int n = recv(s_, reinterpret_cast<char*>(dst), static_cast<int>(cap), 0);
    if (n > 0) return n;
    if (n == 0) return 0; // clean EOF
    int e = WSAGetLastError();
    if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) {
        if (err) *err = "timeout";
        return -1;
    }
    if (e == WSAECONNRESET || e == WSAECONNABORTED || e == WSAESHUTDOWN) return 0;
    if (err) *err = LastWinsockError("recv()");
    return -1;
}

bool TcpSocket::WriteAll(const uint8_t* src, size_t n, std::string* err) {
    size_t sent = 0;
    while (sent < n) {
        int chunk = static_cast<int>((n - sent > 64 * 1024) ? 64 * 1024 : n - sent);
        int r = send(s_, reinterpret_cast<const char*>(src + sent), chunk, 0);
        if (r == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e == WSAEWOULDBLOCK || e == WSAETIMEDOUT) {
                if (err) *err = "timeout mengirim";
                return false;
            }
            if (err) *err = LastWinsockError("send()");
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

// ===== TcpListener =====
bool TcpListener::Listen(const std::string& bindIp, uint16_t port, std::string* err) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr =
        (bindIp.empty() || bindIp == "0.0.0.0") ? INADDR_ANY : inet_addr(bindIp.c_str());

    s_ = socket(AF_INET, SOCK_STREAM, 0);
    if (s_ == INVALID_SOCKET) { if (err) *err = LastWinsockError("socket()"); return false; }
    BOOL one = TRUE;
    setsockopt(s_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    if (bind(s_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SOCKET_ERROR) {
        if (err) *err = "port " + std::to_string(port) + " tidak bisa di-bind (mungkin dipakai aplikasi lain)";
        closesocket(s_);
        s_ = INVALID_SOCKET;
        return false;
    }
    if (listen(s_, 8) == SOCKET_ERROR) {
        if (err) *err = LastWinsockError("listen()");
        closesocket(s_);
        s_ = INVALID_SOCKET;
        return false;
    }
    int len = sizeof(sa);
    getsockname(s_, reinterpret_cast<sockaddr*>(&sa), &len);
    port_ = ntohs(sa.sin_port);
    listening_ = true;
    return true;
}

bool TcpListener::Accept(TcpSocket& out, int waitMs, std::string* err) {
    if (!listening_) return false;
    if (!WaitReadableInternal(waitMs)) return false; // false = timeout / stop
    sockaddr_in sa{};
    int len = sizeof(sa);
    SOCKET c = accept(s_, reinterpret_cast<sockaddr*>(&sa), &len);
    if (c == INVALID_SOCKET) {
        if (err) *err = LastWinsockError("accept()");
        return false;
    }
    out.Close();
    out = TcpSocket(c, sa.sin_addr.s_addr, ntohs(sa.sin_port));
    out.SetNoDelay();
    out.SetTimeouts(15000);
    return true;
}

void TcpListener::Stop() {
    listening_ = false;
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

bool TcpListener::WaitReadableInternal(int ms) {
    if (s_ == INVALID_SOCKET) return false;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s_, &rset);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &rset, nullptr, nullptr, &tv) > 0;
}

// ===== Discovery =====
namespace disc {

std::vector<uint8_t> BuildQuery() {
    std::vector<uint8_t> out;
    util::AppendU32(out, kMagic);
    util::AppendU16(out, 1);  // version
    util::AppendU16(out, kTypeQuery);
    util::AppendU16(out, 0);  // tcpPort (tidak dipakai query)
    util::AppendU64(out, static_cast<uint64_t>(util::WallNowMs()));
    out.push_back(0); // deviceId len 0
    out.push_back(0); // hostname len 0
    return out;
}

std::vector<uint8_t> BuildReply(const std::string& deviceId, const std::string& hostname,
                                uint16_t tcpPort) {
    std::vector<uint8_t> out;
    util::AppendU32(out, kMagic);
    util::AppendU16(out, 1);
    util::AppendU16(out, kTypeReply);
    util::AppendU16(out, tcpPort);
    util::AppendU64(out, static_cast<uint64_t>(util::WallNowMs()));
    uint8_t dl = static_cast<uint8_t>(deviceId.size() > 39 ? 39 : deviceId.size());
    out.push_back(dl);
    util::AppendBytes(out, deviceId.data(), dl);
    uint8_t hl = static_cast<uint8_t>(hostname.size() > 99 ? 99 : hostname.size());
    out.push_back(hl);
    util::AppendBytes(out, hostname.data(), hl);
    return out;
}

bool Parse(const uint8_t* data, size_t len, bool* isQuery, DeviceAnnounce* out) {
    if (len < 18 || len > 512) return false;
    if (util::GetU32LE(data) != kMagic) return false;
    if (util::GetU16LE(data + 4) != 1) return false;
    uint16_t type = util::GetU16LE(data + 6);
    if (type != kTypeQuery && type != kTypeReply) return false;
    *isQuery = (type == kTypeQuery);
    if (out) {
        out->tcpPort = util::GetU16LE(data + 8);
        // tsSec di offset 10 (8 byte) — tidak dipakai untuk logika, hanya debug
        size_t off = 18;
        uint8_t dl = data[off++];
        if (off + dl > len) return false;
        out->deviceId.assign(reinterpret_cast<const char*>(data + off), dl);
        off += dl;
        uint8_t hl = data[off++];
        if (off + hl > len) return false;
        out->hostname.assign(reinterpret_cast<const char*>(data + off), hl);
    }
    return true;
}

} // namespace disc

DiscoveryResponder::~DiscoveryResponder() { Stop(); }

bool DiscoveryResponder::Start(uint16_t port, const std::string& deviceId,
                               const std::string& hostname, uint16_t tcpPort,
                               std::string* err) {
    Stop();
    s_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_ == INVALID_SOCKET) { if (err) *err = LastWinsockError("udp socket()"); return false; }
    BOOL one = TRUE;
    setsockopt(s_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&one), sizeof(one));
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = INADDR_ANY;
    if (bind(s_, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) == SOCKET_ERROR) {
        if (err) *err = "port discovery UDP " + std::to_string(port) + " tidak bisa di-bind";
        closesocket(s_);
        s_ = INVALID_SOCKET;
        return false;
    }
    deviceId_ = deviceId;
    hostname_ = hostname;
    tcpPort_ = tcpPort;
    running_ = true;
    std::thread(&DiscoveryResponder::Loop, this).detach();
    return true;
}

void DiscoveryResponder::Loop() {
    std::map<uint32_t, int64_t> lastReply; // ipNet → wall ms (throttle)
    uint8_t buf[600];
    while (running_) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(s_, &rset);
        timeval tv{1, 0};
        int rc = select(0, &rset, nullptr, nullptr, &tv);
        if (rc <= 0) continue; // timeout → cek running_
        sockaddr_in src{};
        int slen = sizeof(src);
        int n = recvfrom(s_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&src), &slen);
        if (n <= 0) continue;
        bool isQuery = false;
        DeviceAnnounce a;
        if (!disc::Parse(buf, static_cast<size_t>(n), &isQuery, &a) || !isQuery) continue;

        int64_t now = util::WallNowMs();
        auto it = lastReply.find(src.sin_addr.s_addr);
        if (it != lastReply.end() && now - it->second < 300) continue; // anti amplifikasi
        lastReply[src.sin_addr.s_addr] = now;

        auto reply = disc::BuildReply(deviceId_, hostname_, tcpPort_);
        sendto(s_, reinterpret_cast<const char*>(reply.data()), static_cast<int>(reply.size()), 0,
               reinterpret_cast<sockaddr*>(&src), slen);
    }
}

void DiscoveryResponder::Stop() {
    running_ = false;
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

DiscoveryWatcher::~DiscoveryWatcher() { Stop(); }

bool DiscoveryWatcher::Start(uint16_t port, std::string* err) {
    port_ = port;
    s_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_ == INVALID_SOCKET) { if (err) *err = LastWinsockError("udp socket()"); return false; }
    BOOL one = TRUE;
    setsockopt(s_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));
    running_ = true;
    return true;
}

void DiscoveryWatcher::Stop() {
    running_ = false;
    if (s_ != INVALID_SOCKET) {
        closesocket(s_);
        s_ = INVALID_SOCKET;
    }
}

void DiscoveryWatcher::RefreshNow(int timeoutMs) {
    if (!running_) return;
    auto query = disc::BuildQuery();
    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(port_);
    bcast.sin_addr.s_addr = INADDR_BROADCAST; // 255.255.255.255
    sendto(s_, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
           reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));
    // Loopback agar temuan mesin lokal juga terdeteksi (host & viewer satu mesin).
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port_);
    local.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(s_, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
           reinterpret_cast<sockaddr*>(&local), sizeof(local));

    int64_t deadline = util::NowMs() + timeoutMs;
    uint8_t buf[600];
    std::vector<DeviceAnnounce> found;
    while (util::NowMs() < deadline) {
        if (!WaitReadableUdp(100)) continue;
        sockaddr_in src{};
        int slen = sizeof(src);
        int n = recvfrom(s_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                         reinterpret_cast<sockaddr*>(&src), &slen);
        if (n <= 0) continue;
        bool isQuery = false;
        DeviceAnnounce a;
        if (!disc::Parse(buf, static_cast<size_t>(n), &isQuery, &a) || isQuery) continue;
        a.ip = IpToString(src.sin_addr.s_addr);
        a.tsMs = util::WallNowMs();
        found.push_back(std::move(a));
    }
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& f : found) {
        bool dup = false;
        for (auto& c : cache_)
            if (c.deviceId == f.deviceId) { c = f; dup = true; break; }
        if (!dup) cache_.push_back(std::move(f));
    }
    // Buang entri lebih tua dari 60 s.
    int64_t now = util::WallNowMs();
    cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                [&](const DeviceAnnounce& d) { return now - d.tsMs > 60000; }),
                 cache_.end());
}

std::vector<DeviceAnnounce> DiscoveryWatcher::Snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    return cache_;
}

bool DiscoveryWatcher::Resolve(const std::string& deviceId, std::string* ip, uint16_t* port) {
    auto snap = Snapshot();
    for (const auto& d : snap)
        if (d.deviceId == deviceId) { *ip = d.ip; *port = d.tcpPort; return true; }
    return false;
}

bool DiscoveryWatcher::WaitReadableUdp(int ms) {
    if (s_ == INVALID_SOCKET) return false;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s_, &rset);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &rset, nullptr, nullptr, &tv) > 0;
}

// ===== Utilitas =====
std::vector<std::string> MyIps() {
    std::vector<std::string> out;
    ULONG size = 15 * 1024;
    std::vector<uint8_t> buf(size);
    auto* table = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                         GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, table, &size) != NO_ERROR)
        return out;
    for (auto* ad = table; ad; ad = ad->Next) {
        if (ad->OperStatus != IfOperStatusUp) continue;
        if (ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* ua = ad->FirstUnicastAddress; ua; ua = ua->Next) {
            char bufIp[INET_ADDRSTRLEN] = {0};
            if (ua->Address.lpSockaddr->sa_family == AF_INET) {
                auto* sa = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                inet_ntop(AF_INET, &sa->sin_addr, bufIp, sizeof(bufIp));
                out.push_back(bufIp);
            }
        }
    }
    return out;
}

std::string HostNameUtf8() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {0};
    DWORD n = MAX_COMPUTERNAME_LENGTH + 1;
    if (!GetComputerNameW(buf, &n)) return "";
    return std::string(buf, buf + n);
}

} // namespace ldr::net
