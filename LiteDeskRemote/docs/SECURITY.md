# Keamanan — LiteDesk Remote (FASE 1)

Dokumen ini menjelaskan model ancaman, kontrol yang aktif, dan batasan yang jujur
pada FASE 1. Prinsip: keamanan dibangun sejak awal, bukan tambalan FASE 6.

## 1. Lapisan proteksi

```
 [Internet/LAN]
      │ TCP 48500
 ▼ TLS 1.3 (Schannel, sertifikat RSA-2048 self-signed host)
      │ pinning fingerprint (TOFU keyed by Device ID) di viewer
 ▼ Protokol LiteDesk v1
      │ HELLO → HELLO_ACK (sessionId acak)
      │ AUTH_CHALLENGE (nonce 32B + salt + iterasi)
      │ AUTH_RESPONSE (proof HMAC-PBKDF2, diikat sessionId + fingerprint)
 ▼ AUTH_RESULT (permissions mask)
      │ konfirmasi pengguna host (default ON)
 ▼ Sesi aktif: keepalive, permission per-pesan, idle timeout, audit
```

## 2. Model ancaman dan kontrol

| Ancaman | Kontrol | Status |
|---|---|---|
|Penyadap jaringan (passive eavesdropper) | TLS 1.3/1.2 Schannel; seluruh protokol di dalam TLS | ✔ |
| MITM mengganti sertifikat | Pinning SHA-256 sertifikat keyed by Device ID; perubahan → tolak + pesan eksplisit | ✔ |
| MITM relay (terminasi TLS dua sisi) | proof HMAC mengikat fingerprint sertifikat host — penyerang tanpa kunci privat host tidak bisa menghitung proof | ✔ |
| Replay auth | sessionId acak 64-bit diikat di HMAC; nonce acak per challenge | ✔ |
| Brute force PIN online | Rate limiter per-IP: 5 kegagalan/10 menit → lockout 30 s → ×2 per episode, maks 15 menit; budget challenge 10/menit/IP | ✔ |
| PIN bocor di disk | PIN tidak pernah disimpan; hanya salt + digest PBKDF2-HMAC-SHA-256 (210.000 iterasi default); verifikasi constant-time | ✔ |
| PIN bocor di jaringan | PIN tidak pernah dikirim; hanya proof HMAC satu kali | ✔ |
| Perangkat asing masuk tanpa pengawasan | Konfirmasi manual default ON; unattended OFF default; auto-accept hanya untuk perangkat yang di-remember eksplisit | ✔ |
| Akses berlebihan setelah auth | Permission mask (mouse/keyboard/clipboard/file/audio) diberikan subset dari yang diminta; ditolak per-pesan di host | ✔ |
| Sesi lupa ditutup | Idle timeout terkonfigurasi (deteksi frame non-keepalive); keepalive 5 s mendeteksi peer mati ≤ 20 s | ✔ |
| Port terbuka tanpa proteksi | Port TCP hanya menerima TLS; tidak ada respons plaintext; discovery UDP hanya menjawab query valid dengan throttle 1 reply/300 ms/IP (anti amplifikasi) dan bisa dimatikan | ✔ |
| Tidak ada jejak | Audit log JSONL: connect, auth_ok/failed, denied_by_user, permission_denied, idle_timeout, listener start/stop + ring memori untuk UI | ✔ |
| Flood discovery | Throttle reply + paket divalidasi ketat (magic, versi, panjang, maks 512 B) | ✔ |

## 3. Kriptografi (semua komponen OS — CNG/CryptoAPI/Schannel)

| Kebutuhan | Primitif |
|---|---|
| Hash | SHA-256 (BCrypt) |
| MAC | HMAC-SHA-256 (BCrypt, flag HMAC) |
| Derivasi PIN | PBKDF2-HMAC-SHA-256, 210.000 iterasi default (OWASP 2023: ≥ 600k untuk password umum; 210k dipilih karena PIN 6 digit acak + rate limiter online + opsi konfigurasi; nilai bisa dinaikkan di config) |
| Random | BCryptGenRandom (system preferred RNG) |
| TLS | Schannel, protokol default OS (Win10 1903+/11: TLS 1.2+1.3 aktif, ≤1.1 nonaktif), cipher suite default OS (AES-GCM/ChaCha20 di TLS 1.3, ECDHE) |
| Identitas | RSA-2048 self-signed 10 tahun, EKU ServerAuth+ClientAuth, kunci di container CryptoAPI persisten, DER di `security/identity.der` |

Tidak ada algoritma kripto buatan sendiri di jalur Windows. Implementasi referensi
SHA-256/HMAC/PBKDF2 di `crypto_portable.cpp` **hanya** dikompilasi untuk unit test
non-Windows dan divalidasi vektor uji resmi (FIPS 180-4, RFC 4231, RFC 7914) — jalur
produksi Windows memakai CNG dan divalidasi oleh `ldwin_tests` dengan vektor yang sama.

## 4. Siklus hidup PIN

- PIN dibuat acak 6 digit (atau kustom 4–10 digit) di halaman My Device.
- **Ditampilkan sekali** saat dibuat; setelah itu hanya tersimpan hash — PIN tidak bisa
  ditampilkan ulang, hanya diganti.
- Verifikasi host memakai digest tersimpan sebagai kunci HMAC (tanpa menghitung ulang
  PBKDF2 → auth cepat; proteksi brute force dijalankan rate limiter).
- Reset PIN lupa: buat PIN baru di My Device (file pin.json ditimpa; koneksi lama tidak
  terpengaruh).

## 5. Pinning sertifikat (TOFU)

- Saat koneksi pertama ke sebuah Device ID, viewer menyimpan SHA-256 sertifikat host
  di `security/known_hosts.json`.
- Koneksi berikutnya: fingerprint dibandingkan — tidak cocok = **tolak** dengan pesan
  penjelasan (host reinstall vs serangan).
- Reset: halaman Security viewer → "Lupakan Semua Host" → sambung ulang untuk verifikasi
  fingerprint baru (lakukan hanya jika Anda yakin fingerprint berubah karena reinstall).
- Batasan TOFU (jujur): pada koneksi pertama di LAN yang sudah dikompromikan, MITM bisa
  memalsukan fingerprint. Mitigasi rencana FASE 5/6: verifikasi silang via PIN saat
  TOFU (binding pertama lewat challenge yang sama) — terdokumentasi di ROADMAP.

## 6. Batasan yang diketahui (FASE 1) — jujur

1. **Discovery UDP tidak bertanda tangan** — perangkat di LAN bisa memalsukan reply
   (isi deviceId/hostname). Kontrol: viewer hanya memakai hasil discovery sebagai
   endpoint, autentikasi tetap wajib TLS-pinning + PIN. Signing announce direncanakan
   FASE 6.
2. **Viewer belum diautentikasi identitasnya secara kriptografis** — identitas viewer
   berupa Device ID self-claimed + PIN. Ini memadai untuk PIN-benar + konfirmasi-ON;
   auth kunci per-viewer (pasangan kunci) direncanakan FASE 6.
3. **Offline brute force PIN**: bila penyerang MENGENDALIKAN host (bisa membaca
   pin.json) atau sudah membobol TLS, dictionary attack PBKDF2 6 digit mungkin dalam
   hitungan hari di GPU. Mitigasi: PIN 8 digit untuk unattended (1M → 100M kombinasi),
   konfirmasi ON, encryption-at-rest khusus direncanakan FASE 6.
4. **Audit log belum diproteksi integritasnya** — user lokal bisa mengedit file log.
   Append-hash chain direncanakan FASE 6.
5. Tidak ada auto-update, tidak ada service SYSTEM, aplikasi berjalan asInvoker tanpa
   elevation — memperkecil permukaan serangan.

## 7. Rekomendasi hardening pengguna

1. Biarkan `security.require_confirmation = true` kecuali benar-benar butuh unattended.
2. Untuk unattended, pakai PIN 8 digit dan matikan discovery.
3. Bila tidak butuh LAN discovery, set `network.discovery_enabled = false`.
4. Scope firewall rule ke profil Private saja (lihat NETWORK_FIREWALL.md).
5. Jangan pernah mem-forward port 48500 ke internet sebelum FASE 5 selesai; untuk
   akses luar jaringan gunakan VPN WireGuard (GUIDE_INTERNET.md).
6. Tinjau `logs/audit-latest.jsonl` secara berkala.

## 8. Lokasi data

| Path | Isi |
|---|---|
| `%LOCALAPPDATA%\LiteDeskRemote\config.json` | konfigurasi |
| `%LOCALAPPDATA%\LiteDeskRemote\security\pin.json` | salt + digest PBKDF2 (tanpa PIN) |
| `%LOCALAPPDATA%\LiteDeskRemote\security\identity.der` | sertifikat TLS host (kunci privat tetap di key container OS) |
| `%LOCALAPPDATA%\LiteDeskRemote\security\known_hosts.json` | fingerprint host yang pernah disambung (viewer) |
| `%LOCALAPPDATA%\LiteDeskRemote\logs\audit-latest.jsonl` | audit log |
| `%LOCALAPPDATA%\LiteDeskRemote\logs\app-YYYYMMDD.log` | log aplikasi (rotasi 8 MB) |
