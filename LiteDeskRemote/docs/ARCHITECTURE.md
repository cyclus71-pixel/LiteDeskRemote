# LiteDesk Remote — Arsitektur Final

Versi dokumen: 1.0 (FASE 1)
Status implementasi: FASE 1 selesai ditulis dan teruji pada bagian lintas-platform.

---

## 1. Prinsip Desain

LiteDesk Remote dibangun dengan prinsip berikut, yang menjadi dasar semua keputusan teknis:

1. **Native sepenuhnya.** Win32 API, GDI, Winsock, Schannel, CNG, DXGI/Media Foundation (FASE 2), WASAPI (FASE 4). Tidak ada Electron, tidak ada runtime terkelola, tidak ada browser engine.
2. **Nol dependensi pihak ketiga pada FASE 1.** Semua yang dibutuhkan FASE 1 (TLS 1.3, hashing, PBKDF2, HMAC, sertifikat self-signed) disediakan komponen sistem Windows: Schannel dan CNG (BCrypt).
3. **Bounded resources.** Jumlah thread, buffer, dan alokasi dikendalikan secara eksplisit. Tidak ada alokasi per-frame di jalur panas (berlaku penuh sejak FASE 2).
4. **Protokol biner deterministik.** Framing biner dengan header tetap 16 byte, little-endian, guard ukuran payload. Tanpa serializer generik (protobuf/CBOR) — ukuran kode kecil, overhead nol.
5. **Keamanan sejak FASE 1, bukan tambalan di FASE 6.** TLS 1.3, PBKDF2, challenge-response, rate limiting, audit log sudah aktif di FASE 1.
6. **Setiap fase terkompilasi dan teruji.** Bagian lintas-platform (`ldcore`) diuji dengan unit test yang bisa berjalan di OS mana pun (FASE 1: 141 assertion, semua lolos); bagian Windows (`ldwin`, host, viewer) diuji dengan test integrasi localhost yang dijalankan otomatis oleh `scripts/run_tests.ps1` di mesin Windows. Sebagai lapisan QA tambahan, seluruh kode Windows juga telah melewati **link-check penuh** dengan toolchain cross mingw-w64 (clang) terhadap header Windows asli — menangkap kesalahan API/sintaks sebelum build MSVC di mesin pengguna.

## 2. Peta Modul

```
LiteDeskRemote/
├── CMakeLists.txt            → target: ldcore, ldwin, ldhost.exe, ldviewer.exe, test biner
├── common/
│   ├── util/                 → kode murni lintas-platform
│   │   ├── codec.h           → endian LE, hex, base32, perbandingan constant-time
│   │   ├── time.h            → NowMs() (monotonic), WallNowMs(), format timestamp
│   │   ├── log.h/.cpp        → logger thread-safe: file + OutputDebugString, rotasi 8 MB
│   │   ├── json.h/.cpp       → parser/serializer JSON minimal (dipakai config, pin, known_hosts, audit)
│   │   ├── config.h/.cpp     → konfigurasi berbasis JSON di %LOCALAPPDATA%\LiteDeskRemote
│   │   └── events.h          → EventQueue<T> thread-safe (jalur komunikasi thread → UI)
│   ├── protocol/
│   │   ├── protocol.h/.cpp   → framing 16 byte, guard ukuran, FramedReader (parser streaming)
│   │   └── messages.h/.cpp   → BinaryWriter/Reader + encode/decode semua pesan
│   ├── security/
│   │   ├── rate_limiter.h    → limiter autentikasi (injectable clock, teruji lintas-platform)
│   │   ├── security.h/.cpp   → random (CNG), Device ID (SHA-256 fingerprint mesin), PinStore (PBKDF2),
│   │   │                       helper challenge-response, audit helper
│   │   ├── crypto_win.*      → wrapper CNG: SHA-256, HMAC-SHA-256, PBKDF2-HMAC-SHA-256, BCryptGenRandom
│   │   └── cert_identity.*   → identitas TLS host: RSA-2048 self-signed via CryptoAPI, persist DER
│   ├── networking/
│   │   ├── net.h/.cpp        → TcpSocket/TcpListener (timeout non-blocking), discovery UDP query/reply,
│   │   │                       MyIps(), hostname
│   │   ├── tls.h/.cpp        → TlsChannel: handshake client/server Schannel, EncryptMessage/DecryptMessage,
│   │   │                       penanganan buffer EXTRA/renegotiasi/INCOMPLETE_MESSAGE
│   │   └── framed.h/.cpp     → FramedChannel: frame protokol di atas TLS dengan timeout per operasi
│   └── ui/
│       ├── theme.h/.cpp      → palet gelap, font per-DPI (Segoe UI), skala DIP→px
│       ├── widgets.h/.cpp    → Button, Toggle, Label, TextInput (EDIT native, IME aman), ListPanel,
│       │                       StatCard, Stepper — semua owner-draw GDI
│       └── window.h/.cpp     → AppWindow: double-buffered, sidebar navigasi, halaman, overlay dialog,
│                               dark titlebar (DWM), timer 100 ms untuk pump event
├── host/
│   ├── session.h/.cpp        → SessionManager + Session (TLS accept, auth, permission, keepalive, audit)
│   ├── pages.h/.cpp          → Dashboard, My Device, Settings, Security, Connection History
│   └── main.cpp              → inisialisasi, tray icon, message loop
├── viewer/
│   ├── session.h/.cpp        → ViewerSession (connect, TLS + pinning TOFU, auth PIN, keepalive, RTT)
│   ├── pages.h/.cpp          → Remote Connection, Settings, Security, Connection History
│   └── main.cpp
├── signaling/                → slot FASE 5 (server signaling self-hosted), belum diimplementasikan
├── tests/
│   ├── core/test_core.cpp    → 141 assertion lintas-platform (framing, JSON, base32, rate limiter,
│   │                           pesan protokol, guard ukuran, vektor kripto resmi) — dijalankan di OS apa pun
│   └── win/test_win.cpp      → vektor uji PBKDF2/HMAC RFC + handshake TLS+auth end-to-end localhost
│                               + discovery loopback — dijalankan di Windows oleh run_tests.ps1
├── scripts/
│   ├── build.ps1             → build MSVC otomatis (vswhere + CMake + Visual Studio 2022)
│   ├── run_tests.ps1         → build + jalankan core & win tests
│   └── package.ps1           → staging portable zip + opsional installer Inno Setup
├── installer/litedesk.iss    → script installer (FASE 8, opsional)
└── assets/                   → litedesk.ico, app.manifest (PerMonitorV2), host.rc, viewer.rc
```

## 3. Alur Koneksi (FASE 1)

```
 VIEWER (pengendali)                        HOST (dikendalikan)
 ─────────────────────                      ────────────────────
 Remote Connection page
   │ 1. resolve endpoint
   │    - "LD-XXXXX-XXXXX" → cache discovery
   │    - "192.168.1.10[:48500]" → langsung
   ▼
 TcpSocket::Connect ────────────────► TcpListener (0.0.0.0:48500)
   │                                     │ SessionThread baru (maks: network.max_sessions)
   ▼                                     ▼
 TLS 1.3 Handshake ◄───────────────► Schannel server (cert self-signed RSA-2048)
   │                                     │
   │ 2. kirim HELLO                      │ 3. HELLO_ACK (session_id)
   │ 4. AUTH_CHALLENGE ◄─────────────────│    (nonce 32B, salt 16B, iterations)
   │ 5. AUTH_RESPONSE                    │    proof = HMAC(PBKDF2(PIN), nonce|...|cert_sha256)
   │                                     │ 6. rate limiter check + verifikasi constant-time
   │                                     │    + [konfirmasi user / unattended / auto-accept]
   │ 7. AUTH_RESULT ◄────────────────────│    (ok, permissions mask)
   ▼                                     ▼
 state ACTIVE                        state ACTIVE
 PING/PONG tiap 5 s (RTT display)    PONG/PING, idle timer, audit log
```

Discovery LAN: viewer mengirim **query** UDP broadcast ke port 48501; host merespons **reply** unicast
(throttled 1 reply / 300 ms per IP untuk mencegah amplifikasi). Tidak ada broadcast periodik —
jaringan tetap senyap.

## 4. Spesifikasi Protokol Kawat (Protokol v1)

### 4.1 Frame TCP

Header tetap **16 byte**, little-endian:

| Offset | Ukuran | Field    | Nilai                            |
|--------|--------|----------|----------------------------------|
| 0      | 4      | magic    | `0x4C44524D` ("LDRM")            |
| 4      | 2      | version  | `1`                              |
| 6      | 2      | type     | `MsgType`                        |
| 8      | 4      | payload  | ukuran payload dalam byte        |
| 12     | 4      | flags    | `0` (cadangan)                   |

- Payload maksimum: **1 MiB** (guard di kedua sisi; pelanggaran = `SESSION_CLOSED` reason `protocol_error`).
- Parser streaming (`FramedReader`) menerima data parsial, baik per byte maupun burst, dan menghasilkan
  frame utuh. Terverifikasi oleh unit test feed byte-per-byte dan payload terpotong.

### 4.2 Pesan (type)

| Type | Nama              | Payload                                                                                                          |
|------|-------------------|------------------------------------------------------------------------------------------------------------------|
| 1    | `Hello`           | protoVer u32, features u64, deviceId str, deviceName str, kind u8 (0=viewer, 1=host)                              |
| 2    | `HelloAck`        | protoVer u32, features u64, deviceId str, hostName str, sessionId u64                                             |
| 3    | `AuthChallenge`   | serverNonce bytes32, salt bytes16, iterations u32                                                                 |
| 4    | `AuthResponse`    | clientNonce bytes32, proof bytes32, deviceId str, deviceName str, requestedPerms u64                              |
| 5    | `AuthResult`      | ok u8, reason str, permissions u64                                                                                |
| 6    | `PermissionUpdate`| permissions u64                                                                                                   |
| 7    | `Ping`            | tSend u64 (ms monotonic)                                                                                          |
| 8    | `Pong`            | tSend u64, tRecv u64, tNow u64                                                                                    |
| 9    | `SessionClosed`   | reasonCode u8, text str                                                                                           |
| 10   | `ControlStatus`   | level u8, text str                                                                                                |

Nomor type untuk fase berikutnya **sudah direservasi** agar tidak ada perubahan protokol di kemudian hari:
100–149 file transfer (FASE 4), 200–249 video (FASE 2), 300–349 audio (FASE 4), 400–449 input
(FASE 3), 500–549 clipboard (FASE 4). `string` = u16 panjang + byte UTF-8 (maks 1024).

### 4.3 Discovery UDP (port 48501)

| Field                              | Catatan                          |
|------------------------------------|----------------------------------|
| magic u32 = `0x4C445044` ("LDPD")  |                                  |
| version u16                        |                                  |
| type u16 (1=query, 2=reply)        |                                  |
| tcpPort u16                        | port TCP host                    |
| tsSec u64                          | unix time                        |
| deviceId str u8-len (maks 40)      |                                  |
| hostName str u8-len (maks 100)     | UTF-8                            |

Validasi ketat: ukuran paket maks 512 byte, panjang field di-parse dengan bound check.

## 5. Autentikasi dan Keamanan Channel

### 5.1 Lapisan TLS

- Host menghasilkan **RSA-2048 self-signed** saat pertama kali dijalankan (CryptoAPI, container `LiteDesk-Host-Identity`), validity 10 tahun, EKU ServerAuth+ClientAuth. DER disimpan di `security/identity.der`.
- Viewer melakukan **pinning sertifikat (TOFU keyed by Device ID)**: SHA-256 keseluruhan sertifikat disimpan di `security/known_hosts.json` dan diverifikasi pada setiap koneksi berikutnya. Perubahan fingerprint → koneksi ditolak dengan pesan eksplisit (prosedur reset ada di SECURITY.md).
- Protokol TLS: default OS (Windows 10 1903+/11 mengaktifkan TLS 1.3 dan 1.2; TLS ≤1.1 dinonaktifkan oleh OS). Kode tidak men-downgrade.

### 5.2 Auth PIN (challenge-response; PIN tidak pernah dikirim)

```
digest  = PBKDF2-HMAC-SHA256(PIN, salt 16B acak, iter 210000) → 32 B      (dihitung viewer)
proof   = HMAC-SHA256(key=digest,
                      msg = "LDR1" || serverNonce(32) || clientNonce(32)
                            || sessionId(8 LE) || certSha256(32))          (dihitung viewer)
host    = HMAC-SHA256(key=digestTersimpan, msg yang sama)                 (verifikasi constant-time)
```

Properti:
- PIN tidak pernah melewati jaringan; yang dikirim hanya proof satu kali.
- `certSha256` diikat ke dalam HMAC → serangan MITM relay yang menghentikan TLS di tengah gagal.
- `sessionId` acak diikat → replay antar-sesi gagal.
- `clientNonce` acak → proof tidak dapat diprediksi/diputar ulang.
- Host tidak menghitung ulang PBKDF2 saat verifikasi (digest tersimpan langsung dipakai sebagai kunci HMAC) → auth cepat; proteksi brute-force diserahkan ke rate limiter online.
- Dictionary attack offline terhadap capture trafik membutuhkan TLS yang sudah bocor — dibahas terbuka di SECURITY.md (bagian "Batasan").

### 5.3 Rate Limiting

- Per-IP: 5 kegagalan dalam jendela 10 menit → lockout eksponensial 30 s, 60 s, … maks 15 menit.
- Budget challenge: maks 10 challenge / menit / IP (mencegah flood challenge PBKDF2).
- Clock di-inject (std::function) → perilaku teruji deterministik di unit test.

## 6. Model Threading

**ldhost.exe** (FASE 1):

| Thread                    | Kondisi idle            | Tugas                                        |
|---------------------------|-------------------------|----------------------------------------------|
| Main (UI)                 | selalu ada              | message loop Win32, paint tiap ada invalidasi |
| Discovery responder       | saat Remote Access ON   | recvfrom UDP (select 1 s)                    |
| Listener                  | saat Remote Access ON   | accept (select 1 s)                          |
| Per session               | saat ada viewer         | handshake, auth, keepalive, read frame       |

Idle: 1 thread aktif saja → CPU ~0%. RAM: tidak ada alokasi per-frame pada kondisi idle.

**ldviewer.exe**: Main (UI) + 1 thread koneksi saat menyambung + watcher discovery saat halaman
Remote Connection aktif.

Komunikasi thread → UI: `EventQueue<AppEvent>` di-drain WM_TIMER 100 ms; state sesi di-pull oleh
halaman aktif lewat snapshot ber-mutex. Tidak ada raw PostMessage dengan pointer mentah.

## 7. Keputusan Desain dan Alasannya

| Keputusan | Alasan | Alternatif yang ditolak |
|---|---|---|
| GUI Win32 owner-draw, bukan Qt | Qt menambah ~100 MB runtime & baseline RAM besar; tujuan utama proyek adalah ringan. UI dibutuhkan sederhana (form + list), owner-draw GDI cukup dan beratnya ~0. | Qt Widgets, Electron (dilarang spec) |
| TextInput tetap memakai EDIT control native | Caret, IME, dan aksesibilitas gratis; hanya warna yang di-theming | Edit box owner-draw penuh (rawan bug IME) |
| TLS via Schannel, kripto via CNG | Komponen OS; nol dependensi, patch keamanan ikut Windows Update; TLS 1.3 didukung Win10 1903+ | OpenSSL (biner ~3 MB + manajemen versi sendiri); mbedTLS untuk FASE 1 (tetap tambahan) |
| RSA-2048 via CryptoAPI untuk identitas | Jalur paling stabil untuk CertCreateSelfSignCertificate + Schannel | NCrypt/ECDSA (bekerja, tapi permukaan API lebih besar; migrasi mudah karena hanya 1 file) |
| PBKDF2-HMAC-SHA-256 210.000 iterasi | Bawaan CNG satu baris; OWASP masih merekomendasikan rentang ini; auth jarang terjadi (host tak menghitung ulang) | Argon2id (perlu dependensi; akan dievaluasi di FASE 6) |
| Socket blocking + thread per koneksi | Jumlah koneksi kecil (default maks 3 viewer); kompleksitas IOCP tidak sebanding; lebih mudah dibuktikan benar | IOCP (dipertimbangkan untuk jalur media FASE 2, bukan untuk ≤3 koneksi kontrol) |
| Framing biner sendiri | 16 byte overhead, parser 100 baris, teruji penuh; tidak ada codegen | protobuf/CBOR (dependensi/ukuran kode tidak proporsional) |
| Discovery query/reply (bukan announce periodik) | Tanpa trafik pasif di LAN, tanpa tabrakan binding UDP antara host & viewer di mesin yang sama | Broadcast periodik (kebisingan jaringan + kolisi port) |
| libdatachannel (+mbedTLS, libjuice) di FASE 5 | WebRTC data channel lengkap (ICE/STUN/TURN/DTLS-SRTP) ~beberapa MB, terawat, lisensi MPL 2.0 | libwebrtc penuh (ratusan MB, build system berat), implementasi ICE sendiri (rawan salah, keamanan kritis) |
| libopus di FASE 4 | Standar de-facto voice/low-latency; tidak ada pengganti bawaan Windows | AAC/Media Foundation (latency & kompleksitas pipeline lebih tinggi) |
| Hardware H.264 via Media Foundation di FASE 2 | MFT vendor (NVIDIA/Intel/AMD) langsung, nol dependensi, zero-copy via D3D11 texture | ffmpeg/libavcodec (ukuran besar, lisensi) |
| Core lintas-platform (ldcore) | Framing/JSON/limiter/auth-logic bisa diuji deterministik di CI mana pun, termasuk lingkungan penulis; permukaan bug Windows tetap kecil | Semua Windows-only (tidak ada test sebelum mesin Windows) |

## 8. Rencana Teknis FASE 2–8 (dampak ke struktur)

- **FASE 2 (Screen):** `host/capture/` (IDXGIOutputDuplication → texture D3D11), `host/video/` (MFT encoder hardware, konfigurasi bitrate/FPS adaptif), `viewer/rendering/` (D3D11 decoder + blit ke swapchain), pesan `VIDEO_*` 200+. Frame dikirim sebagai H.264 NAL; jendela render terpisah fullscreen-able. Screenshot-diff: encoder hanya menerima frame dirty (DDA `AccumulatedFrames`).
- **FASE 3 (Input):** pesan `INPUT_*` 400+, `SendInput` di host dengan pemeriksaan `permissions.Mouse/Keyboard`, multi-monitor via `EnumDisplayMonitors` + mapping koordinat.
- **FASE 4 (Advanced):** `AUDIO_*` 300+ (WASAPI loopback → Opus 24 kbps–64 kbps adaptif), `CLIPBOARD_*` 500+ (CF_UNICODETEXT + CF_HDROP), `FILE_*` 100–149 (chunk 512 KiB, resume), adaptive bitrate/FPS berdasar RTT + packet loss dari PING/PONG.
- **FASE 5 (Internet):** signaling server C++ (WebSocket, TLS) di `signaling/`, libdatachannel untuk data channel WebRTC (ICE libjuice: STUN publik gratis atau self-host, TURN self-host contoh coturn), reconnect otomatis + fallback TCP-direct-over-VPN. Jalur LAN tetap tanpa server.
- **FASE 6 (Security hardening):** discovery announcement bertanda tangan HMAC identity, verifikasi permissions UI-side, audit log terstruktur penuh, opsi Argon2id (jika evaluasi layak), auto-disconnect idle yang menghitung aktivitas input/media nyata.
- **FASE 7 (Optimasi):** harness benchmark (`tests/benchmark/`) mengukur RAM (working set), CPU (time per frame), latency end-to-end (timestamp di frame), bandwidth; pembanding AnyDesk pada mesin/resolusi/jaringan yang sama — hasil dilaporkan apa adanya.
- **FASE 8 (Release):** static CRT, portable zip via `package.ps1`, installer Inno Setup opsional.

## 9. Konfigurasi Runtime

File: `%LOCALAPPDATA%\LiteDeskRemote\config.json` (dibuat otomatis dengan default saat pertama jalan).
Kunci lengkap ada di README §Konfigurasi. Data keamanan di `security/` (pin.json, identity.der,
known_hosts.json), log di `logs/` (app-*.log, audit-*.jsonl).
