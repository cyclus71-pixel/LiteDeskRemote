// pages.h — halaman GUI viewer: Remote Connection, Settings, Security, History.
#pragma once
#include <functional>

#include "../common/ui/window.h"
#include "../common/util/config.h"
#include "session.h"

namespace ldr::viewer {

struct AppContext {
    KnownHostStore hosts{util::Config::I().SecurityPath("known_hosts.json")};
    ViewerSession session{hosts};
    net::DiscoveryWatcher discovery;
    std::function<void()> refresh;
};

void AddAllPages(ui::AppWindow& win, AppContext& ctx);

} // namespace ldr::viewer
