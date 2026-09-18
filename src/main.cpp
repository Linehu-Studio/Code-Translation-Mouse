#include "App.h"

using FnDpi = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);

static void EnableDpiAware() {
    auto set_ctx = reinterpret_cast<FnDpi>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (set_ctx) {
        set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        return;
    }
    SetProcessDPIAware();
}

static bool HasFlag(LPWSTR cmd, const wchar_t* flag) {
    if (!cmd || !flag) return false;
    std::wstring s = cmd;
    return s.find(flag) != std::wstring::npos;
}

static int RunSelfTest() {
    ctm::EnableDebugConsole();
    ctm::Translator translator;
    translator.Load();

    struct Case {
        const wchar_t* token;
        const wchar_t* expect;
        wchar_t next;
    };
    const Case cases[] = {
        {L"return", L"返回", 0},
        {L"getUserName", L"获取", 0},
        {L"unique_ptr", L"独占", 0},
        {L"isValid", L"有效", 0},
        {L"std::vector", L"向量", 0},
        {L"createWindow", L"创建", L'('},
    };

    int fail = 0;
    for (const auto& c : cases) {
        ctm::Translation tr = translator.Translate(c.token, c.next, true);
        bool ok = tr.useful && tr.summary.find(c.expect) != std::wstring::npos;
        ctm::DebugLog((ok ? L"[OK]   " : L"[FAIL] ") + std::wstring(c.token) + L" => " + tr.summary + L"  (" +
                      tr.kind_name + L")");
        if (!ok) ++fail;
    }
    ctm::DebugLog(fail == 0 ? L"self-test passed" : L"self-test failed");
    return fail;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR cmd, int) {
    EnableDpiAware();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    if (HasFlag(cmd, L"--self-test")) {
        int code = RunSelfTest();
        CoUninitialize();
        return code;
    }

    bool debug = HasFlag(cmd, L"--debug");
    int code = ctm::App::Instance().Run(instance, debug);

    CoUninitialize();
    return code;
}
