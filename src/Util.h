#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>

namespace ctm {

inline void DebugLog(const std::wstring& line) {
    // 不能缓存"是否有控制台"：控制台可能在运行中途才打开（托盘菜单），每次现查。
    if (GetConsoleWindow() == nullptr) return;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!out || out == INVALID_HANDLE_VALUE) return;
    std::wstring msg = line + L"\r\n";
    DWORD written = 0;
    WriteConsoleW(out, msg.c_str(), static_cast<DWORD>(msg.size()), &written, nullptr);
}

inline void EnableDebugConsole() {
    if (GetConsoleWindow()) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
        AllocConsole();
        SetConsoleTitleW(L"代码悬停翻译 - 调试");
    }
    SetConsoleOutputCP(CP_UTF8);
    // AttachConsole 不会自动设置标准句柄，显式绑定到 CONOUT$，
    // 否则从终端启动时 WriteConsoleW 可能写不出去。
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (out != INVALID_HANDLE_VALUE) {
        SetStdHandle(STD_OUTPUT_HANDLE, out);
    }
}

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* p) : p_(p) {}
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : p_(other.p_) { other.p_ = nullptr; }
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            reset();
            p_ = other.p_;
            other.p_ = nullptr;
        }
        return *this;
    }

    void reset() {
        if (p_) {
            p_->Release();
            p_ = nullptr;
        }
    }

    T* get() const { return p_; }
    T** put() {
        reset();
        return &p_;
    }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T* detach() {
        T* tmp = p_;
        p_ = nullptr;
        return tmp;
    }

private:
    T* p_ = nullptr;
};

class ComInit {
public:
    ComInit() { hr_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComInit() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    HRESULT hr() const { return hr_; }

private:
    HRESULT hr_ = E_FAIL;
};

inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr,
                                nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring ToLower(std::wstring_view s) {
    std::wstring out(s);
    for (wchar_t& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

inline std::wstring Trim(std::wstring s) {
    auto is_space = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x00A0 || c == 0x200B;
    };
    while (!s.empty() && is_space(s.front())) s.erase(s.begin());
    while (!s.empty() && is_space(s.back())) s.pop_back();
    return s;
}

inline bool IsIdentChar(wchar_t c) {
    return IsCharAlphaNumericW(c) || c == L'_' || c == L'$' || c == L'@' || c == L'#' || c == L':';
}

inline bool IsAsciiLetter(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

inline bool HasCjk(std::wstring_view s) {
    for (wchar_t c : s) {
        if (c >= 0x4E00 && c <= 0x9FFF) return true;
    }
    return false;
}

inline bool LooksLikeCamelOrSnake(std::wstring_view s) {
    bool has_lower = false;
    bool has_upper = false;
    bool has_under = false;
    for (wchar_t c : s) {
        if (c >= L'a' && c <= L'z') has_lower = true;
        else if (c >= L'A' && c <= L'Z') has_upper = true;
        else if (c == L'_' || c == L'$') has_under = true;
    }
    return has_under || (has_lower && has_upper);
}

inline RECT InflateCopy(RECT r, int dx, int dy) {
    r.left -= dx;
    r.right += dx;
    r.top -= dy;
    r.bottom += dy;
    return r;
}

inline bool RectEmpty(const RECT& r) {
    return r.right <= r.left || r.bottom <= r.top;
}

inline std::wstring ExeDir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    size_t pos = s.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return s.substr(0, pos);
}

inline std::wstring AppDataDir() {
    wchar_t path[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return ExeDir();
    std::wstring dir = std::wstring(path) + L"\\CodeTranslationMouse";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

}  // namespace ctm
