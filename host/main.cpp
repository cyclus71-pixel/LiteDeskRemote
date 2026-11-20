// main.cpp — entry point LiteDesk Host.
// Inisialisasi: config → log → deviceId → identitas TLS → PIN (first run) → UI.
// Konfirmasi akses: thread sesi → EventQueue → overlay UI → promise.
#include "pages.h"
#include "../common/networking/net.h"
#include "../common/security/security.h"
#include "../common/util/config.h"
#include "../common/util/log.h"
#include "../common/util/time.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    using namespace ldr;
    using namespace ldr::util;
    using host::AppContext;

    // 1. Config + log
    Config::I().Load();
    Logger::Instance().Init(Config::I().LogDir(), util::LogLevel::Info);
    LLOG_I("LiteDesk Host mulai (FASE 1)");

    // 2. Winsock
    net::WsaInit wsa;

    // 3. Konteks aplikasi
    auto ctx = std::make_unique<AppContext>();
    std::string err;
    if (!ctx->mgr.Initialize(&err)) {
        Logger::Instance().Error("inisialisasi host gagal: %s", err.c_str());
        MessageBoxW(nullptr,
                    std::wstring(err.begin(), err.end()).c_str(),
                    L"LiteDesk Host - Gagal Inisialisasi", MB_ICONERROR);
        return 1;
    }

    // 4. PIN first-run
    bool firstPin = false;
    std::string newPin;
    {
        auto& pin = host::PinStoreInstance();
        if (!pin.Exists()) {
            firstPin = pin.Generate(6, &newPin, &err);
            if (!firstPin)
                LLOG_E("gagal generate PIN pertama: %s", err.c_str());
        }
    }

    // 5. UI
    ui::AppWindow win;
    win.Create(L"LiteDesk Remote - Host", 880, 600);
    host::AddAllPages(win, *ctx);
    win.SetPage(0);

    // 6. Provider konfirmasi: thread sesi → antrean → UI overlay
    ctx->mgr.SetConfirmProvider([&ctx](const host::ConfirmRequest& req) {
        auto promise = std::make_shared<std::promise<bool>>();
        auto future = promise->get_future();
        ctx->confirmQueue.Push({req, promise});
        auto st = future.wait_for(std::chrono::seconds(60));
        if (st == std::future_status::ready) return future.get();
        return false; // timeout = tolak
    });

    // 7. Pump event (WM_TIMER 100 ms)
    win.SetPump([&ctx, &win]() -> std::vector<std::function<void()>> {
        std::vector<std::function<void()>> out;
        // 7a. konfirmasi masuk → overlay
        for (auto& item : ctx->confirmQueue.Drain()) {
            auto& req = item.req;
            std::wstring ip(req.ip.begin(), req.ip.end());
            std::wstring dev(req.deviceId.begin(), req.deviceId.end());
            std::wstring name(req.deviceName.empty() ? L"(tanpa nama)"
                                                     : std::wstring(req.deviceName.begin(),
                                                                    req.deviceName.end()));
            std::wstring perm(req.permissions.begin(), req.permissions.end());

            auto answer = item.answer;
            ui::AppWindow::Overlay o;
            o.title = L"Permintaan Koneksi Remote";
            o.lines.push_back(L"Perangkat : " + name + L" (" + dev + L")");
            o.lines.push_back(L"Alamat    : " + ip);
            o.lines.push_back(L"Izin      : " + perm);
            o.lines.push_back(L"Terima koneksi ini?");
            o.buttons.push_back(
                {L"Terima", true, [answer]() { answer->set_value(true); }});
            o.buttons.push_back(
                {L"Terima + Ingat", false, [&ctx, req, answer]() {
                     auto list = Config::I().GetStrArray("security.auto_accept_devices");
                     if (std::find(list.begin(), list.end(), req.deviceId) == list.end()) {
                         list.push_back(req.deviceId);
                         Config::I().SetStrArray("security.auto_accept_devices", list);
                         std::string e;
                         Config::I().Save(&e);
                     }
                     answer->set_value(true);
                 }});
            o.buttons.push_back(
                {L"Tolak", false, [answer]() { answer->set_value(false); }});
            win.ShowOverlay(std::move(o));
            out.push_back([]() {});
        }
        // 7b. refresh halaman aktif
        if (ctx->refresh) {
            ctx->refresh();
            out.push_back([]() {});
        }
        return out;
    });

    // 8. Overlay PIN pertama
    if (firstPin) {
        ui::AppWindow::Overlay o;
        o.title = L"PIN Remote Access Pertama";
        o.lines.push_back(L"PIN Anda: " + std::wstring(newPin.begin(), newPin.end()));
        o.lines.push_back(L"PIN hanya ditampilkan SEKALI ini. Catat sekarang.");
        o.lines.push_back(L"Viewer membutuhkan PIN ini untuk terhubung.");
        o.buttons.push_back({L"Saya Sudah Mencatat", true, nullptr});
        win.ShowOverlay(std::move(o));
    }

    LLOG_I("host UI siap");
    int rc = win.Run();

    ctx->mgr.Stop();
    LLOG_I("LiteDesk Host keluar");
    return rc;
}
