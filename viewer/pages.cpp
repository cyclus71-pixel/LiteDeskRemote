// pages.cpp — implementasi halaman viewer.
#include "pages.h"
#include "../common/util/config.h"
#include "../common/util/log.h"

#include <algorithm>

namespace ldr::viewer {

using namespace ldr::ui;
using namespace ldr::util;

namespace {

void SaveConfig() {
    std::string err;
    if (!Config::I().Save(&err)) LLOG_E("gagal simpan config: %s", err.c_str());
}

std::wstring S2W(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// ===== Remote Connection =====
static void BuildRemote(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();
    auto& theme = win.GetTheme();

    auto lbl1 = std::make_unique<Label>(L"Device ID atau IP host", Theme::Font::Small, Theme::SUB);
    lbl1->x = 28; lbl1->y = 10; lbl1->w = 300; lbl1->h = 16;

    auto endpoint = std::make_unique<TextInput>(win.Hwnd(), false, false, L"");
    endpoint->Place(theme, win.PxOf(28, 28).x, win.PxOf(28, 28).y, 300, 34);

    auto lbl2 = std::make_unique<Label>(L"PIN", Theme::Font::Small, Theme::SUB);
    lbl2->x = 344; lbl2->y = 10; lbl2->w = 100; lbl2->h = 16;

    auto pin = std::make_unique<TextInput>(win.Hwnd(), true, true, L"");
    pin->Place(theme, win.PxOf(344, 28).x, win.PxOf(344, 28).y, 120, 34);

    auto portStep = std::make_unique<Stepper>(L"Port host",
        static_cast<int>(Config::I().GetInt("network.tcp_port", 48500)), 1024, 65535, 1);
    portStep->x = 480; portStep->y = 28; portStep->w = 100;

    auto connectBtn = std::make_unique<Button>(L"Sambungkan", Button::Style::Primary);
    connectBtn->x = 28; connectBtn->y = 76; connectBtn->w = 150; connectBtn->h = 38;
    auto* cBtn = connectBtn.get();

    auto disconnectBtn = std::make_unique<Button>(L"Putuskan", Button::Style::Danger);
    disconnectBtn->x = 190; disconnectBtn->y = 76; disconnectBtn->w = 130; disconnectBtn->h = 38;
    disconnectBtn->OnClick = [&ctx]() { ctx.session.Disconnect(); };

    auto refreshBtn = std::make_unique<Button>(L"Segarkan Discovery", Button::Style::Ghost);
    refreshBtn->x = 336; refreshBtn->y = 76; refreshBtn->w = 180; refreshBtn->h = 38;
    refreshBtn->OnClick = [&ctx]() { ctx.discovery.RefreshNow(1200); };

    auto status = std::make_unique<Label>(L"Status: siap", Theme::Font::BodyBold, Theme::TEXT);
    status->x = 28; status->y = 126; status->w = W - 56; status->h = 22;

    auto rtt = std::make_unique<Label>(L"Latency: -", Theme::Font::Body, Theme::SUB);
    rtt->x = 28; rtt->y = 150; rtt->w = W - 56; rtt->h = 20;

    auto fp = std::make_unique<Label>(L"", Theme::Font::Small, Theme::SUB);
    fp->x = 28; fp->y = 172; fp->w = W - 56; fp->h = 18;

    auto listHead = std::make_unique<Label>(
        L"Perangkat ditemukan di LAN (klik untuk mengisi endpoint)",
        Theme::Font::H2, Theme::TEXT);
    listHead->x = 28; listHead->y = 198; listHead->w = W - 56; listHead->h = 24;

    auto list = std::make_unique<ListPanel>();
    list->x = 28; list->y = 226; list->w = W - 56; list->h = 220;

    // klik baris → isi endpoint
    // ListPanel memakai wheel; pemilihan baris: kita patch OnClick lewat Button overlay tak
    // tersedia di FASE 1 - gunakan tombol "Pakai yang pertama" sederhana:
    auto useBtn = std::make_unique<Button>(L"Pakai Device ID Terpilih", Button::Style::Ghost);
    useBtn->x = 28; useBtn->y = 456; useBtn->w = 220; useBtn->h = 32;
    useBtn->OnClick = [&ctx, inp = endpoint.get()]() {
        auto snap = ctx.discovery.Snapshot();
        if (!snap.empty()) inp->SetValue(S2W(snap.front().deviceId));
    };

    cBtn->OnClick = [&ctx, inp = endpoint.get(), pinIn = pin.get(), pStep = portStep.get()]() {
        std::string ep(inp->Value().begin(), inp->Value().end());
        std::string p(pinIn->Value().begin(), pinIn->Value().end());
        if (ep.empty()) return;
        ctx.session.Connect(ep, pStep->value, p);
    };

    ctx.refresh = [&ctx, st = status.get(), rt = rtt.get(), fpl = fp.get(),
                   ls = list.get(), cBtn]() {
        auto s = ctx.session.Snapshot();
        std::wstring stat = L"Status: " + S2W(s.status);
        if (s.state == ConnState::Active) stat += L"  [AKTIF]";
        st->text = stat;
        st->color = s.state == ConnState::Active   ? Theme::ACCENT
                    : s.state == ConnState::Failed ? Theme::DANGER
                                                   : Theme::TEXT;
        rt->text = L"Latency: " +
                   (s.latencyMs >= 0 ? std::to_wstring(s.latencyMs) + L" ms" : L"-");
        fpl->text = s.fingerprintNew
                        ? L"Fingerprint host baru disimpan (TOFU). " + S2W(s.fingerprintHex)
                        : (s.fingerprintHex.empty() ? L"" : L"Fingerprint host: " + S2W(s.fingerprintHex));
        ls->rows.clear();
        for (const auto& d : ctx.discovery.Snapshot()) {
            ls->AddRow({S2W(d.deviceId), S2W(d.hostname) + L"  " + S2W(d.ip) + L":" +
                                             std::to_wstring(d.tcpPort),
                        Theme::TEXT});
        }
        if (ls->rows.empty())
            ls->AddRow({L"(belum ada host ditemukan)",
                        L"pastikan host menyala & firewall UDP 48501", Theme::SUB});
        cBtn->text = s.state == ConnState::Active ? L"Sambung Ulang" : L"Sambungkan";
    };

    ws.push_back(std::move(lbl1));
    ws.push_back(std::move(lbl2));
    ws.push_back(std::move(endpoint));
    ws.push_back(std::move(pin));
    ws.push_back(std::move(portStep));
    ws.push_back(std::move(connectBtn));
    ws.push_back(std::move(disconnectBtn));
    ws.push_back(std::move(refreshBtn));
    ws.push_back(std::move(status));
    ws.push_back(std::move(rtt));
    ws.push_back(std::move(fp));
    ws.push_back(std::move(listHead));
    ws.push_back(std::move(list));
    ws.push_back(std::move(useBtn));
}

// ===== Settings =====
static void BuildSettings(AppWindow& win) {
    auto& ws = win.Widgets();
    int y = 16;

    auto head = std::make_unique<Label>(L"Kualitas (diterapkan saat FASE 2 aktif)",
                                        Theme::Font::H2, Theme::TEXT);
    head->x = 28; head->y = y; head->w = 460; head->h = 24;
    y += 38;

    auto fps = std::make_unique<Stepper>(L"Target FPS",
        static_cast<int>(Config::I().GetInt("quality.target_fps", 30)), 15, 60, 15);
    fps->x = 28; fps->y = y; fps->w = 120;
    fps->OnChanged = [](int v) {
        Config::I().SetInt("quality.target_fps", v);
        SaveConfig();
    };
    y += 40;

    auto br = std::make_unique<Stepper>(L"Bitrate (kbps)",
        static_cast<int>(Config::I().GetInt("quality.bitrate_kbps", 8000)), 1000, 50000, 1000);
    br->x = 28; br->y = y; br->w = 120;
    br->OnChanged = [](int v) {
        Config::I().SetInt("quality.bitrate_kbps", v);
        SaveConfig();
    };
    y += 40;

    auto preset = std::make_unique<Button>(
        L"Preset kualitas: " + S2W(Config::I().GetStr("quality.quality_preset", "balanced")),
        Button::Style::Ghost);
    preset->x = 28; preset->y = y; preset->w = 260; preset->h = 34;
    preset->OnClick = [p = preset.get()]() {
        std::string cur = Config::I().GetStr("quality.quality_preset", "balanced");
        std::string next = cur == "balanced" ? "high"
                           : cur == "high"   ? "low"
                                             : "balanced";
        Config::I().SetStr("quality.quality_preset", next);
        SaveConfig();
        p->text = L"Preset kualitas: " + S2W(next);
    };
    y += 52;

    auto note = std::make_unique<Label>(
        L"Preset dipakai encoder H.264 di FASE 2: low = hemat bandwidth, "
        L"balanced = default, high = kualitas terbaik.",
        Theme::Font::Small, Theme::SUB);
    note->x = 28; note->y = y; note->w = 560; note->h = 18;

    ws.push_back(std::move(head));
    ws.push_back(std::move(fps));
    ws.push_back(std::move(br));
    ws.push_back(std::move(preset));
    ws.push_back(std::move(note));
}

// ===== Security =====
static void BuildSecurity(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();

    auto head = std::make_unique<Label>(
        L"Host dikenal (pinning fingerprint TLS - TOFU)", Theme::Font::H2, Theme::TEXT);
    head->x = 28; head->y = 16; head->w = W - 56; head->h = 24;

    auto list = std::make_unique<ListPanel>();
    list->x = 28; list->y = 46; list->w = (W - 28 * 2 - 16) / 2; list->h = 260;
    for (const auto& h : ctx.hosts.Snapshot())
        list->AddRow({S2W(h.deviceId) + L"  " + S2W(h.name), S2W(h.fingerprintHex),
                      Theme::TEXT});
    if (list->rows.empty())
        list->AddRow({L"(belum ada host dikenal)", L"fingerprint disimpan saat koneksi pertama",
                      Theme::SUB});

    auto forgetBtn = std::make_unique<Button>(L"Lupakan Semua Host", Button::Style::Danger);
    forgetBtn->x = 28 + list->w + 16; forgetBtn->y = 46; forgetBtn->w = 170; forgetBtn->h = 34;
    forgetBtn->OnClick = [&ctx]() {
        ctx.hosts.Clear();
        ctx.hosts.Save();
    };

    auto note = std::make_unique<Label>(
        L"Fingerprint = SHA-256 sertifikat host. Bila host di-reinstall (fingerprint berubah), "
        L"hapus entri di sini lalu sambungkan ulang untuk memverifikasi fingerprint baru.",
        Theme::Font::Small, Theme::SUB);
    note->x = 28; note->y = 320; note->w = W - 56; note->h = 32;

    ws.push_back(std::move(head));
    ws.push_back(std::move(list));
    ws.push_back(std::move(forgetBtn));
    ws.push_back(std::move(note));
}

// ===== History =====
static void BuildHistory(AppWindow& win, AppContext& ctx) {
    auto& ws = win.Widgets();
    int W = win.ContentW();

    auto list = std::make_unique<ListPanel>();
    list->x = 28; list->y = 16; list->w = W - 56; list->h = 320;
    for (const auto& line : ctx.session.LogSnapshot())
        list->AddRow({S2W(line), L"", Theme::TEXT});

    auto note = std::make_unique<Label>(L"Log aplikasi: " + Config::I().AppLogPath(),
                                        Theme::Font::Small, Theme::SUB);
    note->x = 28; note->y = 346; note->w = W - 56; note->h = 18;

    ws.push_back(std::move(list));
    ws.push_back(std::move(note));
}

} // namespace

void AddAllPages(AppWindow& win, AppContext& ctx) {
    AppWindow::Page p0;
    p0.navLabel = L"Remote Connection";
    p0.title = L"Remote Connection";
    p0.subtitle = L"Sambungkan ke host via Device ID / IP (LAN)";
    p0.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildRemote(w, ctx); };
    win.AddPage(std::move(p0));

    AppWindow::Page p1;
    p1.navLabel = L"Settings";
    p1.title = L"Settings";
    p1.subtitle = L"Preferensi kualitas streaming";
    p1.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildSettings(w); };
    win.AddPage(std::move(p1));

    AppWindow::Page p2;
    p2.navLabel = L"Security";
    p2.title = L"Security";
    p2.subtitle = L"Host dikenal dan pinning TLS";
    p2.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildSecurity(w, ctx); };
    win.AddPage(std::move(p2));

    AppWindow::Page p3;
    p3.navLabel = L"Connection History";
    p3.title = L"Connection History";
    p3.subtitle = L"Riwayat koneksi sesi ini";
    p3.Build = [&ctx](AppWindow& w) { ctx.refresh = nullptr; BuildHistory(w, ctx); };
    win.AddPage(std::move(p3));
}

} // namespace ldr::viewer
