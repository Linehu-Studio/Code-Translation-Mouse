#include "App.h"

namespace ctm {
namespace {

const wchar_t kWndClass[] = L"CTM.MessageWindow";
const wchar_t kMutexName[] = L"Local\\Linehu.CodeTranslationMouse";

bool IsLikelyExclusiveFullscreen(POINT pt) {
    HWND hwnd = WindowFromPoint(pt);
    if (!hwnd) return false;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    RECT wr{};
    GetWindowRect(root, &wr);
    HMONITOR mon = MonitorFromWindow(root, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    LONG style = GetWindowLongW(root, GWL_STYLE);
    bool covers = wr.left <= mi.rcMonitor.left && wr.top <= mi.rcMonitor.top && wr.right >= mi.rcMonitor.right &&
                  wr.bottom >= mi.rcMonitor.bottom;
    return covers && !(style & WS_CAPTION);
}

}  // namespace

App& App::Instance() {
    static App app;
    return app;
}

int App::Run(HINSTANCE instance, bool debug) {
    if (!Init(instance, debug)) {
        Shutdown();
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    Shutdown();
    return static_cast<int>(msg.wParam);
}

bool App::Init(HINSTANCE instance, bool debug) {
    instance_ = instance;
    debug_ = debug;

    mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex_ && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
                    L"代码悬停翻译已经在运行。\n请查看系统托盘中的「译」图标。\n\n"
                    L"如需查看调试输出，请先右键托盘图标退出，再以 --debug 启动。",
                    L"代码悬停翻译", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }

    if (debug_) EnableDebugConsole();
    if (debug_) DebugLog(L"[app] 调试模式已启动");

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(0, kWndClass, L"CodeTranslationMouse", WS_OVERLAPPED, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                            instance, this);
    if (!hwnd_) return false;

    SystemParametersInfoW(SPI_GETSCREENREADER, 0, &screen_reader_was_, 0);
    SystemParametersInfoW(SPI_SETSCREENREADER, TRUE, nullptr, SPIF_SENDCHANGE);
    restored_screen_reader_ = true;

    capture_.Init();
    if (!translator_.Load()) {
        DebugLog(L"[app] 词典为空，翻译能力会很有限");
    }
    if (!tooltip_.Create(instance)) {
        MessageBoxW(nullptr, L"无法创建翻译浮窗。", L"代码悬停翻译", MB_ICONERROR);
        return false;
    }
    tray_.Create(hwnd_, instance);
    tray_.ShowBalloon(L"代码悬停翻译", L"已在后台运行。把鼠标移到代码单词上即可看到翻译。\nCtrl+Alt+T 可开关。");

    RegisterHotKey(hwnd_, kHotkeyId, MOD_CONTROL | MOD_ALT, 'T');
    SetTimer(hwnd_, kTimerId, 30, nullptr);
    return true;
}

void App::Shutdown() {
    if (hwnd_) {
        KillTimer(hwnd_, kTimerId);
        UnregisterHotKey(hwnd_, kHotkeyId);
    }
    tray_.Destroy();
    tooltip_.Hide();
    if (restored_screen_reader_) {
        SystemParametersInfoW(SPI_SETSCREENREADER, screen_reader_was_ ? TRUE : FALSE, nullptr, SPIF_SENDCHANGE);
        restored_screen_reader_ = false;
    }
    if (mutex_) {
        ReleaseMutex(mutex_);
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

void App::ToggleEnabled() {
    enabled_ = !enabled_;
    tray_.SetEnabled(enabled_);
    if (!enabled_) HideTooltip();
    DebugLog(enabled_ ? L"[app] 已启用" : L"[app] 已暂停");
}

void App::ToggleDebug() {
    EnableDebugConsole();
    debug_ = true;
    DebugLog(L"[app] 调试输出已打开");
}

void App::HideTooltip() {
    tooltip_.Hide();
    tooltip_visible_ = false;
    last_token_.clear();
}

bool App::Probe(POINT pt) {
    last_probe_pt_ = pt;
    last_probe_tick_ = GetTickCount();

    CaptureResult cap = capture_.CaptureAt(pt);
    Translation tr;
    if (cap.valid()) {
        bool from_editor = cap.source == CaptureSource::Editor;
        tr = translator_.Translate(cap.token, cap.next_char, from_editor);
    }
    if (!cap.valid() || !tr.useful) {
        HideTooltip();
        probe_failed_ = true;
        ++retries_;
        return false;
    }

    probe_failed_ = false;
    retries_ = 0;
    if (tr.token == last_token_ && tooltip_visible_) return true;

    last_token_ = tr.token;
    token_bounds_ = cap.bounds;
    if (RectEmpty(token_bounds_)) {
        token_bounds_ = RECT{pt.x - 20, pt.y - 10, pt.x + 20, pt.y + 10};
    }
    token_bounds_ = InflateCopy(token_bounds_, 10, 8);
    tooltip_.Show(pt, tr);
    tooltip_visible_ = true;
    DebugLog(std::wstring(L"[app] ") + tr.kind_name + L" " + tr.token + L" => " + tr.summary);
    return true;
}

void App::OnTimer() {
    if (!enabled_) return;

    POINT pt{};
    GetCursorPos(&pt);

    if (GetAsyncKeyState(VK_LBUTTON) < 0 || GetAsyncKeyState(VK_RBUTTON) < 0 || GetAsyncKeyState(VK_MBUTTON) < 0) {
        HideTooltip();
        probe_dirty_ = true;
        return;
    }

    if (IsLikelyExclusiveFullscreen(pt)) {
        HideTooltip();
        return;
    }

    // 鼠标仍在当前显示的词上：不重复取词，也避免浮窗闪烁
    if (tooltip_visible_ && PtInRect(&token_bounds_, pt)) return;

    DWORD now = GetTickCount();
    bool moved = pt.x != last_probe_pt_.x || pt.y != last_probe_pt_.y;
    if (moved || probe_dirty_) {
        // 移动中即时取词（限流），松开鼠标后也立即补一次
        if (now - last_probe_tick_ < kProbeMinIntervalMs) return;
        retries_ = 0;
    } else {
        // 鼠标静止且上次取词无结果：有限重试。
        // Chromium/Electron（VS Code、Cursor 等）的无障碍树是收到查询后才懒构建的，
        // 多试几次通常就能取到词。
        if (!probe_failed_ || retries_ >= kMaxRetries) return;
        if (now - last_probe_tick_ < kRetryIntervalMs) return;
    }

    probe_dirty_ = false;
    Probe(pt);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_TIMER:
            if (wp == kTimerId) self->OnTimer();
            return 0;
        case WM_HOTKEY:
            if (wp == kHotkeyId) self->ToggleEnabled();
            return 0;
        case TrayIcon::kCallbackMsg:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
                POINT p{};
                GetCursorPos(&p);
                SetForegroundWindow(hwnd);
                TrackPopupMenu(self->tray_.Menu(), TPM_RIGHTBUTTON, p.x, p.y, 0, hwnd, nullptr);
                PostMessageW(hwnd, WM_NULL, 0, 0);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case TrayIcon::kCmdToggle: self->ToggleEnabled(); break;
                case TrayIcon::kCmdDebug: self->ToggleDebug(); break;
                case TrayIcon::kCmdExit: DestroyWindow(hwnd); break;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

}  // namespace ctm
