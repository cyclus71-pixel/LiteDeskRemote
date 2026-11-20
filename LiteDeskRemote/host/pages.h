// pages.h — halaman GUI host: Dashboard, My Device, Settings, Security, Connection History.
#pragma once
#include <functional>
#include <future>
#include <memory>

#include "../common/ui/window.h"
#include "../common/util/events.h"
#include "session.h"

namespace ldr::host {

// Konteks aplikasi host (dipakai pages & main).
struct AppContext {
    SessionManager mgr;
    // permintaan konfirmasi dari thread sesi → UI (dengan promise jawaban)
    struct ConfirmItem {
        ConfirmRequest req;
        std::shared_ptr<std::promise<bool>> answer;
    };
    util::EventQueue<ConfirmItem> confirmQueue;
    // refresher UI halaman aktif (dipanggil oleh pump WM_TIMER); di-null saat ganti halaman
    std::function<void()> refresh;
};

void AddAllPages(ui::AppWindow& win, AppContext& ctx);

} // namespace ldr::host
