// pages.cpp — implementasi halaman host.
#include "pages.h"
#include "../common/protocol/messages.h"
#include "../common/util/config.h"
#include "../common/util/log.h"
#include "../common/util/time.h"

#include <algorithm>
#include <shellapi.h>
#include <shlobj.h>

namespace ldr::host {

using namespace ldr::ui;
using namespace ldr::util;

namespace {

// ===== helper =====

void CopyTextToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}

std::wstring StatRunning(AppContext& ctx) {
    return ctx.mgr.Running() ? L"AKTIF" : L"NONAKTIF";
}

// Salin konfigurasi UI → config.json
void SaveConfig() {
    std::string err;
    if (!Config::I().Save(&err)) LLOG_E("gagal simpan config: %s", err.c_str());
}

} // namespace

// ===== Dashboard =====
static void BuildDashboard(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();

    auto statusCard = std::make_unique<StatCard>(L"Remote Access", StatRunning(ctx),
                                                 L"klik tombol di bawah untuk ubah");
    auto connCard = std::make_unique<StatCard>(L"Perangkat Terhubung", L"0",
                                               L"batas sesi: " +
                                                   std::to_wstring(Config::I().GetInt(
                                                       "network.max_sessions", 3)));
    auto pinCard = std::make_unique<StatCard>(
        L"PIN", PinStoreInstance().Exists() ? L"terpasang" : L"BELUM DIATUR",
        L"atur di halaman My Device");

    int cw = (W - 28 * 2 - 16 * 2) / 3;
    statusCard->x = 28; statusCard->y = 16; statusCard->w = cw; statusCard->h = 92;
    connCard->x = 28 + cw + 16; connCard->y = 16; connCard->w = cw; connCard->h = 92;
    pinCard->x = 28 + (cw + 16) * 2; pinCard->y = 16; pinCard->w = cw; pinCard->h = 92;

    auto startBtn = std::make_unique<Button>(ctx.mgr.Running() ? L"Matikan Remote Access"
                                                              : L"Nyalakan Remote Access",
                                             ctx.mgr.Running() ? Button::Style::Danger
                                                               : Button::Style::Primary);
    startBtn->x = 28; startBtn->y = 124; startBtn->w = 240; startBtn->h = 40;
    auto* btn = startBtn.get();
    btn->OnClick = [btn, &ctx, &win]() {
        std::string err;
        if (ctx.mgr.Running()) {
            ctx.mgr.Stop();
        } else if (!ctx.mgr.Start(&err)) {
            AppWindow::Overlay o;
            o.title = L"Gagal menyalakan";
            o.lines.push_back(std::wstring(err.begin(), err.end()));
            o.buttons.push_back({L"Tutup", true, nullptr});
            win.ShowOverlay(std::move(o));
            return;
        }
        btn->text = ctx.mgr.Running() ? L"Matikan Remote Access" : L"Nyalakan Remote Access";
        btn->style = ctx.mgr.Running() ? Button::Style::Danger : Button::Style::Primary;
    };

    auto sessList = std::make_unique<ListPanel>();
    sessList->x = 28; sessList->y = 180; sessList->w = (W - 28 * 2 - 16) / 2; sessList->h = 210;

    auto actList = std::make_unique<ListPanel>();
    actList->x = 28 + sessList->w + 16; actList->y = 180; actList->w = sessList->w;
    actList->h = 210;

    auto refresh = [&ctx, s = statusCard.get(), c = connCard.get(), sl = sessList.get(),
                    al = actList.get()]() {
        s->value = StatRunning(ctx);
        size_t n = ctx.mgr.ActiveCount();
        c->value = std::to_wstring(n);
        sl->rows.clear();
        for (const auto& s2 : ctx.mgr.Snapshot()) {
            std::wstring ip(s2.ip.begin(), s2.ip.end());
            std::wstring st(s2.state.begin(), s2.state.end());
            std::wstring lat = s2.latencyMs >= 0 ? std::to_wstring(s2.latencyMs) + L" ms" : L"-";
            sl->AddRow({ip, st + L"  " + lat,
                        st == L"aktif" ? Theme::ACCENT : Theme::TEXT});
        }
        al->rows.clear();
        auto audit = ctx.mgr.AuditSnapshot();
        std::reverse(audit.begin(), audit.end());
        int shown = 0;
        for (const auto& a : audit) {
            if (shown++ >= 30) break;
            std::wstring ev(a.event.begin(), a.event.end());
            std::wstring ip(a.ip.begin(), a.ip.end());
            std::wstring dt(a.detail.begin(), a.detail.end());
            al->AddRow({ev, ip + L"  " + dt,
                        ev == L"auth_ok" ? Theme::ACCENT
                                          : (ev.find(L"failed") != std::wstring::npos
                                                 ? Theme::DANGER
                                                 : Theme::TEXT)});
        }
    };
    ctx.refresh = refresh;

    ws.push_back(std::move(statusCard));
    ws.push_back(std::move(connCard));
    ws.push_back(std::move(pinCard));
    ws.push_back(std::move(startBtn));
    ws.push_back(std::move(sessList));
    ws.push_back(std::move(actList));
}

// ===== My Device =====
static void BuildMyDevice(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();
    auto& theme = win.GetTheme();

    auto idLabel = std::make_unique<Label>(L"Device ID (bagikan ke viewer)", Theme::Font::Small,
                                           Theme::SUB);
    idLabel->x = 28; idLabel->y = 12; idLabel->w = 400; idLabel->h = 18;

    auto idValue = std::make_unique<Label>(std::wstring(ctx.mgr.DeviceId().begin(),
                                                        ctx.mgr.DeviceId().end()),
                                           Theme::Font::H1, Theme::ACCENT);
    idValue->x = 28; idValue->y = 30; idValue->w = 400; idValue->h = 34;

    auto copyBtn = std::make_unique<Button>(L"Salin ID", Button::Style::Ghost);
    copyBtn->x = 440; copyBtn->y = 30; copyBtn->w = 110; copyBtn->h = 34;
    copyBtn->OnClick = [&win, &ctx]() {
        std::wstring id(ctx.mgr.DeviceId().begin(), ctx.mgr.DeviceId().end());
        CopyTextToClipboard(win.Hwnd(), id);
    };

    auto fpLabel = std::make_unique<Label>(
        L"Fingerprint TLS: " + std::wstring(ctx.mgr.FingerprintHex().begin(),
                                            ctx.mgr.FingerprintHex().end()),
        Theme::Font::Small, Theme::SUB);
    fpLabel->x = 28; fpLabel->y = 70; fpLabel->w = W - 56; fpLabel->h = 18;

    // info mesin
    std::wstring ips;
    for (const auto& ip : net::MyIps()) {
        if (!ips.empty()) ips += L", ";
        ips += std::wstring(ip.begin(), ip.end());
    }
    auto hostLabel = std::make_unique<Label>(
        L"Hostname: " + std::wstring(net::HostNameUtf8().begin(), net::HostNameUtf8().end()),
        Theme::Font::Body, Theme::TEXT);
    hostLabel->x = 28; hostLabel->y = 104; hostLabel->w = W - 56; hostLabel->h = 22;
    auto ipLabel = std::make_unique<Label>(L"Alamat IP: " + ips, Theme::Font::Body, Theme::TEXT);
    ipLabel->x = 28; ipLabel->y = 128; ipLabel->w = W - 56; ipLabel->h = 22;

    // PIN
    auto pinHead = std::make_unique<Label>(L"PIN Remote Access", Theme::Font::H2, Theme::TEXT);
    pinHead->x = 28; pinHead->y = 170; pinHead->w = 300; pinHead->h = 26;

    auto pinStatus = std::make_unique<Label>(
        PinStoreInstance().Exists()
            ? L"PIN terpasang (hanya hash PBKDF2 tersimpan; PIN tidak dapat ditampilkan ulang)"
            : L"Belum ada PIN. Buat PIN agar viewer dapat terhubung.",
        Theme::Font::Small, Theme::SUB);
    pinStatus->x = 28; pinStatus->y = 198; pinStatus->w = W - 56; pinStatus->h = 18;

    auto genBtn = std::make_unique<Button>(L"Buat PIN Acak 6 Digit", Button::Style::Primary);
    genBtn->x = 28; genBtn->y = 224; genBtn->w = 200; genBtn->h = 36;
    genBtn->OnClick = [&win]() {
        std::string pin, err;
        if (!PinStoreInstance().Generate(6, &pin, &err)) {
            AppWindow::Overlay o;
            o.title = L"Gagal";
            o.lines.push_back(std::wstring(err.begin(), err.end()));
            o.buttons.push_back({L"Tutup", true, nullptr});
            win.ShowOverlay(std::move(o));
            return;
        }
        AppWindow::Overlay o;
        o.title = L"PIN Baru Dibuat";
        o.lines.push_back(L"PIN Anda: " + std::wstring(pin.begin(), pin.end()));
        o.lines.push_back(L"Catat sekarang - PIN hanya ditampilkan SEKALI ini.");
        o.lines.push_back(L"Untuk mengganti, buat PIN baru kapan saja.");
        o.buttons.push_back({L"Saya Sudah Mencatat", true, nullptr});
        win.ShowOverlay(std::move(o));
        SaveConfig();
    };

    auto customInput = std::make_unique<TextInput>(win.Hwnd(), false, true, L"");
    customInput->Place(theme, win.PxOf(240, 224).x, win.PxOf(240, 224).y, 140, 36);
    auto useBtn = std::make_unique<Button>(L"Pakai PIN Ini", Button::Style::Ghost);
    useBtn->x = 390; useBtn->y = 224; useBtn->w = 150; useBtn->h = 36;
    useBtn->OnClick = [&win, inp = customInput.get()]() {
        std::string pin(inp->Value().begin(), inp->Value().end());
        std::string err;
        if (!PinStoreInstance().SetCustom(pin, &err)) {
            AppWindow::Overlay o;
            o.title = L"Gagal";
            o.lines.push_back(std::wstring(err.begin(), err.end()));
            o.buttons.push_back({L"Tutup", true, nullptr});
            win.ShowOverlay(std::move(o));
            return;
        }
        AppWindow::Overlay o;
        o.title = L"PIN Dipasang";
        o.lines.push_back(L"PIN kustom terpasang. Catat PIN Anda.");
        o.buttons.push_back({L"OK", true, nullptr});
        win.ShowOverlay(std::move(o));
    };

    auto note = std::make_unique<Label>(
        L"Catatan: PIN berbeda per perangkat. PIN yang benar + konfirmasi host (bila aktif) "
        L"dibutuhkan untuk setiap koneksi baru.",
        Theme::Font::Small, Theme::SUB);
    note->x = 28; note->y = 276; note->w = W - 56; note->h = 18;

    ws.push_back(std::move(idLabel));
    ws.push_back(std::move(idValue));
    ws.push_back(std::move(copyBtn));
    ws.push_back(std::move(fpLabel));
    ws.push_back(std::move(hostLabel));
    ws.push_back(std::move(ipLabel));
    ws.push_back(std::move(pinHead));
    ws.push_back(std::move(pinStatus));
    ws.push_back(std::move(genBtn));
    ws.push_back(std::move(customInput));
    ws.push_back(std::move(useBtn));
    ws.push_back(std::move(note));
}

// ===== Settings =====
static void BuildSettings(AppWindow& win) {
    auto& ws = win.Widgets();
    int y = 16;

    auto netHead = std::make_unique<Label>(L"Jaringan", Theme::Font::H2, Theme::TEXT);
    netHead->x = 28; netHead->y = y; netHead->w = 300; netHead->h = 24;
    y += 36;

    auto portStep = std::make_unique<Stepper>(
        L"Port TCP (berlaku saat Remote Access dinyalakan ulang)",
        static_cast<int>(Config::I().GetInt("network.tcp_port", 48500)), 1024, 65535, 1);
    portStep->x = 28; portStep->y = y; portStep->w = 120;
    portStep->OnChanged = [](int v) {
        Config::I().SetInt("network.tcp_port", v);
        SaveConfig();
    };
    y += 40;

    auto maxStep = std::make_unique<Stepper>(
        L"Batas jumlah viewer simultan",
        static_cast<int>(Config::I().GetInt("network.max_sessions", 3)), 1, 10, 1);
    maxStep->x = 28; maxStep->y = y; maxStep->w = 120;
    maxStep->OnChanged = [](int v) {
        Config::I().SetInt("network.max_sessions", v);
        SaveConfig();
    };
    y += 40;

    auto discToggle = std::make_unique<Toggle>(
        L"Auto discovery LAN (UDP query/reply)",
        Config::I().GetBool("network.discovery_enabled", true));
    discToggle->x = 28; discToggle->y = y; discToggle->w = 420;
    discToggle->OnChanged = [](bool v) {
        Config::I().SetBool("network.discovery_enabled", v);
        SaveConfig();
    };
    y += 48;

    auto idleStep = std::make_unique<Stepper>(
        L"Auto-disconnect sesi tidak aktif (menit, 0 = mati)",
        static_cast<int>(Config::I().GetInt("security.idle_timeout_min", 0)), 0, 120, 5);
    idleStep->x = 28; idleStep->y = y; idleStep->w = 120;
    idleStep->OnChanged = [](int v) {
        Config::I().SetInt("security.idle_timeout_min", v);
        SaveConfig();
    };
    y += 52;

    auto qHead = std::make_unique<Label>(L"Kualitas Streaming (diterapkan di FASE 2)",
                                         Theme::Font::H2, Theme::TEXT);
    qHead->x = 28; qHead->y = y; qHead->w = 400; qHead->h = 24;
    y += 36;

    auto fpsStep = std::make_unique<Stepper>(
        L"Target FPS", static_cast<int>(Config::I().GetInt("quality.target_fps", 30)),
        15, 60, 15);
    fpsStep->x = 28; fpsStep->y = y; fpsStep->w = 120;
    fpsStep->OnChanged = [](int v) {
        Config::I().SetInt("quality.target_fps", v);
        SaveConfig();
    };
    y += 40;

    auto brStep = std::make_unique<Stepper>(
        L"Bitrate (kbps)", static_cast<int>(Config::I().GetInt("quality.bitrate_kbps", 8000)),
        1000, 50000, 1000);
    brStep->x = 28; brStep->y = y; brStep->w = 120;
    brStep->OnChanged = [](int v) {
        Config::I().SetInt("quality.bitrate_kbps", v);
        SaveConfig();
    };

    ws.push_back(std::move(netHead));
    ws.push_back(std::move(portStep));
    ws.push_back(std::move(maxStep));
    ws.push_back(std::move(discToggle));
    ws.push_back(std::move(idleStep));
    ws.push_back(std::move(qHead));
    ws.push_back(std::move(fpsStep));
    ws.push_back(std::move(brStep));
}

// ===== Security =====
static void BuildSecurity(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();
    int y = 16;

    auto confToggle = std::make_unique<Toggle>(
        L"Wajib konfirmasi untuk koneksi baru (disarankan)",
        Config::I().GetBool("security.require_confirmation", true));
    confToggle->x = 28; confToggle->y = y; confToggle->w = 460;
    confToggle->OnChanged = [](bool v) {
        Config::I().SetBool("security.require_confirmation", v);
        SaveConfig();
    };
    y += 44;

    auto unatToggle = std::make_unique<Toggle>(
        L"Unattended access (auto-terima bila PIN benar - risiko lebih tinggi)",
        Config::I().GetBool("security.unattended", false));
    unatToggle->x = 28; unatToggle->y = y; unatToggle->w = 560;
    unatToggle->OnChanged = [](bool v) {
        Config::I().SetBool("security.unattended", v);
        SaveConfig();
    };
    y += 52;

    auto permHead = std::make_unique<Label>(
        L"Permission default untuk viewer (izin yang diberikan saat auth berhasil)",
        Theme::Font::H2, Theme::TEXT);
    permHead->x = 28; permHead->y = y; permHead->w = W - 56; permHead->h = 24;
    y += 34;

    uint64_t mask = ctx.mgr.DefaultPermissions();
    auto mkToggle = [&](const wchar_t* label, uint64_t bit, int yy) {
        auto t = std::make_unique<Toggle>(label, (mask & bit) != 0);
        t->x = 28; t->y = yy; t->w = 400;
        t->OnChanged = [&ctx, bit](bool v) {
            uint64_t m = ctx.mgr.DefaultPermissions();
            ctx.mgr.SetDefaultPermissions(v ? (m | bit) : (m & ~bit));
        };
        ws.push_back(std::move(t));
    };
    mkToggle(L"Kontrol mouse", perm::Mouse, y); y += 32;
    mkToggle(L"Kontrol keyboard", perm::Keyboard, y); y += 32;
    mkToggle(L"Sinkronisasi clipboard", perm::Clipboard, y); y += 32;
    mkToggle(L"Transfer file", perm::FileTransfer, y); y += 32;
    mkToggle(L"Audio remote", perm::Audio, y); y += 44;

    auto acceptHead = std::make_unique<Label>(
        L"Perangkat di-auto-accept (PIN benar = langsung masuk tanpa konfirmasi)",
        Theme::Font::H2, Theme::TEXT);
    acceptHead->x = 28; acceptHead->y = y; acceptHead->w = W - 56; acceptHead->h = 24;
    y += 32;

    auto list = std::make_unique<ListPanel>();
    list->x = 28; list->y = y; list->w = (W - 28 * 2 - 16) / 2; list->h = 150;
    for (const auto& d : Config::I().GetStrArray("security.auto_accept_devices"))
        list->AddRow({std::wstring(d.begin(), d.end()), L"", Theme::TEXT});

    auto clearBtn = std::make_unique<Button>(L"Hapus Semua", Button::Style::Danger);
    clearBtn->x = 28 + list->w + 16; clearBtn->y = y; clearBtn->w = 150; clearBtn->h = 34;
    clearBtn->OnClick = []() {
        Config::I().SetStrArray("security.auto_accept_devices", {});
        SaveConfig();
    };

    ws.push_back(std::move(confToggle));
    ws.push_back(std::move(unatToggle));
    ws.push_back(std::move(permHead));
    ws.push_back(std::move(acceptHead));
    ws.push_back(std::move(list));
    ws.push_back(std::move(clearBtn));
}

// ===== Connection History =====
static void BuildHistory(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();

    auto list = std::make_unique<ListPanel>();
    list->x = 28; list->y = 16; list->w = W - 56; list->h = 300;
    auto audit = ctx.mgr.AuditSnapshot();
    std::reverse(audit.begin(), audit.end());
    for (const auto& a : audit) {
        std::string line = util::FormatWall(a.wallMs) + "  " + a.event + "  " + a.ip +
                           (a.deviceId.empty() ? "" : "  " + a.deviceId) +
                           (a.detail.empty() ? "" : "  " + a.detail);
        list->AddRow({std::wstring(line.begin(), line.end()), L"",
                      a.event == "auth_ok" ? Theme::ACCENT : Theme::TEXT});
    }

    auto openBtn = std::make_unique<Button>(L"Buka Folder Log", Button::Style::Ghost);
    openBtn->x = 28; openBtn->y = 326; openBtn->w = 170; openBtn->h = 34;
    openBtn->OnClick = [&win]() {
        std::wstring dir = Config::I().LogDir();
        ShellExecuteW(win.Hwnd(), L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    };

    auto note = std::make_unique<Label>(
        L"Audit lengkap: " + Config::I().AuditLogPath(), Theme::Font::Small, Theme::SUB);
    note->x = 28; note->y = 368; note->w = W - 56; note->h = 18;

    ws.push_back(std::move(list));
    ws.push_back(std::move(openBtn));
    ws.push_back(std::move(note));
}

// ===== Registrasi halaman =====
void AddAllPages(AppWindow& win, AppContext& ctx) {
    AppWindow::Page p1;
    p1.navLabel = L"Dashboard";
    p1.title = L"Dashboard";
    p1.subtitle = L"Ringkasan status remote access";
    p1.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildDashboard(w, ctx); };
    win.AddPage(std::move(p1));

    AppWindow::Page p2;
    p2.navLabel = L"My Device";
    p2.title = L"My Device";
    p2.subtitle = L"Identitas perangkat, PIN, dan info jaringan";
    p2.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildMyDevice(w, ctx); };
    win.AddPage(std::move(p2));

    AppWindow::Page p3;
    p3.navLabel = L"Settings";
    p3.title = L"Settings";
    p3.subtitle = L"Jaringan, sesi, dan kualitas streaming";
    p3.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildSettings(w); };
    win.AddPage(std::move(p3));

    AppWindow::Page p4;
    p4.navLabel = L"Security";
    p4.title = L"Security";
    p4.subtitle = L"Konfirmasi, permission, dan perangkat terpercaya";
    p4.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildSecurity(w, ctx); };
    win.AddPage(std::move(p4));

    AppWindow::Page p5;
    p5.navLabel = L"Connection History";
    p5.title = L"Connection History";
    p5.subtitle = L"Aktivitas koneksi (audit log)";
    p5.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildHistory(w, ctx); };
    win.AddPage(std::move(p5));
}

} // namespace ldr::host
