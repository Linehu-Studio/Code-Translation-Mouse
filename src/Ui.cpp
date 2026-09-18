#include "Ui.h"

#include <cmath>

namespace ctm {
namespace {

constexpr COLORREF kBg = RGB(28, 28, 34);
constexpr COLORREF kBorder = RGB(70, 140, 230);
constexpr COLORREF kToken = RGB(255, 214, 102);
constexpr COLORREF kMeta = RGB(150, 154, 168);
constexpr COLORREF kText = RGB(236, 236, 240);

HFONT MakeFont(const wchar_t* family, int pt, int dpi, int weight = FW_NORMAL) {
    int px = -MulDiv(pt, dpi, 72);
    return CreateFontW(px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family);
}

int WindowDpi(HWND hwnd) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (fn && hwnd) return static_cast<int>(fn(hwnd));
    HDC hdc = GetDC(hwnd);
    int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return dpi > 0 ? dpi : 96;
}

void PlaceNearCursor(HWND hwnd, POINT cursor, SIZE size) {
    int x = cursor.x + 18;
    int y = cursor.y + 22;
    HMONITOR mon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + size.cx + 8 > mi.rcWork.right) x = cursor.x - size.cx - 18;
        if (y + size.cy + 8 > mi.rcWork.bottom) y = cursor.y - size.cy - 16;
        if (x < mi.rcWork.left) x = mi.rcWork.left + 8;
        if (y < mi.rcWork.top) y = mi.rcWork.top + 8;
    }
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, size.cx, size.cy,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOCOPYBITS);
}

HICON CreateGlyphIcon(const wchar_t* glyph, COLORREF bg, COLORREF fg) {
    const int s = 16;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = s;
    bmi.bmiHeader.biHeight = -s;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP color = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(dc, color));
    RECT r{0, 0, s, s};
    HBRUSH brush = CreateSolidBrush(bg);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, fg);
    HFONT font = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                             0, L"Microsoft YaHei UI");
    HGDIOBJ oldf = SelectObject(dc, font);
    DrawTextW(dc, glyph, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, oldf);
    DeleteObject(font);
    SelectObject(dc, old);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);

    HBITMAP mask = CreateBitmap(s, s, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

}  // namespace

bool TooltipWindow::Create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(kBg);
    wc.lpszClassName = L"CTM.Tooltip";
    wc.style = CS_DROPSHADOW | CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    SetLayeredWindowAttributes(hwnd_, 0, 242, LWA_ALPHA);
    return true;
}

LRESULT CALLBACK TooltipWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    TooltipWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<TooltipWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<TooltipWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self && msg == WM_PAINT) {
        self->OnPaint();
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TooltipWindow::Layout(HDC hdc, const Translation& tr, int dpi, SIZE& size, std::vector<RECT>& lines) const {
    const int pad = MulDiv(12, dpi, 96);
    const int gap = MulDiv(5, dpi, 96);
    const int max_w = MulDiv(440, dpi, 96);

    HFONT token_font = MakeFont(L"Cascadia Mono", 11, dpi, FW_SEMIBOLD);
    if (!token_font) token_font = MakeFont(L"Consolas", 11, dpi, FW_SEMIBOLD);
    HFONT meta_font = MakeFont(L"Microsoft YaHei UI", 9, dpi, FW_NORMAL);
    HFONT body_font = MakeFont(L"Microsoft YaHei UI", 12, dpi, FW_MEDIUM);

    auto measure = [&](HFONT font, const std::wstring& text, UINT fmt) -> SIZE {
        HGDIOBJ old = SelectObject(hdc, font);
        RECT rc{0, 0, max_w - pad * 2, 0};
        DrawTextW(hdc, text.c_str(), -1, &rc, fmt | DT_CALCRECT | DT_NOPREFIX);
        SelectObject(hdc, old);
        return SIZE{rc.right - rc.left, rc.bottom - rc.top};
    };

    SIZE s1 = measure(token_font, tr.token, DT_SINGLELINE);
    SIZE s2 = measure(meta_font, tr.kind_name, DT_SINGLELINE);
    SIZE s3 = measure(body_font, tr.summary, DT_WORDBREAK);

    int content_w = s1.cx;
    if (s2.cx > content_w) content_w = s2.cx;
    if (s3.cx > content_w) content_w = s3.cx;
    if (content_w > max_w - pad * 2) content_w = max_w - pad * 2;

    int y = pad;
    RECT r1{pad, y, pad + content_w, y + s1.cy};
    y += s1.cy + gap / 2;
    RECT r2{pad, y, pad + content_w, y + s2.cy};
    y += s2.cy + gap;
    RECT r3{pad, y, pad + content_w, y + s3.cy};
    y += s3.cy + pad;

    lines = {r1, r2, r3};
    size.cx = content_w + pad * 2;
    size.cy = y;

    DeleteObject(token_font);
    DeleteObject(meta_font);
    DeleteObject(body_font);
}

void TooltipWindow::OnPaint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);

    HBRUSH bg = CreateSolidBrush(kBg);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
    HGDIOBJ oldp = SelectObject(hdc, pen);
    HGDIOBJ oldb = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, oldb);
    SelectObject(hdc, oldp);
    DeleteObject(pen);

    int dpi = WindowDpi(hwnd_);
    HFONT token_font = MakeFont(L"Cascadia Mono", 11, dpi, FW_SEMIBOLD);
    if (!token_font) token_font = MakeFont(L"Consolas", 11, dpi, FW_SEMIBOLD);
    HFONT meta_font = MakeFont(L"Microsoft YaHei UI", 9, dpi, FW_NORMAL);
    HFONT body_font = MakeFont(L"Microsoft YaHei UI", 12, dpi, FW_MEDIUM);

    SIZE size{};
    std::vector<RECT> lines;
    Layout(hdc, current_, dpi, size, lines);
    SetBkMode(hdc, TRANSPARENT);

    auto draw = [&](HFONT font, COLORREF color, const RECT& r, const std::wstring& text, UINT fmt) {
        HGDIOBJ old = SelectObject(hdc, font);
        SetTextColor(hdc, color);
        RECT box = r;
        DrawTextW(hdc, text.c_str(), -1, &box, fmt | DT_NOPREFIX);
        SelectObject(hdc, old);
    };

    if (lines.size() >= 3) {
        draw(token_font, kToken, lines[0], current_.token, DT_SINGLELINE | DT_END_ELLIPSIS);
        draw(meta_font, kMeta, lines[1], current_.kind_name, DT_SINGLELINE);
        draw(body_font, kText, lines[2], current_.summary, DT_WORDBREAK);
    }

    DeleteObject(token_font);
    DeleteObject(meta_font);
    DeleteObject(body_font);
    EndPaint(hwnd_, &ps);
}

void TooltipWindow::Show(POINT cursor, const Translation& tr) {
    current_ = tr;
    HDC hdc = GetDC(hwnd_);
    SIZE size{};
    std::vector<RECT> lines;
    Layout(hdc, tr, WindowDpi(hwnd_), size, lines);
    ReleaseDC(hwnd_, hdc);
    PlaceNearCursor(hwnd_, cursor, size);
    InvalidateRect(hwnd_, nullptr, TRUE);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
}

void TooltipWindow::Hide() {
    if (!visible_) return;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

bool TrayIcon::Create(HWND callback_hwnd, HINSTANCE instance) {
    (void)instance;
    icon_on_ = CreateGlyphIcon(L"译", RGB(40, 90, 180), RGB(255, 255, 255));
    icon_off_ = CreateGlyphIcon(L"译", RGB(90, 90, 90), RGB(200, 200, 200));
    menu_ = CreatePopupMenu();
    AppendMenuW(menu_, MF_STRING | MF_CHECKED, kCmdToggle, L"启用悬停翻译\tCtrl+Alt+T");
    AppendMenuW(menu_, MF_STRING, kCmdDebug, L"打开调试控制台");
    AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu_, MF_STRING, kCmdExit, L"退出");

    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = callback_hwnd;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid_.uCallbackMessage = kCallbackMsg;
    nid_.hIcon = icon_on_;
    lstrcpynW(nid_.szTip, L"代码悬停翻译（已启用）", ARRAYSIZE(nid_.szTip));
    created_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    return created_;
}

void TrayIcon::Destroy() {
    if (created_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        created_ = false;
    }
    if (menu_) {
        DestroyMenu(menu_);
        menu_ = nullptr;
    }
    if (icon_on_) {
        DestroyIcon(icon_on_);
        icon_on_ = nullptr;
    }
    if (icon_off_) {
        DestroyIcon(icon_off_);
        icon_off_ = nullptr;
    }
}

void TrayIcon::SetEnabled(bool enabled) {
    nid_.hIcon = enabled ? icon_on_ : icon_off_;
    nid_.uFlags = NIF_ICON | NIF_TIP;
    lstrcpynW(nid_.szTip, enabled ? L"代码悬停翻译（已启用）" : L"代码悬停翻译（已暂停）", ARRAYSIZE(nid_.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
    CheckMenuItem(menu_, kCmdToggle, MF_BYCOMMAND | (enabled ? MF_CHECKED : MF_UNCHECKED));
}

void TrayIcon::ShowBalloon(const wchar_t* title, const wchar_t* text) {
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    lstrcpynW(nid_.szInfoTitle, title, ARRAYSIZE(nid_.szInfoTitle));
    lstrcpynW(nid_.szInfo, text, ARRAYSIZE(nid_.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

}  // namespace ctm
