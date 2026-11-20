# LiteDesk Remote

Aplikasi **remote desktop untuk Windows 10/11** yang ringan, cepat, dan aman —
alternatif AnyDesk yang lebih hemat RAM/CPU. Ditulis 100% native C++20 + Win32 API,
**tanpa Electron, tanpa Node.js, tanpa browser engine, tanpa database, tanpa layanan
berbayar**.

> **Status: FASE 1 selesai (Core Architecture).**
> Lihat [ROADMAP.md](docs/ROADMAP.md) untuk FASE 2–8 (screen streaming, input, file
> transfer, audio, WebRTC, benchmark, release). Tidak ada klaim fitur yang belum
> diimplementasikan.

## Yang sudah bekerja di FASE 1

| Fitur | Status |
|---|---|
| Aplikasi Host & Viewer (GUI gelap Win32, dark mode, aksen hijau) | ✔ |
| Device ID otomatis dari fingerprint mesin (`LD-XXXXX-XXXXX`) | ✔ |
| PIN remote access (PBKDF2-HMAC-SHA-256, tidak pernah dikirim/disimpan plaintext) | ✔ |
| TLS 1.3 (Schannel) + pinning sertifikat TOFU per Device ID | ✔ |
| Autentikasi challenge-response (HMAC, tahan replay/MITM) | ✔ |
| Rate limiting + lockout eksponensial + budget challenge | ✔ |
| Konfirmasi akses remote (Terima / Terima+Ingat / Tolak) | ✔ |
| Unattended access & auto-accept per perangkat | ✔ |
| Permission mouse/keyboard/clipboard/file/audio (mask, dijalankan per-pesan) | ✔ |
| Auto discovery LAN (UDP query/reply, tanpa broadcast periodik) | ✔ |
| Multi-viewer dengan batas sesi terkonfigurasi | ✔ |
| Keepalive Ping/Pong + tampilan latency (RTT) | ✔ |
| Idle auto-disconnect (terkonfigurasi) | ✔ |
| Audit log (JSONL) + connection history + log aplikasi | ✔ |
| Config JSON di `%LOCALAPPDATA%\LiteDeskRemote` | ✔ |
| Koneksi via VPN WireGuard (tunnel = jalur TCP langsung) | ✔ |
| Unit test inti (141 assertion, lintas-platform) + test integrasi Windows | ✔ |

**Belum ada di FASE 1** (rencana jelas di ROADMAP): streaming layar (FASE 2), input
mouse/keyboard remote (FASE 3), clipboard/file/audio (FASE 4), internet P2P via
WebRTC + signaling + STUN/TURN (FASE 5), benchmark AnyDesk (FASE 7).

## Build (Windows 10/11 + Visual Studio 2022)

Prasyarat:
1. Visual Studio 2022 (Build Tools juga boleh) + workload **Desktop development with C++**
2. CMake 3.20+ (termasuk dalam installer VS sebagai komponen C++ CMake tools)

```powershell
git clone <repo> LiteDeskRemote   # atau ekstrak arsip
cd LiteDeskRemote
.\scripts\build.ps1               # build Release → build\Release\ldhost.exe + ldviewer.exe
.\scripts\run_tests.ps1           # build + jalankan semua test
.\scripts\package.ps1             # portable zip (dist\LiteDeskRemote-Portable-0.1.0.zip)
.\scripts\package.ps1 -Installer  # + installer Inno Setup (opsional)
```

Tidak ada dependensi pihak ketiga pada FASE 1 — hanya Windows SDK. Detail:
[docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).

Test inti juga bisa dibangun di OS mana pun (Linux/macOS/CI) karena bagian
protokol/keamanan/logika terpusat di `ldcore`:

```bash
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
./build-linux/ldcore_tests     # 141 assertion: framing, protokol, JSON, kripto, limiter
```

## Pakai cepat (LAN)

1. **Host** (komputer dikendalikan): jalankan `ldhost.exe` → buat PIN di *My Device*
   → klik **Nyalakan Remote Access** di *Dashboard* → izinkan di firewall
   ([docs/NETWORK_FIREWALL.md](docs/NETWORK_FIREWALL.md)).
2. **Viewer** (komputer pengendali): jalankan `ldviewer.exe` → pilih perangkat dari
   daftar discovery di *Remote Connection* (atau isi IP) → masukkan PIN → **Sambungkan**.
3. FASE 1 memvalidasi koneksi, TLS, autentikasi, permission, dan menampilkan RTT
   langsung. Streaming layar menyusul di FASE 2.

Panduan lengkap: [docs/GUIDE_LAN.md](docs/GUIDE_LAN.md) ·
Internet/VPN: [docs/GUIDE_INTERNET.md](docs/GUIDE_INTERNET.md) ·
Keamanan: [docs/SECURITY.md](docs/SECURITY.md) ·
Masalah umum: [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## Konfigurasi

File: `%LOCALAPPDATA%\LiteDeskRemote\config.json` (dibuat otomatis, default aman).

| Kunci | Default | Keterangan |
|---|---|---|
| `network.tcp_port` | 48500 | Port TLS host |
| `network.discovery_enabled` | true | Auto discovery LAN |
| `network.discovery_port` | 48501 | Port UDP discovery |
| `network.max_sessions` | 3 | Batas viewer simultan |
| `security.require_confirmation` | true | Konfirmasi tiap koneksi baru |
| `security.unattended` | false | Auto-terima bila PIN benar |
| `security.pin_iterations` | 210000 | Iterasi PBKDF2 |
| `security.idle_timeout_min` | 0 | Auto-disconnect idle (0 = mati) |
| `quality.target_fps` / `quality.bitrate_kbps` | 30 / 8000 | Dipakai encoder FASE 2 |

Data keamanan di `security/` (pin.json, identity.der, known_hosts.json), log di `logs/`.

## Struktur proyek

```
LiteDeskRemote/
├── CMakeLists.txt          README.md  LICENSE  docs/  assets/  installer/
├── common/
│   ├── protocol/           framing biner v1 + semua pesan (kontrak kawat)
│   ├── security/           device ID, PIN store PBKDF2, rate limiter, CNG, cert identity
│   ├── networking/         TCP, TLS Schannel, framed channel, discovery UDP
│   ├── ui/                 tema gelap, widget kit, window + sidebar (Win32/GDI)
│   └── util/               log, json, config, codec, time, events
├── host/                   SessionManager + halaman GUI host + main
├── viewer/                 ViewerSession + halaman GUI viewer + main
├── signaling/              slot FASE 5 (README rencana)
├── tests/                  core (lintas-platform) + win (integrasi localhost)
└── scripts/                build.ps1  run_tests.ps1  package.ps1
```

Desain lengkap dan alasan setiap keputusan: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Benchmark (FASE 7)

Target pengukuran: RAM idle, CPU idle, CPU streaming, bandwidth, FPS, latency
end-to-end — dibandingkan dengan AnyDesk pada mesin, resolusi, dan jaringan yang sama.
Harness benchmark dibangun di FASE 7; **belum ada angka performa yang diklaim
sekarang** karena belum bisa diukur secara adil.

## Lisensi

MIT — lihat [LICENSE](LICENSE).
