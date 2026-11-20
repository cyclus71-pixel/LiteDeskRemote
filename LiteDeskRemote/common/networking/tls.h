// tls.h — channel TLS 1.3/1.2 berbasis Schannel, client & server.
// Handshake manual (validasi sertifikat sendiri via pinning), EncryptMessage/DecryptMessage
// dengan penanganan lengkap: SEC_E_INCOMPLETE_MESSAGE, SECBUFFER_EXTRA, renegotiasi,
// context expired. Windows-only (secur32).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "net.h"

#define SECURITY_WIN32
// didefinisikan oleh sspi.h; SECURITY_WIN32 harus didefinisikan sebelum include security.h
#include <security.h>
#include <schannel.h>
#include <sspi.h>

namespace ldr::net {

// Manajemen credential handle (dibungkus shared_ptr agar aman dipakai lintas objek).
struct CredHandleDeleter {
    void operator()(CredHandle* h) const;
};
using CredPtr = std::unique_ptr<CredHandle, CredHandleDeleter>;

// Credential untuk server: sertifikat identitas host (RSA-2048 self-signed).
CredPtr MakeServerCred(PCCERT_CONTEXT cert, std::string* err);
// Credential untuk client: tanpa auto-creds; validasi manual (pinning) oleh pemanggil.
CredPtr MakeClientCred(std::string* err);

class TlsChannel {
public:
    TlsChannel() = default;
    ~TlsChannel();

    struct RecvResult {
        size_t n = 0;
        enum Status { Ok, Eof, Timeout, Error } status = Ok;
    };

    // Handshake klien. Bila outCertSha256 != nullptr, SHA-256 sertifikat server
    // (whole-DER, 32 byte) diisi di sini untuk pinning.
    bool HandshakeClient(SOCKET s, CredHandle* cred, int timeoutMs, std::string* err,
                         uint8_t outCertSha256[32] = nullptr);
    bool HandshakeServer(SOCKET s, CredHandle* cred, int timeoutMs, std::string* err);

    // >0: byte plaintext; 0: EOF bersih; -1: error atau timeout.
    int Recv(uint8_t* dst, size_t cap, std::string* err = nullptr);
    // Mengirim data (dichunk sesuai cbMaximumMessage Schannel). false bila gagal.
    // Thread-safe: bisa dipanggil dari keepalive thread bersamaan dengan loop utama.
    bool Send(const uint8_t* src, size_t n, std::string* err = nullptr);

    // Kirim close_notify best-effort, lalu tutup.
    void CloseNotify();
    void Close();
    bool Active() const { return active_; }
    const std::string& Error() const { return err_; }

private:
    // Satu langkah handshake: kirim token keluar bila ada, feed token masuk.
    bool RecvMoreRaw(int timeoutMs); // append ke rawIn_
    bool DecryptPending();           // rawIn_ → plain_ (return false = error)
    bool RunHandshake(SECURITY_STATUS (TlsChannel::*fn)(SecBufferDesc*, SecBufferDesc*,
                                                        DWORD*, bool*),
                      int timeoutMs, std::string* err);

    SECURITY_STATUS ClientStep(SecBufferDesc* in, SecBufferDesc* out, DWORD* attrs, bool* first);
    SECURITY_STATUS ServerStep(SecBufferDesc* in, SecBufferDesc* out, DWORD* attrs, bool* first);

    SOCKET s_ = INVALID_SOCKET;
    CredHandle* cred_ = nullptr;
    CtxtHandle ctx_{};
    bool ctxOpen_ = false;
    bool firstCall_ = true;
    bool isClient_ = false;
    bool active_ = false;
    std::string err_;
    std::mutex sendMu_;  // melindungi EncryptMessage+send dari thread keepalive

    std::vector<uint8_t> rawIn_;  // byte terenkripsi belum diproses
    std::vector<uint8_t> plain_;  // plaintext siap dibaca pemanggil
    SecPkgContext_StreamSizes sizes_{};
    uint8_t certSha256_[32]{};
    bool certShaValid_ = false;
};

} // namespace ldr::net
