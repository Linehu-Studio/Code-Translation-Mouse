#pragma once

#include "Util.h"
#include <unknwn.h>

namespace ctm {

enum class CaptureSource {
    None,
    Editor,  // Scintilla / UIA TextPattern / EDIT
    Generic
};

struct CaptureResult {
    std::wstring token;
    wchar_t next_char = 0;
    RECT bounds{};
    CaptureSource source = CaptureSource::None;

    bool valid() const { return !token.empty(); }
};

class TextCapture {
public:
    TextCapture();
    ~TextCapture();

    bool Init();
    CaptureResult CaptureAt(POINT screen_pt);

private:
    bool FromScintilla(HWND hwnd, POINT screen_pt, CaptureResult& out);
    bool FromEdit(HWND hwnd, POINT screen_pt, CaptureResult& out);
    bool FromUia(POINT screen_pt, CaptureResult& out);
    bool FromMsaa(POINT screen_pt, CaptureResult& out);

    bool TryTextPattern(IUnknown* element_unk, POINT screen_pt, CaptureResult& out);
    bool ExpandIdentifierRange(void* text_range, CaptureResult& out);

    void* automation_ = nullptr;  // IUIAutomation*
};

}  // namespace ctm
