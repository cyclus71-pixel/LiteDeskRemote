// window.cpp — AppWindow: frame utama, sidebar, halaman, overlay, DPI, pump event.
#include "window.h"
#include "../util/log.h"

#include <algorithm>
#include <dwmapi.h>
#include <windowsx.h>

namespace ldr::ui {

namespace {
const wchar_t* kClass = L"LiteDeskDarkWindow";

// kumpulan brush statis
HBRUSH InputBrush() {
    static HBRUSH b = CreateSolidBrush(Theme::INPUT_BG);
    return b;
}

void FillRectC(HDC hdc, const RECT& r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(hdc, &r, b);
    DeleteObject(b);
}

void DrawTextC(HDC hdc, const std::wstring& text, RECT r, HFONT f, COLORREF c, UINT flags) {
    HGDIOBJ of = SelectObject(hdc, f);
    COLORREF oc = SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &r, flags);
    SetTextColor(hdc, oc);
    SelectObject(hdc, of);
}
} // namespace

// ===== Create =====
bool AppWindow::Create(const wchar_t* title, int wDip, int hDip) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcStatic;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // posisi: sesuai ukuran DIP dengan DPI monitor utama
    UINT dpi = 96;
    {
        HDC hdc = GetDC(nullptr);
        dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(nullptr, hdc);
    }
    int w = static_cast<int>(wDip * dpi / 96.0);
    int h = static_cast<int>(hDip * dpi / 96.0);
    RECT wr{0, 0, w, h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = wr.right - wr.left, winH = wr.bottom - wr.top;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);

    hwnd_ = CreateWindowExW(0, kClass, title, WS_OVERLAPPEDWINDOW,
                            (sw - winW) / 2, (sh - winH) / 2, winW, winH,
                            nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    // dark titlebar (Windows 10/11)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd_, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));

    SetTimer(hwnd_, 1, 100, nullptr); // pump event 10x/detik
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int AppWindow::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

// ===== Pages =====
void AppWindow::AddPage(Page p) { pages_.push_back(std::move(p)); }

void AppWindow::SetPage(int idx) {
    if (idx < 0 || idx >= static_cast<int>(pages_.size())) return;
    if (cur_ == idx) return;
    cur_ = idx;
    RebuildPage();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::RebuildPage() {
    if (cur_ < 0 || cur_ >= static_cast<int>(pages_.size())) return;
    widgets_.clear(); // TextInput dtor menghancurkan EDIT child-nya
    if (pages_[cur_].Build) pages_[cur_].Build(*this);
}

// ===== Geometry =====
int AppWindow::ContentW() const {
    RECT r;
    GetClientRect(hwnd_, &r);
    return static_cast<int>((r.right - r.left) / theme_.S) - sidebarW_;
}
int AppWindow::ContentH() const {
    RECT r;
    GetClientRect(hwnd_, &r);
    return static_cast<int>((r.bottom - r.top) / theme_.S) - contentTop_;
}

POINT AppWindow::MouseDip(LPARAM lp) const {
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    return POINT{static_cast<LONG>((p.x - theme_.Px(sidebarW_)) / theme_.S),
                 static_cast<LONG>((p.y - contentTop_) / theme_.S)};
}

POINT AppWindow::MouseSidebarDip(LPARAM lp) const {
    POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    return POINT{static_cast<LONG>(p.x / theme_.S), static_cast<LONG>(p.y / theme_.S)};
}

// Konversi koordinat konten (DIP) → pixel absolut dalam hwnd (untuk EDIT children).
POINT AppWindow::PxOf(int dx, int dy) const {
    RECT r;
    GetClientRect(hwnd_, &r);
    return POINT{theme_.Px(sidebarW_) + theme_.Px(dx), contentTop_ + theme_.Px(dy)};
}

// ===== WndProc =====
LRESULT CALLBACK AppWindow::WndProcStatic(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    return self->Handle(msg, wp, lp);
}

LRESULT AppWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            theme_.Ensure(hwnd_);
            break;

        case WM_SIZE:
            // re-position EDIT children pada halaman aktif
            RebuildPage(); // sederhana & aman: build ulang layout sesuai ukuran baru
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case WM_DPICHANGED: {
            RECT* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER);
            theme_.Ensure(hwnd_);
            RebuildPage();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }

        case WM_TIMER:
            if (wp == 1 && pump_) {
                auto actions = pump_();
                if (!actions.empty()) {
                    for (auto& a : actions) a();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
            }
            break;

        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wp);
            SetTextColor(hdc, Theme::TEXT);
            SetBkColor(hdc, Theme::INPUT_BG);
            return reinterpret_cast<LRESULT>(InputBrush());
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd_, &ps);
            RECT r;
            GetClientRect(hwnd_, &r);
            // double buffer
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, r.right, r.bottom);
            HGDIOBJ ob = SelectObject(mem, bmp);
            OnPaint(mem, r);
            BitBlt(hdc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, ob);
            DeleteObject(bmp);
            DeleteDC(mem);
            EndPaint(hwnd_, &ps);
            break;
        }

        case WM_ERASEBKGND:
            return 1; // kita menggambar semua

        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            bool hit = false;
            auto& ws = hasOverlay_ ? overlayWidgets_ : widgets_;
            for (auto& wgt : ws) {
                if (!wgt->visible) continue;
                wgt->hover = wgt->Hit(MouseDip(lp));
                hit = hit || wgt->hover;
            }
            navHover_ = -1;
            POINT sp = MouseSidebarDip(lp);
            int navIdx = -1, seen = -1;
            for (size_t i = 0; i < pages_.size(); ++i) {
                if (pages_[i].navLabel.empty()) continue;
                ++seen;
                int y = theme_.Px(70 + seen * 44);
                if (sp.y >= y && sp.y < y + theme_.Px(36)) { navIdx = static_cast<int>(i); break; }
            }
            navHover_ = navIdx;
            if (hit || navIdx >= 0) SetCursor(LoadCursorW(nullptr, IDC_HAND));
            else SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }

        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            for (auto& wgt : widgets_) wgt->MouseLeave();
            for (auto& wgt : overlayWidgets_) wgt->MouseLeave();
            navHover_ = -1;
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case WM_LBUTTONDOWN: {
            SetFocus(hwnd_);
            bool any = false;
            auto& ws = hasOverlay_ ? overlayWidgets_ : widgets_;
            for (auto& wgt : ws) {
                if (!wgt->visible) continue;
                if (wgt->MouseDown(MouseDip(lp))) any = true;
            }
            if (any) InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }

        case WM_LBUTTONUP: {
            bool any = false;
            auto& ws = hasOverlay_ ? overlayWidgets_ : widgets_;
            for (auto& wgt : ws) {
                if (!wgt->visible) continue;
                if (wgt->MouseUp(MouseDip(lp))) any = true;
            }
            if (!hasOverlay_) {
                // klik sidebar → navigasi
                POINT sp = MouseSidebarDip(lp);
                if (sp.x < sidebarW_) {
                    int seen = -1;
                    for (size_t i = 0; i < pages_.size(); ++i) {
                        if (pages_[i].navLabel.empty()) continue;
                        ++seen;
                        int y = theme_.Px(70 + seen * 44);
                        if (sp.y >= y && sp.y < y + theme_.Px(36)) { SetPage(static_cast<int>(i)); break; }
                    }
                }
            }
            if (any) InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        }

        case WM_MOUSEWHEEL: {
            // arahkan ke ListPanel di bawah kursor
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd_, &pt);
            POINT dip{static_cast<LONG>((pt.x - theme_.Px(sidebarW_)) / theme_.S),
                      static_cast<LONG>((pt.y - contentTop_) / theme_.S)};
            for (auto& wgt : widgets_) {
                auto* list = dynamic_cast<ListPanel*>(wgt.get());
                if (list && list->Hit(dip)) {
                    list->Wheel(GET_WHEEL_DELTA_WPARAM(wp));
                    InvalidateRect(hwnd_, nullptr, FALSE);
                    break;
                }
            }
            break;
        }

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE && hasOverlay_) CloseOverlay();
            else if (wp == VK_RETURN && hasOverlay_ && !overlay_.buttons.empty()) {
                // tombol primary (atau pertama) dijalankan
                int pick = -1;
                for (size_t i = 0; i < overlay_.buttons.size(); ++i)
                    if (overlay_.buttons[i].primary) { pick = static_cast<int>(i); break; }
                if (pick < 0) pick = 0;
                auto fn = overlay_.buttons[pick].fn;
                CloseOverlay();
                if (fn) fn();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd_);
            break;

        case WM_DESTROY:
            KillTimer(hwnd_, 1);
            theme_.Cleanup();
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
    return 0;
}

// ===== Paint =====
void AppWindow::OnPaint(HDC hdc, const RECT& client) {
    FillRectC(hdc, client, Theme::BG);

    if (cur_ >= 0 && cur_ < static_cast<int>(pages_.size())) {
        const Page& page = pages_[cur_];
        int ox = theme_.Px(sidebarW_);
        // header halaman
        DrawTextC(hdc, page.title,
                  {ox + theme_.Px(28), theme_.Px(22),
                   client.right - theme_.Px(28), theme_.Px(48)},
                  theme_.Get(Theme::Font::H1), Theme::TEXT, DT_SINGLELINE);
        DrawTextC(hdc, page.subtitle,
                  {ox + theme_.Px(28), theme_.Px(48),
                   client.right - theme_.Px(28), theme_.Px(68)},
                  theme_.Get(Theme::Font::Small), Theme::SUB, DT_SINGLELINE);

        // garis pemisah header
        RECT line{ox + theme_.Px(28), theme_.Px(72),
                  client.right - theme_.Px(28), theme_.Px(72) + 1};
        FillRectC(hdc, line, Theme::BORDER);

        // koordinat paint widget: origin = (ox, contentTop)
        contentTop_ = theme_.Px(84);
        HRGN oldClip = CreateRectRgn(0, 0, 0, 0);
        GetClipRgn(hdc, oldClip);
        HRGN rg = CreateRectRgn(ox, contentTop_, client.right, client.bottom);
        SelectClipRgn(hdc, rg);
        for (auto& wgt : widgets_) wgt->Paint(hdc, theme_);
        if (page.PaintExtra) page.PaintExtra(*this, hdc);
        SelectClipRgn(hdc, oldClip);
        DeleteObject(rg);
        DeleteObject(oldClip);
    }

    PaintSidebar(hdc, client);
    if (hasOverlay_) PaintOverlay(hdc, client);
}

void AppWindow::PaintSidebar(HDC hdc, const RECT& client) {
    RECT sb{0, 0, theme_.Px(sidebarW_), client.bottom};
    FillRectC(hdc, sb, Theme::SIDEBAR);

    // logo
    DrawTextC(hdc, L"LiteDesk",
              {theme_.Px(20), theme_.Px(16), theme_.Px(sidebarW_), theme_.Px(40)},
              theme_.Get(Theme::Font::H2), Theme::TEXT, DT_SINGLELINE);
    RECT dot{theme_.Px(20), theme_.Px(46), theme_.Px(28), theme_.Px(54)};
    HBRUSH ab = CreateSolidBrush(Theme::ACCENT);
    FillRect(hdc, &dot, ab);
    DeleteObject(ab);

    int seen = -1;
    for (size_t i = 0; i < pages_.size(); ++i) {
        const Page& p = pages_[i];
        if (p.navLabel.empty()) continue;
        ++seen;
        int y = theme_.Px(70 + seen * 44);
        bool active = cur_ == static_cast<int>(i);
        bool hover = navHover_ == static_cast<int>(i);
        if (active) {
            RECT bg{theme_.Px(10), y, theme_.Px(sidebarW_ - 10), y + theme_.Px(36)};
            FillRectC(hdc, bg, Theme::PANEL);
            RECT bar{theme_.Px(10), y, theme_.Px(14), y + theme_.Px(36)};
            FillRectC(hdc, bar, Theme::ACCENT);
        } else if (hover) {
            RECT bg{theme_.Px(10), y, theme_.Px(sidebarW_ - 10), y + theme_.Px(36)};
            FillRectC(hdc, bg, RGB(24, 27, 33));
        }
        DrawTextC(hdc, p.navLabel,
                  {theme_.Px(28), y, theme_.Px(sidebarW_ - 12), y + theme_.Px(36)},
                  theme_.Get(active ? Theme::Font::BodyBold : Theme::Font::Body),
                  active ? Theme::TEXT : Theme::SUB, DT_SINGLELINE | DT_VCENTER);
    }
}

void AppWindow::ShowOverlay(Overlay o) {
    overlay_ = std::move(o);
    hasOverlay_ = true;
    overlayWidgets_.clear();
    overlayInput_.reset();
    LayoutOverlayWidgets();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void AppWindow::LayoutOverlayWidgets() {
    contentTop_ = theme_.Px(84); // pastikan valid meski dipanggil sebelum paint pertama
    // posisi diputuskan saat paint; buat tombol di tengah bawah overlay
    RECT r;
    GetClientRect(hwnd_, &r);
    int cx = (r.right - r.left) / 2;
    int rows = static_cast<int>(overlay_.lines.size());
    int boxH = theme_.Px(150 + rows * 22 + (overlay_.input ? 60 : 0));
    int y = (r.bottom - r.top) / 2 - boxH / 2;
    int btnY = y + boxH - theme_.Px(58);

    int n = static_cast<int>(overlay_.buttons.size());
    int btnW = theme_.Px(120), gap = theme_.Px(12);
    int total = n * btnW + (n - 1) * gap;
    int x0 = cx - total / 2;
    for (int i = 0; i < n; ++i) {
        auto b = std::make_unique<Button>(overlay_.buttons[i].label,
                                          overlay_.buttons[i].primary
                                              ? Button::Style::Primary
                                              : Button::Style::Ghost);
        b->x = static_cast<int>((x0 + i * (btnW + gap) - theme_.Px(sidebarW_)) / theme_.S);
        b->y = static_cast<int>((btnY - contentTop_) / theme_.S);
        b->w = 120;
        b->h = 34;
        auto fn = overlay_.buttons[i].fn;
        b->OnClick = [this, fn]() {
            CloseOverlay();
            if (fn) fn();
        };
        overlayWidgets_.push_back(std::move(b));
    }
    if (overlay_.input) {
        overlayInput_ = std::make_unique<TextInput>(
            hwnd_, false, false, overlay_.inputInitial);
        overlayInput_->Place(theme_, cx - theme_.Px(220), btnY - theme_.Px(52),
                             440, 34);
        if (overlay_.inputSubmit) {
            auto fn = overlay_.inputSubmit;
            overlayInput_->OnSubmit = [this, fn]() {
                if (overlay_.inputSink) *overlay_.inputSink = overlayInput_->Value();
                CloseOverlay();
                if (fn) fn();
            };
        }
    }
}

void AppWindow::PaintOverlay(HDC hdc, const RECT& client) {
    // dim latar (solid gelap; GDI tak punya alpha per-pixel tanpa buffer)
    RECT full{0, 0, client.right, client.bottom};
    FillRectC(hdc, full, RGB(12, 13, 17));

    RECT r;
    GetClientRect(hwnd_, &r);
    int rows = static_cast<int>(overlay_.lines.size());
    int boxH = theme_.Px(150 + rows * 22 + (overlay_.input ? 60 : 0));
    int boxW = theme_.Px(520);
    int x = (r.right - r.left) / 2 - boxW / 2;
    int y = (r.bottom - r.top) / 2 - boxH / 2;
    RECT box{x, y, x + boxW, y + boxH};

    // panel
    HBRUSH pb = CreateSolidBrush(Theme::PANEL);
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::BORDER);
    HGDIOBJ ob = SelectObject(hdc, pb);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, box.left, box.top, box.right, box.bottom, 12, 12);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(pb);
    DeleteObject(pen);

    DrawTextC(hdc, overlay_.title, {x + theme_.Px(24), y + theme_.Px(16),
                                    x + boxW - theme_.Px(24), y + theme_.Px(44)},
              theme_.Get(Theme::Font::H2), Theme::ACCENT, DT_SINGLELINE);
    int ly = y + theme_.Px(52);
    for (const auto& line : overlay_.lines) {
        DrawTextC(hdc, line, {x + theme_.Px(24), ly, x + boxW - theme_.Px(24), ly + theme_.Px(22)},
                  theme_.Get(Theme::Font::Body), Theme::TEXT, DT_SINGLELINE);
        ly += theme_.Px(22);
    }
    (void)rows;
}

void AppWindow::CloseOverlay() {
    hasOverlay_ = false;
    overlayWidgets_.clear();
    overlayInput_.reset();
    overlay_.inputSink = nullptr;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

} // namespace ldr::ui
