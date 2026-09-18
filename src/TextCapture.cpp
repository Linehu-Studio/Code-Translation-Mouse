#include "TextCapture.h"

#include <oleacc.h>
#include <oleauto.h>
#include <uiautomation.h>

#include <cwchar>

namespace ctm {
namespace {

constexpr UINT kSciGetCharAt = 2006;
constexpr UINT kSciPositionFromPointClose = 2023;
constexpr UINT kSciPointXFromPosition = 2164;
constexpr UINT kSciPointYFromPosition = 2165;
constexpr UINT kSciTextHeight = 2279;
constexpr UINT kSciPositionBefore = 2417;
constexpr UINT kSciPositionAfter = 2418;

LRESULT SendTimeout(HWND hwnd, UINT msg, WPARAM w, LPARAM l, UINT timeout_ms = 80) {
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(hwnd, msg, w, l, SMTO_ABORTIFHUNG, timeout_ms, &result)) {
        return 0;
    }
    return static_cast<LRESULT>(result);
}

bool ClassContains(HWND hwnd, const wchar_t* needle) {
    wchar_t cls[256]{};
    if (!GetClassNameW(hwnd, cls, 256)) return false;
    std::wstring lower = ToLower(cls);
    std::wstring n = ToLower(needle);
    return lower.find(n) != std::wstring::npos;
}

std::wstring ExtractIdent(const std::wstring& text, int pos, int& start, int& end) {
    if (pos < 0 || pos >= static_cast<int>(text.size())) return {};
    start = pos;
    end = pos;
    if (!IsIdentChar(text[pos])) return {};
    while (start > 0 && IsIdentChar(text[start - 1])) --start;
    while (end < static_cast<int>(text.size()) && IsIdentChar(text[end])) ++end;
    return text.substr(start, end - start);
}

void UnionScreenRect(RECT& acc, const RECT& add, bool& has) {
    if (!has) {
        acc = add;
        has = true;
    } else {
        UnionRect(&acc, &acc, &add);
    }
}

// 找出文本中最长的标识符连续段，用于在整词文本里定位悬停的标识符。
bool FindIdentifierRun(const std::wstring& text, int& start, int& end) {
    int best_s = -1;
    int best_len = 0;
    int n = static_cast<int>(text.size());
    int i = 0;
    while (i < n) {
        if (!IsIdentChar(text[i])) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && IsIdentChar(text[j])) ++j;
        if (j - i > best_len) {
            best_s = i;
            best_len = j - i;
        }
        i = j;
    }
    if (best_s < 0 || best_len < 1 || best_len > 64) return false;
    start = best_s;
    end = best_s + best_len;
    return true;
}

}  // namespace

TextCapture::TextCapture() = default;

TextCapture::~TextCapture() {
    if (automation_) {
        static_cast<IUIAutomation*>(automation_)->Release();
        automation_ = nullptr;
    }
}

bool TextCapture::Init() {
    IUIAutomation* automation = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                  reinterpret_cast<void**>(&automation));
    if (FAILED(hr) || !automation) {
        DebugLog(L"[capture] UI Automation 不可用，将仅使用 Scintilla/编辑框取词");
        return false;
    }
    automation_ = automation;
    return true;
}

CaptureResult TextCapture::CaptureAt(POINT screen_pt) {
    CaptureResult out;
    HWND hwnd = WindowFromPoint(screen_pt);
    if (!hwnd) return out;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return out;

    wchar_t cls[256]{};
    GetClassNameW(hwnd, cls, 256);
    DebugLog(std::wstring(L"[capture] hwnd class=") + cls);

    if (FromScintilla(hwnd, screen_pt, out)) return out;

    HWND parent = GetParent(hwnd);
    if (parent && FromScintilla(parent, screen_pt, out)) return out;

    if (FromEdit(hwnd, screen_pt, out)) return out;
    if (FromUia(screen_pt, out)) return out;
    if (FromMsaa(screen_pt, out)) return out;
    return out;
}

bool TextCapture::FromScintilla(HWND hwnd, POINT screen_pt, CaptureResult& out) {
    if (!ClassContains(hwnd, L"scintilla")) return false;

    POINT client = screen_pt;
    if (!ScreenToClient(hwnd, &client)) return false;

    LRESULT pos = SendTimeout(hwnd, kSciPositionFromPointClose, static_cast<WPARAM>(client.x),
                              static_cast<LPARAM>(client.y));
    if (pos < 0) return false;

    auto char_at = [&](LRESULT p) -> unsigned char {
        return static_cast<unsigned char>(SendTimeout(hwnd, kSciGetCharAt, static_cast<WPARAM>(p), 0));
    };
    auto is_word = [&](LRESULT p) -> bool {
        unsigned char b = char_at(p);
        if (b == 0) return false;
        if (b < 0x80) return IsIdentChar(static_cast<wchar_t>(b));
        return true;
    };

    if (!is_word(pos)) return false;

    LRESULT start = pos;
    LRESULT end = SendTimeout(hwnd, kSciPositionAfter, static_cast<WPARAM>(pos), 0);
    if (end <= start) end = start + 1;

    for (int i = 0; i < 96; ++i) {
        LRESULT prev = SendTimeout(hwnd, kSciPositionBefore, static_cast<WPARAM>(start), 0);
        if (prev < 0 || prev >= start) break;
        if (!is_word(prev)) break;
        start = prev;
    }
    for (int i = 0; i < 96; ++i) {
        if (!is_word(end)) break;
        LRESULT next = SendTimeout(hwnd, kSciPositionAfter, static_cast<WPARAM>(end), 0);
        if (next <= end) break;
        end = next;
    }

    std::string bytes;
    bytes.reserve(static_cast<size_t>(end - start));
    for (LRESULT p = start; p < end; ++p) {
        bytes.push_back(static_cast<char>(char_at(p)));
    }
    out.token = Trim(Utf8ToWide(bytes));
    if (out.token.empty()) return false;

    unsigned char nb = char_at(end);
    if (nb > 0 && nb < 0x80) out.next_char = static_cast<wchar_t>(nb);

    int x1 = static_cast<int>(SendTimeout(hwnd, kSciPointXFromPosition, 0, start));
    int y1 = static_cast<int>(SendTimeout(hwnd, kSciPointYFromPosition, 0, start));
    int x2 = static_cast<int>(SendTimeout(hwnd, kSciPointXFromPosition, 0, end));
    int h = static_cast<int>(SendTimeout(hwnd, kSciTextHeight, 0, 0));
    if (h <= 0) h = 16;
    POINT p1{x1, y1};
    POINT p2{x2, y1 + h};
    ClientToScreen(hwnd, &p1);
    ClientToScreen(hwnd, &p2);
    out.bounds = RECT{p1.x, p1.y, p2.x, p2.y};
    out.source = CaptureSource::Editor;
    DebugLog(std::wstring(L"[capture] scintilla token=") + out.token);
    return true;
}

bool TextCapture::FromEdit(HWND hwnd, POINT screen_pt, CaptureResult& out) {
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    std::wstring lower = ToLower(cls);
    if (lower != L"edit" && lower.find(L"richedit") == std::wstring::npos) return false;
    if (lower.find(L"richedit") != std::wstring::npos) return false;  // 跨进程结构体指针不可用

    POINT client = screen_pt;
    ScreenToClient(hwnd, &client);
    DWORD_PTR idx = 0;
    if (!SendMessageTimeoutW(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(client.x, client.y), SMTO_ABORTIFHUNG, 80, &idx)) {
        return false;
    }
    int pos = static_cast<int>(LOWORD(idx));

    LRESULT len = SendTimeout(hwnd, WM_GETTEXTLENGTH, 0, 0, 120);
    if (len <= 0 || len > 2'000'000) return false;

    std::wstring text(static_cast<size_t>(len) + 1, L'\0');
    DWORD_PTR copied = 0;
    if (!SendMessageTimeoutW(hwnd, WM_GETTEXT, static_cast<WPARAM>(len + 1),
                             reinterpret_cast<LPARAM>(text.data()), SMTO_ABORTIFHUNG, 120, &copied)) {
        return false;
    }
    text.resize(static_cast<size_t>(len));

    int start = 0, end = 0;
    out.token = Trim(ExtractIdent(text, pos, start, end));
    if (out.token.empty()) return false;
    if (end < static_cast<int>(text.size())) out.next_char = text[end];

    DWORD_PTR packed = 0;
    SendMessageTimeoutW(hwnd, EM_POSFROMCHAR, static_cast<WPARAM>(start), 0, SMTO_ABORTIFHUNG, 80, &packed);
    POINT p1{static_cast<SHORT>(LOWORD(packed)), static_cast<SHORT>(HIWORD(packed))};
    SendMessageTimeoutW(hwnd, EM_POSFROMCHAR, static_cast<WPARAM>(end), 0, SMTO_ABORTIFHUNG, 80, &packed);
    POINT p2{static_cast<SHORT>(LOWORD(packed)), static_cast<SHORT>(HIWORD(packed))};
    ClientToScreen(hwnd, &p1);
    ClientToScreen(hwnd, &p2);
    TEXTMETRICW tm{};
    HDC hdc = GetDC(hwnd);
    if (hdc) {
        GetTextMetricsW(hdc, &tm);
        ReleaseDC(hwnd, hdc);
    }
    int h = tm.tmHeight > 0 ? tm.tmHeight : 16;
    out.bounds = RECT{p1.x, p1.y, p2.x + 2, p1.y + h};
    out.source = CaptureSource::Editor;
    DebugLog(std::wstring(L"[capture] edit token=") + out.token);
    return true;
}

bool TextCapture::ExpandIdentifierRange(void* text_range, CaptureResult& out) {
    auto* origin = static_cast<IUIAutomationTextRange*>(text_range);
    if (!origin) return false;

    auto get_text = [](IUIAutomationTextRange* r, int max_chars = 128) -> std::wstring {
        BSTR b = nullptr;
        r->GetText(max_chars, &b);
        std::wstring s = b ? b : L"";
        if (b) SysFreeString(b);
        return s;
    };

    auto bounds_of = [](IUIAutomationTextRange* r, RECT& bounds) {
        SAFEARRAY* sa = nullptr;
        if (FAILED(r->GetBoundingRectangles(&sa)) || !sa) return;
        DOUBLE* data = nullptr;
        if (SUCCEEDED(SafeArrayAccessData(sa, reinterpret_cast<void**>(&data)))) {
            LONG lb = 0, ub = -1;
            SafeArrayGetLBound(sa, 1, &lb);
            SafeArrayGetUBound(sa, 1, &ub);
            int n = static_cast<int>(ub - lb + 1);
            bool has = false;
            for (int i = 0; i + 3 < n; i += 4) {
                RECT rc{static_cast<LONG>(data[i]), static_cast<LONG>(data[i + 1]),
                        static_cast<LONG>(data[i] + data[i + 2]), static_cast<LONG>(data[i + 1] + data[i + 3])};
                UnionScreenRect(bounds, rc, has);
            }
            SafeArrayUnaccessData(sa);
        }
        SafeArrayDestroy(sa);
    };

    // 快速路径：把区间扩展到整个词后一次取回全部文本，再在本地定位标识符。
    // 逐字符扩展在 Electron 等远程提供程序上每步都要跨进程调用，一次悬停可达数百次调用。
    ComPtr<IUIAutomationTextRange> word;
    if (SUCCEEDED(origin->Clone(reinterpret_cast<IUIAutomationTextRange**>(word.put()))) && word &&
        SUCCEEDED(word->ExpandToEnclosingUnit(TextUnit_Word))) {
        std::wstring wtext = get_text(word.get(), 256);
        int rs = 0, re = 0;
        if (wtext.size() < 255 && FindIdentifierRun(wtext, rs, re)) {
            int wlen = static_cast<int>(wtext.size());
            if (rs > 0) {
                int moved = 0;
                word->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, rs, &moved);
            }
            if (re < wlen) {
                int moved = 0;
                word->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, re - wlen, &moved);
            }
            // 词边界可能把 "std::vector" 这类 token 切开，按标识符字符向两侧少量扩展补回。
            for (int side = 0; side < 2; ++side) {
                for (int i = 0; i < 6; ++i) {
                    ComPtr<IUIAutomationTextRange> test;
                    if (FAILED(word->Clone(reinterpret_cast<IUIAutomationTextRange**>(test.put()))) || !test) break;
                    int moved = 0;
                    HRESULT hr = side == 0
                                     ? test->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1,
                                                                &moved)
                                     : test->MoveEndpointByUnit(TextPatternRangeEndpoint_Start,
                                                                TextUnit_Character, -1, &moved);
                    if (FAILED(hr) || moved == 0) break;
                    std::wstring t = get_text(test.get(), 256);
                    if (t.empty() || t.size() > 64) break;
                    wchar_t edge = side == 0 ? t.back() : t.front();
                    if (!IsIdentChar(edge)) break;
                    if (side == 0) {
                        word->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved);
                    } else {
                        word->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved);
                    }
                }
            }
            std::wstring token = Trim(get_text(word.get(), 256));
            if (!token.empty() && token.size() <= 64) {
                out.token = token;
                ComPtr<IUIAutomationTextRange> after;
                if (SUCCEEDED(word->Clone(reinterpret_cast<IUIAutomationTextRange**>(after.put()))) && after) {
                    int moved = 0;
                    if (SUCCEEDED(after->MoveEndpointByRange(TextPatternRangeEndpoint_Start, word.get(),
                                                             TextPatternRangeEndpoint_End)) &&
                        SUCCEEDED(after->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1,
                                                            &moved)) &&
                        moved > 0) {
                        std::wstring n = get_text(after.get(), 4);
                        if (!n.empty()) out.next_char = n.front();
                    }
                }
                bounds_of(word.get(), out.bounds);
                return true;
            }
        }
    }

    // 慢速回退：Word 单元不可用时按字符逐个扩展。
    ComPtr<IUIAutomationTextRange> range;
    if (FAILED(origin->Clone(reinterpret_cast<IUIAutomationTextRange**>(range.put()))) || !range) return false;
    if (FAILED(range->ExpandToEnclosingUnit(TextUnit_Character))) return false;

    std::wstring first = get_text(range.get(), 4);
    if (first.empty() || !IsIdentChar(first.front())) return false;

    for (int i = 0; i < 24; ++i) {
        ComPtr<IUIAutomationTextRange> test;
        if (FAILED(range->Clone(reinterpret_cast<IUIAutomationTextRange**>(test.put()))) || !test) break;
        int moved = 0;
        if (FAILED(test->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved)) ||
            moved == 0) {
            break;
        }
        std::wstring t = get_text(test.get());
        if (t.empty() || !IsIdentChar(t.front())) break;
        int moved2 = 0;
        range->MoveEndpointByUnit(TextPatternRangeEndpoint_Start, TextUnit_Character, -1, &moved2);
    }

    for (int i = 0; i < 24; ++i) {
        ComPtr<IUIAutomationTextRange> test;
        if (FAILED(range->Clone(reinterpret_cast<IUIAutomationTextRange**>(test.put()))) || !test) break;
        int moved = 0;
        if (FAILED(test->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved)) ||
            moved == 0) {
            break;
        }
        std::wstring t = get_text(test.get());
        if (t.empty() || !IsIdentChar(t.back())) break;
        int moved2 = 0;
        range->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved2);
    }

    out.token = Trim(get_text(range.get()));
    if (out.token.empty()) return false;

    ComPtr<IUIAutomationTextRange> after;
    if (SUCCEEDED(range->Clone(reinterpret_cast<IUIAutomationTextRange**>(after.put()))) && after) {
        int moved = 0;
        if (SUCCEEDED(after->MoveEndpointByRange(TextPatternRangeEndpoint_Start, range.get(),
                                                 TextPatternRangeEndpoint_End)) &&
            SUCCEEDED(after->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, 1, &moved)) &&
            moved > 0) {
            std::wstring n = get_text(after.get(), 4);
            if (!n.empty()) out.next_char = n.front();
        }
    }

    bounds_of(range.get(), out.bounds);
    return true;
}

bool TextCapture::TryTextPattern(IUnknown* element_unk, POINT screen_pt, CaptureResult& out) {
    auto* el = static_cast<IUIAutomationElement*>(element_unk);
    if (!el) return false;

    IUnknown* pattern = nullptr;
    HRESULT hr = el->GetCurrentPattern(UIA_TextPatternId, &pattern);
    if (FAILED(hr) || !pattern) return false;
    ComPtr<IUnknown> pattern_ptr(pattern);

    IUIAutomationTextPattern* text = nullptr;
    hr = pattern->QueryInterface(IID_IUIAutomationTextPattern, reinterpret_cast<void**>(&text));
    if (FAILED(hr) || !text) return false;
    ComPtr<IUIAutomationTextPattern> text_ptr(text);

    IUIAutomationTextRange* range = nullptr;
    hr = text->RangeFromPoint(screen_pt, &range);
    if (FAILED(hr) || !range) return false;
    ComPtr<IUIAutomationTextRange> range_ptr(range);

    if (!ExpandIdentifierRange(range, out)) return false;
    out.source = CaptureSource::Editor;
    DebugLog(std::wstring(L"[capture] uia text token=") + out.token);
    return true;
}

bool TextCapture::FromUia(POINT screen_pt, CaptureResult& out) {
    auto* automation = static_cast<IUIAutomation*>(automation_);
    if (!automation) return false;

    IUIAutomationElement* raw = nullptr;
    HRESULT hr = automation->ElementFromPoint(screen_pt, &raw);
    if (FAILED(hr) || !raw) return false;
    ComPtr<IUIAutomationElement> el(raw);

    if (TryTextPattern(el.get(), screen_pt, out)) return true;

    IUIAutomationTreeWalker* walker_raw = nullptr;
    if (SUCCEEDED(automation->get_ControlViewWalker(&walker_raw)) && walker_raw) {
        ComPtr<IUIAutomationTreeWalker> walker(walker_raw);
        IUIAutomationElement* current = nullptr;
        el.get()->AddRef();
        current = el.get();
        for (int i = 0; i < 10 && current; ++i) {
            IUIAutomationElement* parent = nullptr;
            walker->GetParentElement(current, &parent);
            current->Release();
            current = parent;
            if (!current) break;
            if (TryTextPattern(current, screen_pt, out)) {
                current->Release();
                return true;
            }
        }
        if (current) current->Release();
    }

    CONTROLTYPEID ctype = 0;
    el->get_CurrentControlType(&ctype);

    // name 兜底：只在元素本身就是编辑/文本控件、且整个 Name 恰好是一个标识符时才采用。
    // 否则会把整行代码、侧边栏文件名之类的元素文本里的某个词错当成悬停处的单词。
    if (ctype != UIA_DocumentControlTypeId && ctype != UIA_EditControlTypeId &&
        ctype != UIA_TextControlTypeId) {
        return false;
    }

    BSTR name = nullptr;
    el->get_CurrentName(&name);
    std::wstring token = Trim(name ? name : L"");
    if (name) SysFreeString(name);
    if (token.empty() || token.size() > 64) return false;

    int start = 0, end = 0;
    std::wstring ident = ExtractIdent(token, 0, start, end);
    if (ident.empty() || ident != token) return false;

    out.token = ident;
    out.source = CaptureSource::Editor;
    RECT r{};
    if (SUCCEEDED(el->get_CurrentBoundingRectangle(&r))) out.bounds = r;
    DebugLog(std::wstring(L"[capture] uia name token=") + out.token);
    return true;
}

bool TextCapture::FromMsaa(POINT screen_pt, CaptureResult& out) {
    IAccessible* acc = nullptr;
    VARIANT child;
    VariantInit(&child);
    HRESULT hr = AccessibleObjectFromPoint(screen_pt, &acc, &child);
    if (FAILED(hr) || !acc) {
        VariantClear(&child);
        return false;
    }
    ComPtr<IAccessible> acc_ptr(acc);

    // 只信文本类控件的 Name，且要求整个 Name 就是一个标识符，
    // 否则容易把界面上其他元素的文字当成悬停处的单词。
    VARIANT role;
    VariantInit(&role);
    acc->get_accRole(child, &role);
    bool text_like = role.vt == VT_I4 && (role.lVal == ROLE_SYSTEM_TEXT || role.lVal == ROLE_SYSTEM_DOCUMENT);

    BSTR name = nullptr;
    if (text_like) acc->get_accName(child, &name);
    VariantClear(&child);
    VariantClear(&role);
    if (!text_like) return false;

    std::wstring token = Trim(name ? name : L"");
    if (name) SysFreeString(name);
    if (token.empty() || token.size() > 64) return false;

    int start = 0, end = 0;
    std::wstring ident = ExtractIdent(token, 0, start, end);
    if (ident.empty() || ident != token) return false;

    out.token = ident;
    out.source = CaptureSource::Editor;
    DebugLog(std::wstring(L"[capture] msaa token=") + out.token);
    return true;
}

}  // namespace ctm
