// theme.h — palet gelap + font per-DPI. Semua ukuran widget dalam DIP, dikonversi
// ke pixel lewat Theme::Px() (mengikuti PerMonitorV2).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace ldr::ui {

struct Theme {
    // Palet: background gelap, panel, aksen hijau (spesifikasi §8).
    static constexpr COLORREF BG        = RGB(20, 22, 27);
    static constexpr COLORREF PANEL     = RGB(30, 33, 40);
    static constexpr COLORREF PANEL2    = RGB(38, 42, 51);
    static constexpr COLORREF BORDER    = RGB(47, 51, 61);
    static constexpr COLORREF TEXT      = RGB(233, 235, 240);
    static constexpr COLORREF SUB       = RGB(150, 158, 170);
    static constexpr COLORREF ACCENT    = RGB(49, 208, 126);   // hijau
    static constexpr COLORREF ACCENT_DEEP = RGB(28, 132, 82);
    static constexpr COLORREF DANGER    = RGB(244, 81, 108);
    static constexpr COLORREF WARN      = RGB(245, 166, 35);
    static constexpr COLORREF INPUT_BG  = RGB(25, 28, 34);
    static constexpr COLORREF SIDEBAR   = RGB(17, 19, 23);

    enum class Font { Body, BodyBold, H1, H2, Small, Mono };

    UINT dpi = 96;
    float S = 1.0f;
    HFONT hBody = nullptr, hBodyBold = nullptr, hH1 = nullptr, hH2 = nullptr,
          hSmall = nullptr, hMono = nullptr;
    HBRUSH hInputBrush = nullptr;

    void Ensure(HWND hwnd);     // sesuaikan DPI jendela; buat font bila perlu
    void Cleanup();
    int Px(int dip) const { return static_cast<int>(dip * S + 0.5f); }
    HFONT Get(Font f) const;
};

} // namespace ldr::ui
