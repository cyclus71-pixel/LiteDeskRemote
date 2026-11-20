#include "theme.h"

namespace ldr::ui {

static HFONT MakeFont(HWND hwnd, int pt, bool bold, const wchar_t* face) {
    UINT dpi = 96;
    if (hwnd) {
        UINT (WINAPI * pGetDpi)(HWND) = nullptr;
        HMODULE u32 = GetModuleHandleW(L"user32.dll");
        if (u32)
            pGetDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(
                reinterpret_cast<void*>(GetProcAddress(u32, "GetDpiForWindow")));
        if (pGetDpi) dpi = pGetDpi(hwnd);
        if (dpi == 0) {
            HDC hdc = GetDC(hwnd);
            dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
            ReleaseDC(hwnd, hdc);
        }
    }
    return CreateFontW(-MulDiv(pt, static_cast<int>(dpi), 72), 0, 0, 0,
                       bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

void Theme::Ensure(HWND hwnd) {
    UINT dpi = 96;
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    UINT (WINAPI * pGetDpi)(HWND) = nullptr;
    if (u32)
        pGetDpi = reinterpret_cast<UINT(WINAPI*)(HWND)>(
            reinterpret_cast<void*>(GetProcAddress(u32, "GetDpiForWindow")));
    if (pGetDpi) dpi = pGetDpi(hwnd);
    if (dpi == 0) {
        HDC hdc = GetDC(hwnd);
        dpi = static_cast<UINT>(GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(hwnd, hdc);
    }
    if (dpi == this->dpi && hBody) return;
    Cleanup();
    this->dpi = dpi;
    S = static_cast<float>(dpi) / 96.0f;
    hBody = MakeFont(hwnd, 9, false, L"Segoe UI");
    hBodyBold = MakeFont(hwnd, 9, true, L"Segoe UI");
    hH1 = MakeFont(hwnd, 18, true, L"Segoe UI");
    hH2 = MakeFont(hwnd, 13, true, L"Segoe UI");
    hSmall = MakeFont(hwnd, 8, false, L"Segoe UI");
    hMono = MakeFont(hwnd, 9, false, L"Consolas");
    if (!hInputBrush) hInputBrush = CreateSolidBrush(INPUT_BG);
}

void Theme::Cleanup() {
    if (hBody) DeleteObject(hBody);
    if (hBodyBold) DeleteObject(hBodyBold);
    if (hH1) DeleteObject(hH1);
    if (hH2) DeleteObject(hH2);
    if (hSmall) DeleteObject(hSmall);
    if (hMono) DeleteObject(hMono);
    hBody = hBodyBold = hH1 = hH2 = hSmall = hMono = nullptr;
}

HFONT Theme::Get(Font f) const {
    switch (f) {
        case Font::BodyBold: return hBodyBold;
        case Font::H1: return hH1;
        case Font::H2: return hH2;
        case Font::Small: return hSmall;
        case Font::Mono: return hMono;
        default: return hBody;
    }
}

} // namespace ldr::ui
