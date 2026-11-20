# Roadmap LiteDesk Remote

Prinsip: setiap fase harus **terkompilasi dan teruji** sebelum lanjut; fitur fase
sebelumnya tidak boleh hilang.

## FASE 1 — Core Architecture ✅ (selesai, kode teruji pada bagian lintas-platform)

- [x] Struktur modular: common/{protocol,security,networking,ui,util}, host/, viewer/, tests/, scripts/
- [x] Protokol kawat v1: framing biner 16 B, 10 pesan aktif + reservasi nomor FASE 2–5
- [x] TLS 1.3 Schannel server & client, pinning TOFU per Device ID
- [x] Autentikasi PIN challenge-response (PBKDF2 + HMAC, anti-replay/MITM)
- [x] Device ID (SHA-256 fingerprint mesin → LD-XXXXX-XXXXX)
- [x] Rate limiter + lockout eksponensial + budget challenge
- [x] Host: listener, multi-sesi (batas terkonfigurasi), permission mask,
      konfirmasi akses, unattended, auto-accept, idle timeout, audit log, GUI 5 halaman
- [x] Viewer: discovery LAN, koneksi by ID/IP:port, auth, RTT, known hosts, GUI 4 halaman
- [x] Test: 141 assertion inti (lintas-platform) + test integrasi Windows
      (CNG vectors, identitas TLS, handshake localhost, wrong-PIN lockout, discovery)
- [x] Build scripts (build/run_tests/package) + manifest DPI + ikon + installer script

Kriteria terpenuhi: `ldcore_tests` 141/141 lolos di sandbox Linux (protokol, JSON,
kripto vektor resmi, limiter, config). `ldwin_tests` dijalankan di Windows via
`scripts\run_tests.ps1` (menutup jalur produksi CNG/Schannel/Winsock + handshake
end-to-end).

## FASE 2 — Remote Screen (rencana berikutnya)

- [ ] Desktop Duplication API (IDXGIOutputDuplication, dirty-frames only)
- [ ] H.264 hardware encoder via Media Foundation (NVIDIA/Intel/AMD MFT)
- [ ] H.264 decoder (MF/DXVA) + render D3D11 viewer, fullscreen, scaling
- [ ] Adaptive bitrate + FPS berbasis RTT & loss (pesan PING/PONG sudah ada)
- [ ] Multi-monitor (pilih layar)
- [ ] Pesan VIDEO_* (reservasi 200–249 sudah dikunci di protokol)
- Kriteria: 1080p 15–60 FPS di jaringan LAN, CPU encoder < 15% (GPU), frame
  zero-copy dari DDA ke encoder (D3D11 texture).

## FASE 3 — Remote Input

- [ ] Mouse (SendInput, mapping koordinat multi-monitor, smoothing opsional)
- [ ] Keyboard (scancode, IME-aware clipboard-preserving mode)
- [ ] Fullscreen hotkey, switch monitor
- [ ] Pesan INPUT_* (reservasi 400–449), permission enforcement (infra sudah ada)

## FASE 4 — Advanced Features

- [ ] Clipboard sync (CF_UNICODETEXT + CF_HDROP) — reservasi 500–549
- [ ] File transfer dua arah (chunk 512 KiB, resume, progress) — reservasi 100–149
- [ ] Audio streaming (WASAPI loopback → Opus 24–64 kbps adaptif) — reservasi 300–349
- [ ] Dependency: libopus (dibahaskan di DEPENDENCIES.md)

## FASE 5 — Internet Connectivity

- [ ] libdatachannel (ICE/STUN/TURN/DTLS-SRTP) untuk data channel WebRTC
- [ ] signaling server self-hosted (folder signaling/, TLS + auth challenge sama)
- [ ] STUN publik/self-host, TURN self-host (coturn), konfigurasi domain sendiri
- [ ] Reconnection otomatis + fallback; VPN WireGuard tetap jalur alternatif
- Kriteria: diuji pada NAT simetris + NAT port-restricted nyata; laporan hasil.

## FASE 6 — Security Hardening

- [ ] Discovery announcement bertanda tangan HMAC identity
- [ ] Verifikasi silang PIN saat TOFU pertama (anti-MITM first-connect)
- [ ] Opsi Argon2id (evaluasi manfaat vs PBKDF2 utk PIN)
- [ ] Audit log append-hash chain; viewer keypair auth
- [ ] Review keamanan menyeluruh + fuzzing framing (libFuzzer/gdb)

## FASE 7 — Optimization & Benchmark

- [ ] Harness pengukuran: RAM working set, CPU (per frame & idle), latency
      end-to-end, bandwidth, FPS
- [ ] Benchmark vs AnyDesk: mesin, resolusi, jaringan identik; laporan angka apa adanya
- [ ] Profil alokasi; eliminasi copy di jalur video/audio

## FASE 8 — Release

- [ ] Release build final (static CRT), portable exe, installer Inno Setup final
- [ ] Dokumentasi final + CHANGELOG + packaging CI
- [ ] Distribusi source (zip/git), checksum, dan tanda tangan build
