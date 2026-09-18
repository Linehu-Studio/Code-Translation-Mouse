#pragma once

#include "TextCapture.h"
#include "Translate.h"
#include "Ui.h"

namespace ctm {

class App {
public:
    static App& Instance();
    int Run(HINSTANCE instance, bool debug);

    bool Enabled() const { return enabled_; }

private:
    App() = default;

    bool Init(HINSTANCE instance, bool debug);
    void Shutdown();
    void OnTimer();
    bool Probe(POINT pt);
    void HideTooltip();
    void ToggleEnabled();
    void ToggleDebug();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HANDLE mutex_ = nullptr;
    BOOL screen_reader_was_ = FALSE;
    bool restored_screen_reader_ = false;

    TextCapture capture_;
    Translator translator_;
    TooltipWindow tooltip_;
    TrayIcon tray_;

    bool enabled_ = true;
    bool debug_ = false;

    POINT last_probe_pt_{-30000, -30000};  // 上次取词位置
    DWORD last_probe_tick_ = 0;
    bool probe_failed_ = false;  // 上次取词无结果（用于静止时的懒加载重试）
    bool probe_dirty_ = false;   // 需要强制重新取词（如松开鼠标后）
    int retries_ = 0;
    bool tooltip_visible_ = false;
    RECT token_bounds_{};
    std::wstring last_token_;

    static constexpr UINT kTimerId = 1;
    static constexpr UINT kHotkeyId = 1;
    static constexpr DWORD kProbeMinIntervalMs = 50;  // 移动时两次取词的最小间隔
    static constexpr DWORD kRetryIntervalMs = 150;    // 静止取词失败后的重试间隔
    static constexpr int kMaxRetries = 4;
};

}  // namespace ctm
