# Panduan Koneksi Internet — LiteDesk Remote

## Status yang JUJUR (FASE 1)

| Jalur internet | Status FASE 1 |
|---|---|
| P2P WebRTC (STUN/TURN) | **Belum diimplementasikan** — dibangun di FASE 5 |
| Server signaling self-hosted | **Belum diimplementasikan** — dibangun di FASE 5 (slot `signaling/`) |
| TCP direct via VPN (WireGuard) | **Bekerja sekarang** (tunnel = jaringan L2/L3, jalur LAN logic berlaku) |

Sesuai spesifikasi proyek, kami **tidak mengklaim** koneksi internet P2P bekerja
sebelum NAT traversal + signaling benar-benar diimplementasikan dan diuji. Berikut
yang bisa dipakai hari ini dan rencana teknisnya.

## 1. Bekerja sekarang: WireGuard VPN (disarankan untuk penggunaan nyata)

WireGuard membuat tunnel encrypted antara viewer dan host; setelah tunnel naik,
kedua mesin punya IP privat yang bisa diakses langsung — jalur LAN LiteDesk bekerja
tanpa perubahan apa pun, lewat internet/NAT apa pun (asal UDP 51820 ke server/VPS
peer tidak diblokir).

**Topologi ringan:** satu VPS murah (atau mesin apa pun dengan IP publik) sebagai
WireGuard peer penghubung; host & viewer menyambung ke sana.

**Contoh konfigurasi host** (`wg0.conf`):

```ini
[Interface]
PrivateKey = <kunci-privat-host>
Address    = 10.7.0.2/24

[Peer]
PublicKey  = <kunci-publik-vps>
Endpoint   = 203.0.113.10:51820     # IP publik VPS
AllowedIPs = 10.7.0.0/24
PersistentKeepalive = 25
```

Viewer serupa dengan `Address = 10.7.0.3/24`. Di host, izinkan firewall TCP 48500
hanya dari subnet tunnel (lihat [NETWORK_FIREWALL.md](NETWORK_FIREWALL.md §VPN)).

Kemudian di viewer: endpoint `10.7.0.2` (IP tunnel host) + PIN → Sambungkan.
Semua trafik remote tetap melalui TLS 1.3 di atas WireGuard (defensif dua lapis).

**Kelebihan:** bekerja hari ini, enkripsi WireGuard modern, tanpa layanan berbayar.
**Kekurangan:** butuh peer publik (VPS) sebagai titik kumpul.

## 2. Rencana FASE 5: P2P WebRTC (tanpa VPN)

Arsitektur yang diimplementasikan di FASE 5 (dijelaskan penuh di
[ARCHITECTURE.md §8](ARCHITECTURE.md)):

```
viewer ──signaling (self-hosted, TLS)──► signaling server ◄── host
   │                                        │                 │
   └────── WebRTC data channel (DTLS-SRTP) ─┴── ICE/STUN/TURN ─┘
            direct P2P bila NAT mengizinkan
            TURN relay self-host sebagai fallback
```

- **Signaling**: `ldsignaling.exe` self-hosted (WebSocket + TLS), domain sendiri;
  hanya merelay SDP/ICE setelah autentikasi challenge PIN host yang sama dengan FASE 1.
- **STUN**: server publik gratis (mis. `stun:stun.l.google.com:19302`) atau self-host.
- **TURN**: self-host (coturn) sebagai fallback untuk NAT simetris/korporat.
- **Library**: libdatachannel (ICE libjuice + DTLS-SRTP + mbedTLS) — dependensi
  pertama proyek ini selain sistem, dibenarkan di [DEPENDENCIES.md](DEPENDENCIES.md).
- **Reconnection**: resume sesi + fallback otomatis.

## 3. Yang TIDAK boleh dilakukan sebelum FASE 5

- Jangan forward port 48500 langsung ke internet dari router tanpa VPN — permukaan
  serangan online brute-force membesar meski TLS+PIN aktif (rate limiter tetap
  melindungi, tapi jangan buka permukaan yang tidak perlu).
- Jangan terima klaim "remote via internet" dari build lain sebelum pengujian NAT
  nyata terdokumentasi.

## 4. Checklist VPN cepat

1. `winget install WireGuard.WireGuard` (atau installer resmi).
2. Buat keypair: `wg genkey | tee privatekey | wg pubkey > publickey`.
3. Susun config (contoh di atas) di host, viewer, dan peer publik.
4. Aktifkan tunnel di kedua mesin → uji `ping 10.7.0.2` dari viewer.
5. Jalankan LiteDesk seperti LAN: [GUIDE_LAN.md](GUIDE_LAN.md).
