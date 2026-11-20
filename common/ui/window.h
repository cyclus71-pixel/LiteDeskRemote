// window.h — jendela gelap dengan sidebar navigasi, halaman, overlay dialog.
// Double-buffered painting, PerMonitorV2 DPI, timer pump event dari thread lain.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "theme.h"
#include "widgets.h"

namespace ldr::ui {

class AppWindow {
public:
    struct Page {
        std::wstring navLabel;   // kosong = tidak tampil di sidebar
        std::wstring title;
        std::wstring subtitle;
        std::function<void(AppWindow&)> Build;      // dipanggil saat halaman aktif
        std::function<void(AppWindow&, HDC)> PaintExtra; // opsional, setelah widget
    };

    struct OverlayBtn {
        std::wstring label;
        bool primary = false;
        std::function<void()> fn;
    };
    struct Overlay {
        std::wstring title;
        std::vector<std::wstring> lines;
        std::vector<OverlayBtn> buttons;
        bool input = false;              // tampilkan 1 input teks di overlay
        std::wstring inputInitial;
        std::wstring* inputSink = nullptr;
        std::function<void()> inputSubmit;
    };

    bool Create(const wchar_t* title, int wDip, int hDip);
    int Run();
    void PostClose() { PostMessageW(hwnd_, WM_CLOSE, 0, 0); }

    void AddPage(Page p);
    void SetPage(int idx);
    int CurPage() const { return cur_; }

    Theme& GetTheme() { return theme_; }
    HWND Hwnd() const { return hwnd_; }
    std::vector<std::unique_ptr<Widget>>& Widgets() { return widgets_; }

    int ContentW() const;  // DIP
    int ContentH() const;  // DIP
    int SidebarW() const { return sidebarW_; }
    POINT PxOf(int dx, int dy) const; // koordinat konten DIP → pixel absolut hwnd

    void ShowOverlay(Overlay o);
    void CloseOverlay();
    bool HasOverlay() const { return hasOverlay_; }

    // WM_TIMER: drain callback dari thread lain (mis. EventQueue) → refresh bila ada.
    void SetPump(std::function<std::vector<std::function<void()>>() > pump) { pump_ = std::move(pump); }

private:
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint(HDC hdc, const RECT& client);
    void PaintSidebar(HDC hdc, const RECT& client);
    void PaintOverlay(HDC hdc, const RECT& client);
    void RebuildPage();
    void LayoutOverlayWidgets();
    POINT MouseDip(LPARAM lp) const;  // posisi mouse dalam DIP area konten
    POINT MouseSidebarDip(LPARAM lp) const;

    HWND hwnd_ = nullptr;
    Theme theme_;
    std::vector<Page> pages_;
    int cur_ = -1;
    std::vector<std::unique_ptr<Widget>> widgets_;
    std::vector<std::unique_ptr<Widget>> overlayWidgets_;
    Overlay overlay_;
    bool hasOverlay_ = false;
    int sidebarW_ = 210;
    int navHover_ = -1;
    int contentTop_ = 0;
    std::unique_ptr<TextInput> overlayInput_;
    std::function<std::vector<std::function<void()>>() > pump_;
    bool trackingMouse_ = false;
};

} // namespace ldr::ui
