# Panduan Penggunaan LAN — LiteDesk Remote (FASE 1)

Skenario: dua (atau lebih) komputer Windows 10/11 dalam jaringan yang sama
(Wi-Fi rumah, LAN kantor, atau satu mesin untuk uji coba).

## 1. Persiapan sekali

1. Build atau ekstrak portable: `ldhost.exe` (host) dan `ldviewer.exe` (viewer)
   di masing-masing komputer.
2. Di komputer **host**, buat firewall rule (sekali saja):
   ```bat
   netsh advfirewall firewall add rule name="LiteDesk Host TCP" dir=in action=allow protocol=TCP localport=48500 profile=private
   netsh advfirewall firewall add rule name="LiteDesk Discovery UDP" dir=in action=allow protocol=UDP localport=48501 profile=private
   ```
   Panduan lengkap: [NETWORK_FIREWALL.md](NETWORK_FIREWALL.md).

## 2. Di komputer host

1. Jalankan `ldhost.exe`. Saat pertama kali, muncul overlay **PIN Remote Access
   Pertama** — catat PIN (mis. `483920`); PIN hanya tampil sekali.
2. Halaman **Dashboard** → klik **Nyalakan Remote Access**. Status berubah AKTIF;
   di bawahnya terlihat jumlah perangkat terhubung dan log aktivitas.
3. Halaman **My Device** menampilkan **Device ID** (mis. `LD-K7QM2-P9WXT`),
   hostname, alamat IP LAN, dan fingerprint TLS. Salin Device ID untuk dibagikan
   (opsional — viewer juga bisa menemukan lewat discovery).

## 3. Di komputer viewer

1. Jalankan `ldviewer.exe` → halaman **Remote Connection**.
2. Tunggu/tekan **Segarkan Discovery** — perangkat host muncul di daftar bawah
   (Device ID, hostname, IP:port). *(Klik "Pakai Device ID Terpilih" untuk mengisi
   kolom endpoint.)*
3. Masukkan **PIN** host.
4. Klik **Sambungkan**. Alur status: menyambung → TLS handshake → autentikasi →
   **AKTIF** dengan tampilan **Latency** (RTT) yang diperbarui tiap 5 detik.
5. Di host (bila konfirmasi aktif — default), muncul overlay **Permintaan Koneksi
   Remote** dengan nama perangkat, IP, dan izin yang diminta: **Terima**,
   **Terima + Ingat**, atau **Tolak**.

> **Koneksi pertama = pinning TLS (TOFU):** viewer menyimpan fingerprint host.
> Koneksi berikutnya diverifikasi otomatis; fingerprint berubah → ditolak dengan
> pesan penjelasan (lihat halaman Security viewer).

## 4. Alternatif tanpa discovery

Isi endpoint langsung di kolom pertama:
- IP: `192.168.1.10` (port default 48500)
- IP:port: `192.168.1.10:48500`

Host juga bisa diakses dari beberapa viewer bersamaan — batas sesi diatur di
Settings host (`network.max_sessions`, default 3). Viewer ke-4 mendapat pesan
"batas jumlah sesi tercapai".

## 5. Uji coba satu mesin (host + viewer di PC yang sama)

1. Jalankan `ldhost.exe`, nyalakan Remote Access, catat PIN.
2. Jalankan `ldviewer.exe` → endpoint `127.0.0.1` → PIN → Sambungkan.
3. Discovery juga menemukan host lokal (query dikirim ke 127.0.0.1 dan broadcast).

## 6. Yang terjadi setelah AKTIF (FASE 1)

- Sesi tervalidasi penuh: TLS 1.3, autentikasi PIN, permission mask, keepalive.
- Host menampilkan sesi aktif (IP, status, latency) di Dashboard; semua kejadian
  terekam di Connection History + audit log.
- Viewer menampilkan status "terhubung — streaming layar hadir di FASE 2".
  Layar remote belum mengalir di FASE 1 — ini sesuai ROADMAP, bukan kegagalan.

## 7. Mematikan

- Host: **Matikan Remote Access** → semua sesi ditutup dengan alasan
  `remote access dimatikan di host`, listener berhenti, port tak lagi terbuka.
- Viewer: **Putuskan**.
