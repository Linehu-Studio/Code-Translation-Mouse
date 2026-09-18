#pragma once

#include "Util.h"
#include "Translate.h"

namespace ctm {

class TooltipWindow {
public:
    bool Create(HINSTANCE instance);
    void Show(POINT cursor, const Translation& tr);
    void Hide();
    bool Visible() const { return visible_; }
    HWND Hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void OnPaint();
    void Layout(HDC hdc, const Translation& tr, int dpi, SIZE& size, std::vector<RECT>& lines) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Translation current_{};
    bool visible_ = false;
};

class TrayIcon {
public:
    bool Create(HWND callback_hwnd, HINSTANCE instance);
    void Destroy();
    void SetEnabled(bool enabled);
    void ShowBalloon(const wchar_t* title, const wchar_t* text);
    HMENU Menu() const { return menu_; }

    static constexpr UINT kCallbackMsg = WM_APP + 32;
    static constexpr UINT kCmdToggle = 1001;
    static constexpr UINT kCmdDebug = 1002;
    static constexpr UINT kCmdExit = 1003;

private:
    HICON MakeIcon(bool enabled);
    NOTIFYICONDATAW nid_{};
    HMENU menu_ = nullptr;
    HICON icon_on_ = nullptr;
    HICON icon_off_ = nullptr;
    bool created_ = false;
};

}  // namespace ctm
