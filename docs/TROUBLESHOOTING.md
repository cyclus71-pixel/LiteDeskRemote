# Troubleshooting — LiteDesk Remote (FASE 1)

## Build

| Gejala | Solusi |
|---|---|
| `CMake tidak ditemukan` di build.ps1 | Instal komponen VS "C++ CMake tools for Windows" atau CMake 3.20+ standalone (tambahkan ke PATH). |
| `Visual Studio 2022 ... tidak ditemukan` | Instal VS 2022 / Build Tools dengan workload **Desktop development with C++** (x64 toolset). |
| Error C7555/C4828 (encoding) | Sudah ditangani flag `/utf-8`; pastikan file tidak diubah editor yang mengubah encoding. |
| Link error `bcrypt.lib / secur32.lib` | Pastikan Windows SDK terinstal (komponen VS default workload C++). |
| `MSB8066`/RC error saat compile `.rc` | Pastikan `assets\litedesk.ico` dan `assets\app.manifest` ada (bagian dari repo). |

## Runtime — Host

| Gejala | Sebab & solusi |
|---|---|
| Overlay "Gagal menyalakan: port 48500 tidak bisa di-bind" | Port dipakai aplikasi lain → ganti `network.tcp_port` di Settings, atau matikan pemakai port: `netstat -ano | findstr 48500` lalu `taskkill /PID <pid>`. |
| Viewer tidak menemukan host (discovery kosong) | 1) Firewall UDP 48501 belum dibuka; 2) berbeda subnet/VLAN atau AP isolation aktif di router/kantor; 3) discovery OFF di host. Solusi cepat: pakai IP langsung. |
| Koneksi ditolak "terkunci sementara" | Rate limiter aktif setelah ≥5 PIN salah dalam 10 menit. Tunggu (30 s → 15 menit) atau restart `ldhost.exe` (menghapus bucket di memori). |
| "fingerprint TLS host BERUBAH" di viewer | Host di-reinstall / identitas TLS dibuat ulang / (jarang) MITM. Bila Anda yakin reinstall: halaman Security viewer → **Lupakan Semua Host** → sambung ulang. |
| PIN lupa | Buat PIN baru di My Device (PIN lama ditimpa). PIN tidak bisa dilihat ulang by design. |
| Overlay konfirmasi tidak muncul, koneksi timeout 60 s | Overlay tertutup tanpa jawaban → dianggap Tolak. Buka kembali host dan jawab overlay. Untuk akses tanpa pengawasan, aktifkan unattended atau Terima + Ingat. |

## Runtime — Viewer

| Gejala | Solusi |
|---|---|
| "timeout menyambung ke IP" | Host tidak menyala / firewall host menutup TCP 48500 / IP salah → uji `Test-NetConnection <ip> -Port 48500`. |
| "device id tidak ditemukan di discovery" | Device ID format `LD-XXXXX-XXXXX` perlu discovery; segarkan daftar, atau isi IP langsung. |
| "autentikasi ditolak: PIN salah" | Periksa PIN; hati-hati lockout setelah 5 kegagalan. |
| Status "host menutup koneksi: sesi ditutup normal" | Host mematikan Remote Access / menutup sesi — perilaku normal. |
| "tidak ada ping dari host (koneksi mati)" | Jaringan terputus / host sleep. Tahan koneksi baru (Sambung Ulang). WireGuard sleep juga memicu ini. |

## Diagnostik

- **Log aplikasi**: `%LOCALAPPDATA%\LiteDeskRemote\logs\app-YYYYMMDD.log` (rotasi 8 MB).
- **Audit log**: `%LOCALAPPDATA%\LiteDeskRemote\logs\audit-latest.jsonl` — satu baris
  JSON per kejadian (connect, auth_ok/failed, denied_by_user, permission_denied...).
- **Connection History** di aplikasi menampilkan ring audit terbaru.
- Naikkan verbosity: ubah `util::LogLevel::Info` → `Debug` di `host/main.cpp` /
  `viewer/main.cpp` lalu build ulang (FASE 8 akan memindahkannya ke config).

## Reset data

```powershell
# matikan dulu ldhost.exe / ldviewer.exe, lalu:
Remove-Item -Recurse -Force "$env:LOCALAPPDATA\LiteDeskRemote"
```
Menghapus: config, PIN, identitas TLS (Device ID bisa berubah), fingerprint host
dikenal, log. Aman untuk mulai dari nol.

## Test mandiri

```powershell
.\scripts\run_tests.ps1     # unit + integrasi (TLS+auth localhost, discovery, CNG)
```
Bila `ldwin_tests` gagal padahal build sukses, sertakan output saat melapor — test
ini menutupi jalur persis yang dipakai aplikasi.
