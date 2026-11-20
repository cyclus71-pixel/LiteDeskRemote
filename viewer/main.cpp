// main.cpp — entry point LiteDesk Viewer.
#include "pages.h"
#include "../common/networking/net.h"
#include "../common/security/security.h"
#include "../common/util/config.h"
#include "../common/util/log.h"
#include "../common/util/time.h"

namespace ldr::viewer {
using util::Config;

int RunViewer() {
    Config::I().Load();
    util::Logger::Instance().Init(Config::I().LogDir(), util::LogLevel::Info);
    LLOG_I("LiteDesk Viewer mulai (FASE 1)");

    net::WsaInit wsa;

    auto ctx = std::make_unique<AppContext>();
    ctx->hosts.Load();

    ui::AppWindow win;
    win.Create(L"LiteDesk Remote - Viewer", 900, 620);
    AddAllPages(win, *ctx);
    win.SetPage(0);

    // Resolver: Device ID → ip/port dari cache discovery
    ctx->session.SetResolver(
        [&ctx](const std::string& id, std::string* ip, uint16_t* port) {
            return ctx->discovery.Resolve(id, ip, port);
        });

    // Discovery awal
    std::string derr;
    ctx->discovery.Start(Config::I().GetInt("network.discovery_port", 48501), &derr);
    if (!derr.empty()) LLOG_W("discovery: %s", derr.c_str());
    ctx->discovery.RefreshNow(1200);

    int64_t lastDiscovery = util::NowMs();

    win.SetPump([&ctx, &lastDiscovery]() -> std::vector<std::function<void()>> {
        std::vector<std::function<void()>> out;
        // discovery periodik ringan tiap 8 detik
        if (util::NowMs() - lastDiscovery > 8000) {
            lastDiscovery = util::NowMs();
            ctx->discovery.RefreshNow(600);
        }
        if (ctx->refresh) {
            ctx->refresh();
            out.push_back([]() {});
        }
        return out;
    });

    int rc = win.Run();
    ctx->session.Disconnect();
    ctx->discovery.Stop();
    LLOG_I("LiteDesk Viewer keluar");
    return rc;
}

} // namespace ldr::viewer

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    return ldr::viewer::RunViewer();
}
