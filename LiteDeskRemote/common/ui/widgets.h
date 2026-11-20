// widgets.h — widget owner-draw ringan (GDI): Button, Toggle, Label, TextInput (EDIT
// native untuk caret/IME), ListPanel, StatCard, Stepper. Koordinat dalam DIP, relatif
// terhadap area konten jendela.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "theme.h"

namespace ldr::ui {

class Widget {
public:
    int x = 0, y = 0, w = 0, h = 0; // DIP, relatif area konten
    bool visible = true;
    bool enabled = true;
    bool hover = false;
    bool pressed = false;

    virtual ~Widget() = default;
    virtual void Paint(HDC hdc, const Theme& t) = 0;
    virtual bool MouseMove(POINT p) { hover = Hit(p); return hover; }
    virtual bool MouseDown(POINT p) { return Hit(p) && enabled; }
    virtual bool MouseUp(POINT p) { return Hit(p) && enabled; }
    virtual void MouseLeave() { hover = false; pressed = false; }
    virtual HCURSOR Cursor() const { return LoadCursorW(nullptr, IDC_ARROW); }
    bool Hit(POINT p) const {
        return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
    }
};

class Button : public Widget {
public:
    enum class Style { Primary, Ghost, Danger };
    std::wstring text;
    Style style = Style::Primary;
    Button(std::wstring t, Style s = Style::Primary) : text(std::move(t)), style(s) {}
    void Paint(HDC hdc, const Theme& t) override;
    bool MouseMove(POINT p) override { hover = Hit(p); return hover; }
    bool MouseDown(POINT p) override { pressed = Hit(p) && enabled; return pressed; }
    bool MouseUp(POINT p) override {
        bool was = pressed;
        pressed = false;
        if (was && Hit(p) && enabled && OnClick) OnClick();
        return was;
    }
    void MouseLeave() override { hover = false; pressed = false; }
    HCURSOR Cursor() const override {
        return LoadCursorW(nullptr, enabled ? IDC_HAND : IDC_ARROW);
    }
    std::function<void()> OnClick;
};

class Toggle : public Widget {
public:
    std::wstring text;
    bool checked = false;
    Toggle(std::wstring t, bool initial) : text(std::move(t)), checked(initial) { h = 24; }
    void Paint(HDC hdc, const Theme& t) override;
    bool MouseUp(POINT p) override {
        if (!Hit(p) || !enabled) return false;
        checked = !checked;
        if (OnChanged) OnChanged(checked);
        return true;
    }
    HCURSOR Cursor() const override { return LoadCursorW(nullptr, IDC_HAND); }
    std::function<void(bool)> OnChanged;
};

class Label : public Widget {
public:
    std::wstring text;
    Theme::Font font = Theme::Font::Body;
    COLORREF color = 0;
    int align = 0; // 0=kiri, 1=tengah
    Label(std::wstring t, Theme::Font f = Theme::Font::Body, COLORREF c = 0)
        : text(std::move(t)), font(f), color(c) {}
    void Paint(HDC hdc, const Theme& t) override;
};

// Input teks memakai EDIT control native (IME + caret gratis). Warna diatur oleh
// AppWindow lewat WM_CTLCOLOREDIT.
class TextInput : public Widget {
public:
    bool password = false;
    bool numeric = false;
    TextInput(HWND parent, bool pwd, bool num, const std::wstring& initial);
    ~TextInput() override;
    void Place(const Theme& t, int px, int py, int wDip, int hDip); // px absolut dalam hwnd
    std::wstring Value() const;
    void SetValue(const std::wstring& v);
    void SetVisible(bool on);
    HWND Edit() const { return edit_; }

    void Paint(HDC, const Theme&) override {} // EDIT menggambar sendiri
    std::function<void()> OnSubmit;           // Enter
private:
    HWND edit_ = nullptr;
    static LRESULT CALLBACK SubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};

class ListPanel : public Widget {
public:
    struct Row {
        std::wstring main;
        std::wstring sub;
        COLORREF color = 0; // 0 = default TEXT
    };
    std::vector<Row> rows;
    void AddRow(Row r) {
        rows.push_back(std::move(r));
        if (rows.size() > 1000) rows.erase(rows.begin(), rows.end() - 1000);
        scrollBottom_ = true;
    }
    void Clear() { rows.clear(); offset_ = 0; }
    void Paint(HDC hdc, const Theme& t) override;
    bool Wheel(int delta) {
        int maxOff = static_cast<int>(rows.size() * 22) - static_cast<int>(h / 22) - 1;
        if (maxOff < 0) maxOff = 0;
        offset_ -= delta / 40;
        if (offset_ < 0) offset_ = 0;
        if (offset_ > maxOff) offset_ = maxOff;
        scrollBottom_ = false;
        return true;
    }
private:
    int offset_ = 0;
    bool scrollBottom_ = true;
};

class StatCard : public Widget {
public:
    std::wstring title, value, sub;
    COLORREF accent = 0;
    StatCard(std::wstring t, std::wstring v, std::wstring s, COLORREF a = 0)
        : title(std::move(t)), value(std::move(v)), sub(std::move(s)), accent(a) {}
    void Paint(HDC hdc, const Theme& t) override;
};

class Stepper : public Widget {
public:
    std::wstring label;
    int value = 0, minV = 0, maxV = 100, step = 1;
    Stepper(std::wstring l, int v, int mn, int mx, int st)
        : label(std::move(l)), value(v), minV(mn), maxV(mx), step(st) { h = 30; }
    void Paint(HDC hdc, const Theme& t) override;
    bool MouseUp(POINT p) override {
        if (!Hit(p) || !enabled) return false;
        int third = w / 3;
        if (p.x < x + third) value = value - step < minV ? minV : value - step;
        else if (p.x > x + w - third) value = value + step > maxV ? maxV : value + step;
        if (OnChanged) OnChanged(value);
        return true;
    }
    HCURSOR Cursor() const override { return LoadCursorW(nullptr, IDC_HAND); }
    std::function<void(int)> OnChanged;
};

} // namespace ldr::ui
