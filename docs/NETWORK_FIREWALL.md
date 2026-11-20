# Firewall & Port — LiteDesk Remote

## Ringkasan port (FASE 1)

| Port | Protokol | Arah | Fungsi | Wajib? |
|---|---|---|---|---|
| 48500 | TCP | Inbound | TLS host (koneksi remote) | Ya (di komputer host) |
| 48501 | UDP | Inbound+Outbound | Discovery LAN query/reply | Opsional (bila discovery aktif) |

Tidak ada port lain yang dibuka pada FASE 1. FASE 5 (WebRTC) akan menambahkan UDP
dinamis untuk ICE + port TURN self-host — akan didokumentasikan di file ini saat
diimplementasikan.

## Menyiapkan firewall via perintah (disarankan)

Jalankan **PowerShell/CMD sebagai Administrator** di komputer **host**:

```bat
:: TCP TLS host - profil Private saja (jaringan rumah/kantor tepercaya)
netsh advfirewall firewall add rule name="LiteDesk Host TCP" ^
  dir=in action=allow protocol=TCP localport=48500 profile=private

:: UDP discovery (bila discovery diaktifkan)
netsh advfirewall firewall add rule name="LiteDesk Discovery UDP" ^
  dir=in action=allow protocol=UDP localport=48501 profile=private
```

Menghapus rule:

```bat
netsh advfirewall firewall delete rule name="LiteDesk Host TCP"
netsh advfirewall firewall delete rule name="LiteDesk Discovery UDP"
```

## Via GUI (Windows Defender Firewall)

1. Buka **Windows Security → Firewall & network protection → Advanced settings**.
2. **Inbound Rules → New Rule…**
3. Rule type: **Port** → TCP, local port `48500` → **Allow the connection**.
4. Centang hanya **Private** (dan Domain bila jaringan kantor terkelola) —
   **jangan centang Public**.
5. Beri nama `LiteDesk Host TCP`. Ulangi untuk UDP `48501` bila perlu.

> Catatan: bila jaringan rumah Anda tertandai *Public*, ubah profil jaringan ke
> *Private* (Settings → Network → Properties) agar rule berlaku — atau, jika harus
> tetap Public, buat rule dengan `profile=public` dan pahami risikonya (port terlihat
> oleh semua perangkat jaringan publik; proteksi TLS+PIN tetap aktif).

## Uji port

Dari komputer viewer (Windows PowerShell):

```powershell
Test-NetConnection 192.168.1.10 -Port 48500      # ganti IP host
TcpTestSucceeded : True   # ← harus True
```

Bila `False`: cek rule firewall, cek host Remote Access menyala (Dashboard → AKTIF),
cek IP host via halaman My Device, cek AP isolation pada router/Wi-Fi kantor.

## Prinsip keamanan port

- Port TCP 48500 selalu memproteksi dirinya: handshake non-TLS tidak mengungkap data;
  autentikasi PIN + rate limiting berjalan setelah TLS.
- Rule firewall dibatasi profil **Private**; installer opsional membuat rule yang sama.
- Port discovery UDP bisa dimatikan via Settings (Auto discovery OFF) tanpa
  mengganggu koneksi manual IP.

## VPN WireGuard

Bila host diakses melalui tunnel WireGuard, buat rule dengan scope interface
WireGuard (atau scope ke IP tunnel) — contoh bila tunnel memakai subnet 10.7.0.0/24:

```bat
netsh advfirewall firewall add rule name="LiteDesk Host TCP (WG)" ^
  dir=in action=allow protocol=TCP localport=48500 remoteip=10.7.0.0/24
```

Detail penggunaan VPN: [GUIDE_INTERNET.md](GUIDE_INTERNET.md).
