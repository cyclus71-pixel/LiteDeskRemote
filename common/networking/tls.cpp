// tls.cpp — implementasi Schannel: handshake client/server, enkripsi/dekripsi record,
// penanganan EXTRA/INCOMPLETE/RENEGOTIATE. Pola implementasi mengikuti praktik standar
// SSPI (InitializeSecurityContext/AcceptSecurityContext streaming + QueryContextAttributes).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif

#include "tls.h"
#include "../security/crypto_win.h"
#include "../util/codec.h"
#include "../util/log.h"
#include "../util/time.h"

#include <cstring>
#include <mutex>
#include <vector>

#include <windows.h>
#include <wincrypt.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>

// Kompatibilitas nama flag antar SDK: MS SDK = *_ALLOCATED_MEMORY, mingw-w64 = *_ALLOCATE_MEMORY.
#ifndef ISC_REQ_ALLOCATED_MEMORY
#ifdef ISC_REQ_ALLOCATE_MEMORY
#define ISC_REQ_ALLOCATED_MEMORY ISC_REQ_ALLOCATE_MEMORY
#else
#define ISC_REQ_ALLOCATED_MEMORY 0x00000100
#endif
#endif

#ifndef ASC_REQ_ALLOCATED_MEMORY
#ifdef ASC_REQ_ALLOCATE_MEMORY
#define ASC_REQ_ALLOCATED_MEMORY ASC_REQ_ALLOCATE_MEMORY
#else
#define ASC_REQ_ALLOCATED_MEMORY 0x00000100
#endif
#endif

// Di Windows SDK (MSVC), field _SCHANNEL_CRED adalah `paCred` dan fungsi SSPI-nya `AcceptSecurityContext`.
#if defined(_MSC_VER)
#define LDR_SC_PA_CREDS paCred
#define LDR_AcceptSecurityContext AcceptSecurityContext
#elif defined(__MINGW32__)
#define LDR_SC_PA_CREDS paCred
#define LDR_AcceptSecurityContext AcceptSecurityContext
#else
#define LDR_SC_PA_CREDS paCreds
#define LDR_AcceptSecurityContext AcceptSecurityContextW
#endif

namespace ldr::net {

namespace {
constexpr DWORD kClientReqFlags =
    ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
    ISC_REQ_ALLOCATED_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION |
    ISC_REQ_USE_SUPPLIED_CREDS;
constexpr DWORD kServerReqFlags =
    ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
    ASC_REQ_ALLOCATED_MEMORY | ASC_REQ_STREAM;

// Helper socket (dipakai TlsChannel; TcpSocket milik pemanggil handshake).
bool WaitReadableSocket(SOCKET s, int ms) {
    if (s == INVALID_SOCKET) return false;
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(s, &rset);
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return select(0, &rset, nullptr, nullptr, &tv) > 0;
}

bool TcpWriteAll(SOCKET s, const uint8_t* src, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        int chunk = static_cast<int>((n - sent > 64 * 1024) ? 64 * 1024 : n - sent);
        int r = send(s, reinterpret_cast<const char*>(src + sent), chunk, 0);
        if (r == SOCKET_ERROR) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}
} // namespace

// ===== Credential =====
void CredHandleDeleter::operator()(CredHandle* h) const {
    if (h) {
        if (h->dwLower != 0 || h->dwUpper != 0) FreeCredentialsHandle(h);
        delete h;
    }
}

CredPtr MakeServerCred(PCCERT_CONTEXT cert, std::string* err) {
    SCHANNEL_CRED sc{};
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    sc.cCreds = 1;
    sc.LDR_SC_PA_CREDS = const_cast<PCCERT_CONTEXT*>(&cert);
    // grfEnabledProtocols = 0 -> default OS (Win10 1903+/11: TLS 1.2 + 1.3, lama nonaktif).
    CredHandle* h = new CredHandle{};
    TimeStamp exp{};
    wchar_t pkgName[] = UNISP_NAME_W;
    SECURITY_STATUS st = AcquireCredentialsHandleW(nullptr, pkgName, SECPKG_CRED_INBOUND,
                                                   nullptr, &sc, nullptr, nullptr, h, &exp);
    if (st != SEC_E_OK) {
        delete h;
        if (err) *err = "AcquireCredentialsHandle(server) gagal (0x" +
                        std::to_string(static_cast<unsigned long>(st)) + ")";
        return nullptr;
    }
    return CredPtr(h);
}

CredPtr MakeClientCred(std::string* err) {
    SCHANNEL_CRED sc{};
    sc.dwVersion = SCHANNEL_CRED_VERSION;
    // Tanpa flag revocation = tidak ada revocation check (offline-friendly).
    sc.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION |
                 SCH_CRED_NO_SERVERNAME_CHECK | SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                 SCH_CRED_IGNORE_REVOCATION_OFFLINE;
    CredHandle* h = new CredHandle{};
    TimeStamp exp{};
    wchar_t pkgName[] = UNISP_NAME_W;
    SECURITY_STATUS st = AcquireCredentialsHandleW(nullptr, pkgName, SECPKG_CRED_OUTBOUND,
                                                   nullptr, &sc, nullptr, nullptr, h, &exp);
    if (st != SEC_E_OK) {
        delete h;
        if (err) *err = "AcquireCredentialsHandle(client) gagal";
        return nullptr;
    }
    return CredPtr(h);
}

// ===== TlsChannel =====
TlsChannel::~TlsChannel() { Close(); }

void TlsChannel::Close() {
    if (ctxOpen_) {
        DeleteSecurityContext(&ctx_);
        ctxOpen_ = false;
    }
    firstCall_ = true;
    active_ = false;
    rawIn_.clear();
    plain_.clear();
}

void TlsChannel::CloseNotify() {
    // Best-effort shutdown: token SCHANNEL_SHUTDOWN lalu kirim token akhir bila ada.
    if (!ctxOpen_) { Close(); return; }
    BYTE tokenBuf[64];
    SecBufferDesc outDesc{SECBUFFER_VERSION, 1, nullptr};
    SecBuffer outTok{0, SECBUFFER_TOKEN, tokenBuf};
    SecBuffer bufs[1] = {outTok};
    outDesc.pBuffers = bufs;
    BYTE shutdownToken[4] = {0x01, 0x00, 0x00, 0x00};
    SecBufferDesc inDesc{SECBUFFER_VERSION, 1, nullptr};
    SecBuffer inBuf{4, SECBUFFER_TOKEN, shutdownToken};
    SecBuffer ib[1] = {inBuf};
    inDesc.pBuffers = ib;
    if (ApplyControlToken(&ctx_, &inDesc) == SEC_E_OK) {
        DWORD attrs = 0;
        SECURITY_STATUS st;
        if (firstCall_) {
            st = InitializeSecurityContextW(cred_, nullptr, nullptr, kClientReqFlags, 0, 0,
                                            nullptr, 0, &ctx_, &outDesc, &attrs, nullptr);
        } else {
            st = InitializeSecurityContextW(cred_, &ctx_, nullptr, kClientReqFlags, 0, 0,
                                            nullptr, 0, &ctx_, &outDesc, &attrs, nullptr);
        }
        (void)st;
        if (outTok.cbBuffer > 0 && tokenBuf[0]) {
            int n = send(s_, reinterpret_cast<const char*>(outTok.pvBuffer), outTok.cbBuffer, 0);
            (void)n;
        }
    }
    Close();
}

bool TlsChannel::RecvMoreRaw(int timeoutMs) {
    if (!WaitReadableSocket(s_, timeoutMs)) {
        err_ = "timeout menunggu data TLS";
        return false;
    }
    uint8_t buf[16 * 1024];
    int n = recv(s_, reinterpret_cast<char*>(buf), sizeof(buf), 0);
    if (n == 0) { err_ = "koneksi ditutup peer (EOF)"; return false; }
    if (n < 0) { err_ = "gagal menerima data TLS"; return false; }
    rawIn_.insert(rawIn_.end(), buf, buf + n);
    return true;
}

bool TlsChannel::Send(const uint8_t* src, size_t n, std::string* err) {
    if (!active_) { if (err) *err = "TLS belum aktif"; return false; }
    std::lock_guard<std::mutex> lk(sendMu_);
    size_t off = 0;
    size_t maxMsg = sizes_.cbMaximumMessage > 0 ? sizes_.cbMaximumMessage : 16 * 1024;
    while (off < n) {
        size_t chunk = (n - off < maxMsg) ? (n - off) : maxMsg;
        std::vector<uint8_t> record(sizes_.cbHeader + chunk + sizes_.cbTrailer);
        std::memcpy(record.data() + sizes_.cbHeader, src + off, chunk);
        SecBuffer bufs[4];
        bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
        bufs[0].cbBuffer = sizes_.cbHeader;
        bufs[0].pvBuffer = record.data();
        bufs[1].BufferType = SECBUFFER_DATA;
        bufs[1].cbBuffer = static_cast<ULONG>(chunk);
        bufs[1].pvBuffer = record.data() + sizes_.cbHeader;
        bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
        bufs[2].cbBuffer = sizes_.cbTrailer;
        bufs[2].pvBuffer = record.data() + sizes_.cbHeader + chunk;
        bufs[3].BufferType = SECBUFFER_EMPTY;
        bufs[3].cbBuffer = 0;
        bufs[3].pvBuffer = nullptr;
        SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
        SECURITY_STATUS st = EncryptMessage(&ctx_, 0, &desc, 0);
        if (st != SEC_E_OK) { if (err) *err = "EncryptMessage gagal"; return false; }
        size_t total = sizes_.cbHeader + chunk + bufs[2].cbBuffer;
        if (!TcpWriteAll(s_, record.data(), total)) {
            if (err) *err = "gagal mengirim record TLS";
            return false;
        }
        off += chunk;
    }
    return true;
}

int TlsChannel::Recv(uint8_t* dst, size_t cap, std::string* err) {
    if (!active_) { if (err) *err = "TLS belum aktif"; return -1; }
    if (!plain_.empty()) {
        size_t n = plain_.size() < cap ? plain_.size() : cap;
        std::memcpy(dst, plain_.data(), n);
        plain_.erase(plain_.begin(), plain_.begin() + static_cast<long>(n));
        return static_cast<int>(n);
    }
    while (true) {
        if (!DecryptPending()) {
            if (err) *err = err_;
            return err_.find("ditutup") != std::string::npos ? 0 : -1;
        }
        if (!plain_.empty()) {
            size_t n = plain_.size() < cap ? plain_.size() : cap;
            std::memcpy(dst, plain_.data(), n);
            plain_.erase(plain_.begin(), plain_.begin() + static_cast<long>(n));
            return static_cast<int>(n);
        }
        if (!RecvMoreRaw(15000)) {
            if (err) *err = err_;
            return 0;
        }
    }
}

bool TlsChannel::DecryptPending() {
    if (rawIn_.empty()) return true;
    SecBuffer bufs[4];
    for (auto& b : bufs) {
        b.cbBuffer = static_cast<ULONG>(rawIn_.size());
        b.BufferType = SECBUFFER_DATA;
        b.pvBuffer = rawIn_.data();
    }
    SecBufferDesc desc{SECBUFFER_VERSION, 4, bufs};
    ULONG qop = 0;
    SECURITY_STATUS st = DecryptMessage(&ctx_, &desc, 0, &qop);

    if (st == SEC_E_INCOMPLETE_MESSAGE) return true;

    if (st == SEC_I_CONTEXT_EXPIRED) {
        for (ULONG i = 0; i < 4; ++i) {
            if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0)
                plain_.insert(plain_.end(), static_cast<uint8_t*>(bufs[i].pvBuffer),
                              static_cast<uint8_t*>(bufs[i].pvBuffer) + bufs[i].cbBuffer);
        }
        active_ = false;
        err_ = "koneksi ditutup peer (TLS expired)";
        return false;
    }

    if (st == SEC_I_RENEGOTIATE) {
        for (ULONG i = 0; i < 4; ++i) {
            if (bufs[i].BufferType == SECBUFFER_TOKEN && bufs[i].cbBuffer > 0) {
                std::vector<uint8_t> tok(static_cast<uint8_t*>(bufs[i].pvBuffer),
                                         static_cast<uint8_t*>(bufs[i].pvBuffer) + bufs[i].cbBuffer);
                rawIn_.assign(tok.begin(), tok.end());
                std::string e2;
                auto fn = isClient_ ? &TlsChannel::ClientStep : &TlsChannel::ServerStep;
                if (!RunHandshake(fn, 15000, &e2)) {
                    err_ = "renegotiasi gagal: " + e2;
                    active_ = false;
                    return false;
                }
                return true;
            }
        }
        err_ = "renegotiasi tanpa token";
        active_ = false;
        return false;
    }

    if (st != SEC_E_OK) {
        err_ = "DecryptMessage gagal (status 0x" + std::to_string(static_cast<unsigned long>(st)) + ")";
        active_ = false;
        return false;
    }

    std::vector<uint8_t> dataPart, extraPart;
    for (ULONG i = 0; i < 4; ++i) {
        if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].cbBuffer > 0) {
            dataPart.assign(static_cast<uint8_t*>(bufs[i].pvBuffer),
                            static_cast<uint8_t*>(bufs[i].pvBuffer) + bufs[i].cbBuffer);
        }
        if (bufs[i].BufferType == SECBUFFER_EXTRA && bufs[i].cbBuffer > 0) {
            extraPart.assign(static_cast<uint8_t*>(bufs[i].pvBuffer),
                             static_cast<uint8_t*>(bufs[i].pvBuffer) + bufs[i].cbBuffer);
        }
    }
    plain_.insert(plain_.end(), dataPart.begin(), dataPart.end());
    rawIn_.swap(extraPart);
    return true;
}

SECURITY_STATUS TlsChannel::ClientStep(SecBufferDesc* in, SecBufferDesc* out,
                                       DWORD* attrs, bool* first) {
    if (*first) {
        *first = false;
        return InitializeSecurityContextW(cred_, nullptr, nullptr, kClientReqFlags, 0, 0,
                                          nullptr, 0, &ctx_, out, attrs, nullptr);
    }
    return InitializeSecurityContextW(cred_, &ctx_, nullptr, kClientReqFlags, 0, 0,
                                      in, 0, &ctx_, out, attrs, nullptr);
}

SECURITY_STATUS TlsChannel::ServerStep(SecBufferDesc* in, SecBufferDesc* out,
                                       DWORD* attrs, bool* first) {
    if (*first) {
        *first = false;
        return LDR_AcceptSecurityContext(cred_, nullptr, in, kServerReqFlags, 0,
                                         &ctx_, out, attrs, nullptr);
    }
    return LDR_AcceptSecurityContext(cred_, &ctx_, in, kServerReqFlags, 0,
                                     &ctx_, out, attrs, nullptr);
}

bool TlsChannel::RunHandshake(SECURITY_STATUS (TlsChannel::*fn)(SecBufferDesc*, SecBufferDesc*,
                                                                DWORD*, bool*),
                              int timeoutMs, std::string* err) {
    firstCall_ = true;
    bool done = false;
    int guard = 0;
    while (!done) {
        if (++guard > 16) { if (err) *err = "handshake terlalu banyak langkah"; return false; }

        SecBuffer inBufs[2] = {
            {static_cast<ULONG>(rawIn_.size()), SECBUFFER_TOKEN,
             rawIn_.empty() ? nullptr : rawIn_.data()},
            {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc inDesc{SECBUFFER_VERSION, 2, inBufs};
        SecBuffer outBufs[1] = {{0, SECBUFFER_TOKEN, nullptr}};
        SecBufferDesc outDesc{SECBUFFER_VERSION, 1, outBufs};
        DWORD attrs = 0;
        SECURITY_STATUS st = (this->*fn)(&inDesc, &outDesc, &attrs, &firstCall_);

        if (st == SEC_E_INCOMPLETE_MESSAGE) {
            if (!RecvMoreRaw(timeoutMs)) { if (err) *err = err_; return false; }
            continue;
        }

        if (outBufs[0].cbBuffer > 0 && outBufs[0].pvBuffer) {
            std::vector<uint8_t> tok(static_cast<uint8_t*>(outBufs[0].pvBuffer),
                                     static_cast<uint8_t*>(outBufs[0].pvBuffer) + outBufs[0].cbBuffer);
            FreeContextBuffer(outBufs[0].pvBuffer);
            if (!TcpWriteAll(s_, tok.data(), tok.size())) {
                if (err) *err = "gagal mengirim token handshake";
                return false;
            }
        }

        if (st == SEC_I_CONTINUE_NEEDED) {
            std::vector<uint8_t> extra;
            for (int i = 0; i < 2; ++i) {
                if (inBufs[i].BufferType == SECBUFFER_EXTRA && inBufs[i].cbBuffer > 0) {
                    extra.assign(static_cast<uint8_t*>(inBufs[i].pvBuffer),
                                 static_cast<uint8_t*>(inBufs[i].pvBuffer) + inBufs[i].cbBuffer);
                }
            }
            rawIn_.swap(extra);
            if (!RecvMoreRaw(timeoutMs)) { if (err) *err = err_; return false; }
            continue;
        }

        if (st == SEC_E_OK) {
            std::vector<uint8_t> extra;
            for (int i = 0; i < 2; ++i) {
                if (inBufs[i].BufferType == SECBUFFER_EXTRA && inBufs[i].cbBuffer > 0) {
                    extra.assign(static_cast<uint8_t*>(inBufs[i].pvBuffer),
                                 static_cast<uint8_t*>(inBufs[i].pvBuffer) + inBufs[i].cbBuffer);
                }
            }
            rawIn_.swap(extra);
            done = true;
            break;
        }

        if (err) *err = "handshake gagal (status 0x" +
                        std::to_string(static_cast<unsigned long>(st)) + ")";
        return false;
    }
    return true;
}

bool TlsChannel::HandshakeClient(SOCKET s, CredHandle* cred, int timeoutMs, std::string* err,
                                 uint8_t outCertSha256[32]) {
    Close();
    s_ = s;
    cred_ = cred;
    isClient_ = true;
    err_.clear();
    if (!RunHandshake(&TlsChannel::ClientStep, timeoutMs, err)) return false;

    if ((QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_)) != SEC_E_OK) {
        if (err) *err = "QueryContextAttributes(STREAM_SIZES) gagal";
        return false;
    }
    PCCERT_CONTEXT remote = nullptr;
    if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &remote) != SEC_E_OK ||
        !remote) {
        if (err) *err = "sertifikat server tidak tersedia";
        return false;
    }
    DWORD hashLen = 32;
    uint8_t hash[32]{};
    if (CertGetCertificateContextProperty(remote, CERT_SHA256_HASH_PROP_ID, hash, &hashLen) &&
        hashLen == 32) {
        if (outCertSha256) std::memcpy(outCertSha256, hash, 32);
        std::memcpy(certSha256_, hash, 32);
        certShaValid_ = true;
    }
    CertFreeCertificateContext(remote);
    active_ = true;
    LLOG_I("TLS client handshake selesai (fingerprint=%s)",
           certShaValid_ ? util::HexEncode(certSha256_, 8).c_str() : "?");
    return true;
}

bool TlsChannel::HandshakeServer(SOCKET s, CredHandle* cred, int timeoutMs, std::string* err) {
    Close();
    s_ = s;
    cred_ = cred;
    isClient_ = false;
    err_.clear();
    if (!RunHandshake(&TlsChannel::ServerStep, timeoutMs, err)) return false;
    if (QueryContextAttributesW(&ctx_, SECPKG_ATTR_STREAM_SIZES, &sizes_) != SEC_E_OK) {
        if (err) *err = "QueryContextAttributes(STREAM_SIZES) gagal";
        return false;
    }
    active_ = true;
    LLOG_I("TLS server handshake selesai");
    return true;
}

} // namespace ldr::net
