# signaling/ — Server Signaling (FASE 5, BELUM DIIMPLEMENTASIKAN)

Folder ini disiapkan untuk FASE 5 (Internet Connectivity). Status saat ini:
**belum ada implementasi** — tidak ada target build dari folder ini di FASE 1.

## Rencana (FASE 5)

Server signaling self-hosted yang ringan untuk WebRTC P2P:

- WebSocket + TLS (bingkai signaling sama seperti protokol kawat LiteDesk).
- Peran:
  1. Registrasi Device ID host (map device_id → koneksi signaling).
  2. Relay offer/answer SDP + ICE candidates antara viewer dan host.
  3. Rate limit per IP + autentikasi challenge-response yang sama seperti FASE 1
     (PIN host) sebelum mengekspose keberadaan host.
- Self-hosted: jalankan `ldsignaling.exe` di VPS / server sendiri dengan domain sendiri.
- STUN publik gratis (mis. `stun:stun.l.google.com:19302`) atau self-host coturn;
  TURN self-host (coturn) sebagai fallback relay.

Koneksi LAN TIDAK membutuhkan server ini — LAN tetap direct TCP + discovery UDP.

## Alasan menunda ke FASE 5

Spesifikasi meminta jangan mengklaim koneksi internet bekerja tanpa NAT traversal +
signaling yang benar-benar diimplementasikan. FASE 1 fokus pada arsitektur, keamanan
dasar, dan jalur LAN; WebRTC/STUN/TURN/signaling dibangun utuh di FASE 5 dan diuji
pada jaringan NAT nyata sebelum diklaim bekerja.
