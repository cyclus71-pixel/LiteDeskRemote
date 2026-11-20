// widgets.cpp — implementasi painter widget.
#include "widgets.h"

#include <cmath>
#include <commctrl.h>
#include <cwchar>

namespace ldr::ui {

namespace {

void RoundRectFill(HDC hdc, const RECT& r, int radiusDip, COLORREF fill,
                   COLORREF border, float scale) {
    int rad = static_cast<int>(radiusDip * scale);
    if (rad > (r.bottom - r.top) / 2) rad = (r.bottom - r.top) / 2;
    if (rad > (r.right - r.left) / 2) rad = (r.right - r.left) / 2;
    if (rad < 1) rad = 1;
    HBRUSH b = CreateSolidBrush(fill);
    HPEN pen = border ? CreatePen(PS_SOLID, 1, border) : (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, pen);
    RoundRect(hdc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    if (border) DeleteObject(pen);
    DeleteObject(b);
}

void DrawTextSimple(HDC hdc, const std::wstring& text, const RECT& r, HFONT font,
                    COLORREF color, UINT flags, int ox = 0, int oy = 0) {
    HGDIOBJ of = SelectObject(hdc, font);
    COLORREF oc = SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    RECT rr = r;
    OffsetRect(&rr, ox, oy);
    DrawTextW(hdc, text.c_str(), -1, &rr, flags);
    SetTextColor(hdc, oc);
    SelectObject(hdc, of);
}

} // namespace

// ===== Button =====
void Button::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    RECT r{x, y, x + w, y + h};
    COLORREF fill = Theme::PANEL2, txt = Theme::TEXT;
    if (style == Style::Primary) {
        fill = enabled ? Theme::ACCENT : Theme::ACCENT_DEEP;
        txt = RGB(12, 20, 16);
    } else if (style == Style::Danger) {
        fill = enabled ? Theme::DANGER : RGB(120, 45, 60);
        txt = RGB(28, 10, 14);
    }
    if (enabled && hover && style == Style::Ghost) fill = Theme::PANEL2;
    if (enabled && pressed) fill = (style == Style::Ghost) ? Theme::BORDER : (style == Style::Danger ? RGB(200, 60, 85) : Theme::ACCENT_DEEP);
    RoundRectFill(hdc, r, 8, fill, enabled && hover ? Theme::BORDER : 0, t.S);
    DrawTextSimple(hdc, text, r, t.Get(Theme::Font::BodyBold), txt,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// ===== Toggle =====
void Toggle::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    int knobR = 9;
    int trackW = 40, trackH = 20;
    RECT track{x, y + (h - trackH) / 2, x + trackW, y + (h - trackH) / 2 + trackH};
    COLORREF trackC = checked ? Theme::ACCENT : Theme::BORDER;
    if (!enabled) trackC = Theme::PANEL2;
    RoundRectFill(hdc, track, 10, trackC, 0, t.S);
    int cx = checked ? track.right - knobR - 3 : track.left + knobR + 3;
    int cy = (track.top + track.bottom) / 2;
    HBRUSH kb = CreateSolidBrush(checked ? RGB(15, 26, 20) : Theme::SUB);
    HPEN pen = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ ob = SelectObject(hdc, kb);
    HGDIOBJ op = SelectObject(hdc, pen);
    Ellipse(hdc, cx - knobR, cy - knobR, cx + knobR, cy + knobR);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(kb);

    RECT tr{x + trackW + 10, y, x + w, y + h};
    DrawTextSimple(hdc, text, tr, t.Get(Theme::Font::Body),
                   enabled ? Theme::TEXT : Theme::SUB, DT_VCENTER | DT_SINGLELINE);
}

// ===== Label =====
void Label::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    RECT r{x, y, x + w, y + h};
    COLORREF c = color ? color : Theme::TEXT;
    UINT flags = DT_SINGLELINE | DT_VCENTER;
    if (align == 1) flags |= DT_CENTER;
    DrawTextSimple(hdc, text, r, t.Get(font), c, flags);
}

// ===== TextInput =====
TextInput::TextInput(HWND parent, bool pwd, bool num, const std::wstring& initial)
    : password(pwd), numeric(num) {
    DWORD style = WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL;
    if (pwd) style |= ES_PASSWORD;
    if (num) style |= ES_NUMBER;
    edit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(), style,
                            0, 0, 10, 10, parent, nullptr,
                            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)),
                            nullptr);
    if (edit_) {
        SetWindowSubclass(edit_, SubclassProc, 1,
                          reinterpret_cast<DWORD_PTR>(this));
        SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L""));
        ShowWindow(edit_, SW_SHOW);
    }
}

TextInput::~TextInput() {
    if (edit_) DestroyWindow(edit_);
}

void TextInput::Place(const Theme& t, int px, int py, int wDip, int hDip) {
    x = 0; y = 0; // DIP coords tak dipakai (paint kosong); posisi via pixel
    w = wDip; h = hDip;
    if (edit_)
        MoveWindow(edit_, px, py, t.Px(wDip), t.Px(hDip), TRUE);
}

std::wstring TextInput::Value() const {
    wchar_t buf[512] = {0};
    int n = GetWindowTextW(edit_, buf, 511);
    return std::wstring(buf, buf + n);
}

void TextInput::SetValue(const std::wstring& v) { SetWindowTextW(edit_, v.c_str()); }

void TextInput::SetVisible(bool on) {
    visible = on;
    if (edit_) ShowWindow(edit_, on ? SW_SHOW : SW_HIDE);
}

LRESULT CALLBACK TextInput::SubclassProc(HWND h, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR data) {
    auto* self = reinterpret_cast<TextInput*>(data);
    if (msg == WM_KEYDOWN && wp == VK_RETURN && self && self->OnSubmit) {
        self->OnSubmit();
        return 0;
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// ===== ListPanel =====
void ListPanel::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    RECT r{x, y, x + w, y + h};
    // Panel + border
    HBRUSH b = CreateSolidBrush(Theme::PANEL);
    HPEN pen = CreatePen(PS_SOLID, 1, Theme::BORDER);
    HGDIOBJ ob = SelectObject(hdc, b);
    HGDIOBJ op = SelectObject(hdc, pen);
    Rectangle(hdc, r.left, r.top, r.right, r.bottom);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(b);
    DeleteObject(pen);

    if (scrollBottom_) {
        int visible = static_cast<int>(h / 22) - 1;
        offset_ = static_cast<int>(rows.size()) - visible - 1;
        if (offset_ < 0) offset_ = 0;
        scrollBottom_ = false;
    }

    int rowH = static_cast<int>(22 * t.S);
    RECT clip{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
    HRGN oldClip = CreateRectRgn(0, 0, 0, 0);
    GetClipRgn(hdc, oldClip);
    HRGN rg = CreateRectRgnIndirect(&clip);
    SelectClipRgn(hdc, rg);

    for (size_t i = offset_; i < rows.size(); ++i) {
        int py = static_cast<int>(r.top + 1 + (i - offset_) * 22 * t.S);
        if (py + rowH > clip.bottom) break;
        const Row& row = rows[i];
        DrawTextSimple(hdc, row.main, {r.left + 10, py, r.right - 10, py + rowH},
                       t.Get(Theme::Font::Body), row.color ? row.color : Theme::TEXT,
                       DT_SINGLELINE | DT_VCENTER);
        if (!row.sub.empty()) {
            int mainW = static_cast<int>(row.main.size() * 6.5 * t.S) + 20;
            DrawTextSimple(hdc, row.sub,
                           {r.left + mainW, py, r.right - 10, py + rowH},
                           t.Get(Theme::Font::Small), Theme::SUB,
                           DT_SINGLELINE | DT_VCENTER);
        }
    }
    SelectClipRgn(hdc, oldClip);
    DeleteObject(rg);
    DeleteObject(oldClip);
}

// ===== StatCard =====
void StatCard::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    RECT r{x, y, x + w, y + h};
    RoundRectFill(hdc, r, 10, Theme::PANEL, Theme::BORDER, t.S);
    // strip aksen kiri
    RECT strip{x + 12, y + 14, x + 16, y + h - 14};
    HBRUSH ab = CreateSolidBrush(accent ? accent : Theme::ACCENT);
    FillRect(hdc, &strip, ab);
    DeleteObject(ab);

    DrawTextSimple(hdc, title, {x + 26, y + 10, x + w - 12, y + 30},
                   t.Get(Theme::Font::Small), Theme::SUB, DT_SINGLELINE);
    DrawTextSimple(hdc, value, {x + 26, y + 30, x + w - 12, y + h - 26},
                   t.Get(Theme::Font::H2), Theme::TEXT, DT_SINGLELINE);
    DrawTextSimple(hdc, sub, {x + 26, y + h - 26, x + w - 12, y + h - 8},
                   t.Get(Theme::Font::Small), Theme::SUB, DT_SINGLELINE);
}

// ===== Stepper =====
void Stepper::Paint(HDC hdc, const Theme& t) {
    if (!visible) return;
    RECT r{x, y, x + w, y + h};
    RoundRectFill(hdc, r, 8, Theme::PANEL, Theme::BORDER, t.S);
    int third = w / 3;
    DrawTextSimple(hdc, L"\u2013", {x, y, x + third, y + h}, t.Get(Theme::Font::BodyBold),
                   Theme::TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    wchar_t buf[32];
    swprintf(buf, 32, L"%d", value);
    DrawTextSimple(hdc, buf, {x + third, y, x + 2 * third, y + h},
                   t.Get(Theme::Font::BodyBold), Theme::ACCENT,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextSimple(hdc, L"+", {x + 2 * third, y, x + w, y + h}, t.Get(Theme::Font::BodyBold),
                   Theme::TEXT, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DrawTextSimple(hdc, label, {x + w + 12, y, x + w + 320, y + h},
                   t.Get(Theme::Font::Body), Theme::SUB, DT_SINGLELINE | DT_VCENTER);
}

} // namespace ldr::ui
