# LiteDesk Remote — Keputusan Dependency

Prinsip: **setiap dependency harus dibenarkan**, dan dependensi bawaan Windows selalu dipilih bila
setara.

## FASE 1 — NOL dependensi pihak ketiga

Seluruh FASE 1 hanya memakai komponen sistem Windows dan C++20 standard library:

| Kebutuhan | Komponen sistem | Alasan |
|---|---|---|
| GUI | Win32 API + GDI (user32, gdi32) | ukuran & RAM terkecil yang mungkin; kontrol native (EDIT) untuk IME/caret |
| Jaringan TCP/UDP | Winsock2 (ws2_32) | bawaan OS |
| TLS 1.3 | Schannel (secur32) | bawaan OS; patch keamanan ikut Windows Update; tanpa biner tambahan |
| SHA-256 / HMAC / PBKDF2 / RNG | CNG BCrypt (bcrypt) | bawaan OS, FIPS-ready |
| Sertifikat self-signed | CryptoAPI + CertCreateSelfSignCertificate (crypt32, advapi32) | bawaan OS |
| Registry / fingerprint mesin | advapi32 (RegGetValueW) | bawaan OS |
| Daftar IP lokal | IP Helper (iphlpapi) | bawaan OS |
| Dark titlebar | dwmapi | bawaan OS |
| Tray icon | shell32 (Shell_NotifyIcon) | bawaan OS |
| JSON config | parser internal ~300 baris (common/util/json.cpp) | skema kecil & tetap; dependensi JSON tidak proporsional |
| Testing | test runner internal (assert macros) | tanpa gtest agar build tetap nol-dependensi |

## Fase berikutnya (rencana, dengan alasan)

| Fase | Dependency | Alasan memilih | Alternatif ditolak |
|---|---|---|---|
| 4 | **libopus** (BSD-like) | codec audio low-latency standar; tidak ada pengganti bawaan Windows untuk loopback streaming | Media Foundation AAC (pipeline lebih berat, latency lebih tinggi) |
| 5 | **libdatachannel** (MPL 2.0) + **mbedTLS** + **libjuice** (built-in) | ICE/STUN/TURN + DTLS-SRTP + data channel lengkap dalam beberapa MB; API C++ bersih; TURN client included | libwebrtc penuh (ratusan MB, build system kompleks); menulis ICE sendiri (rawan salah di area keamanan kritis) |
| 5 | **coturn** (opsional, self-host) | TURN relay fallback standar RFC 5766, gratis, self-host | layanan TURN berbayar (dilarang spec sebagai dependensi wajib) |
| 8 | **Inno Setup** (opsional, hanya untuk installer) | installer kecil dan gratis; portable exe tidak membutuhkannya | NSIS/WiX (setara; Inno dipilih karena script .iss ringkas) |

STUN publik yang dipakai FASE 5 hanya server STUN gratis (mis. `stun.l.google.com:19302` atau self-host
coturn) — bukan layanan berbayar, dan bisa sepenuhnya self-hosted.

## Metode pengadaan (FASE 4+)

Dependency di atas akan diambil via CMake `FetchContent` ke folder `third_party/` saat build, atau
via vcpkg — keputusan final di FASE 4. FASE 1 **tidak mengunduh apa pun**.
