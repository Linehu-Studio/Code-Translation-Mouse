#include "Translate.h"

#include <cwctype>
#include <sstream>

namespace ctm {
namespace {

bool AllUpperAscii(std::wstring_view s) {
    bool letter = false;
    for (wchar_t c : s) {
        if (c >= L'a' && c <= L'z') return false;
        if (c >= L'A' && c <= L'Z') letter = true;
    }
    return letter;
}

std::wstring JoinParts(const std::vector<std::wstring>& parts, const std::vector<std::wstring>& zh) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += L" · ";
        if (i < zh.size() && !zh[i].empty()) out += zh[i];
        else out += parts[i];
    }
    return out;
}

}  // namespace

static constexpr char kFallbackDict[] = R"UTF8(
if|如果|keyword
else|否则|keyword
for|循环|keyword
while|当...时循环|keyword
return|返回|keyword
class|类|keyword
struct|结构体|keyword
void|空类型|keyword
int|整数|type
string|字符串|type
bool|布尔|type
true|真|keyword
false|假|keyword
null|空|keyword
nullptr|空指针|keyword
this|当前对象|keyword
new|新建|keyword
delete|删除|keyword
public|公有|keyword
private|私有|keyword
protected|保护|keyword
static|静态|keyword
const|常量|keyword
virtual|虚函数|keyword
override|重写|keyword
get|获取|word
set|设置|word
user|用户|word
name|名称|word
count|计数|word
index|索引|word
list|列表|word
data|数据|word
file|文件|word
path|路径|word
error|错误|word
value|值|word
)UTF8";

bool Translator::Load() {
    keywords_.clear();
    types_.clear();
    words_.clear();
    ParseDictionary(kFallbackDict, false);
    LoadFromResource();
    LoadFromFile(ExeDir() + L"\\dictionary.txt", true);
    LoadFromFile(AppDataDir() + L"\\dictionary.txt", true);
    DebugLog(L"[dict] keywords=" + std::to_wstring(keywords_.size()) + L" types=" +
             std::to_wstring(types_.size()) + L" words=" + std::to_wstring(words_.size()));
    return !keywords_.empty() || !words_.empty();
}

bool Translator::LoadFromResource() {
    HMODULE mod = GetModuleHandleW(nullptr);
    HRSRC hrsrc = FindResourceW(mod, L"DICTIONARY", RT_RCDATA);
    if (!hrsrc) {
        DebugLog(L"[dict] 内置词典资源未找到");
        return false;
    }
    HGLOBAL hglob = LoadResource(mod, hrsrc);
    if (!hglob) return false;
    const char* data = static_cast<const char*>(LockResource(hglob));
    DWORD size = SizeofResource(mod, hrsrc);
    if (!data || size == 0) return false;
    ParseDictionary(std::string_view(data, size), false);
    return true;
}

bool Translator::LoadFromFile(const std::wstring& path, bool override_existing) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(file, &li) || li.QuadPart <= 0 || li.QuadPart > 8'000'000) {
        CloseHandle(file);
        return false;
    }
    std::string buf(static_cast<size_t>(li.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = ReadFile(file, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return false;
    buf.resize(read);
    ParseDictionary(buf, override_existing);
    DebugLog(std::wstring(L"[dict] 已加载 ") + path);
    return true;
}

void Translator::ParseDictionary(std::string_view utf8, bool override_existing) {
    std::string text(utf8);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    std::wstring wide = Utf8ToWide(text);
    std::wistringstream in(wide);
    std::wstring line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        line = Trim(line);
        if (line.empty() || line[0] == L'#') continue;
        size_t p1 = line.find(L'|');
        if (p1 == std::wstring::npos) continue;
        size_t p2 = line.find(L'|', p1 + 1);
        std::wstring en = Trim(line.substr(0, p1));
        std::wstring zh;
        std::wstring kind = L"word";
        if (p2 == std::wstring::npos) {
            zh = Trim(line.substr(p1 + 1));
        } else {
            zh = Trim(line.substr(p1 + 1, p2 - p1 - 1));
            kind = ToLower(Trim(line.substr(p2 + 1)));
        }
        if (en.empty() || zh.empty()) continue;
        std::wstring key = ToLower(en);
        auto put = [&](std::unordered_map<std::wstring, std::wstring>& map) {
            if (override_existing || !map.contains(key)) map[key] = zh;
        };
        if (kind == L"keyword" || kind == L"kw") put(keywords_);
        else if (kind == L"type") put(types_);
        else put(words_);
    }
}

std::wstring Translator::LookupWord(std::wstring_view part) const {
    std::wstring key = ToLower(part);
    if (auto it = words_.find(key); it != words_.end()) return it->second;
    if (auto it = types_.find(key); it != types_.end()) return it->second;
    if (auto it = keywords_.find(key); it != keywords_.end()) return it->second;
    return {};
}

std::wstring Translator::StripCommonPrefix(std::wstring token) {
    static const wchar_t* prefixes[] = {L"m_", L"s_", L"g_", L"k_", L"ms_", L"lpsz", L"psz"};
    std::wstring lower = ToLower(token);
    for (const wchar_t* p : prefixes) {
        size_t n = wcslen(p);
        if (lower.size() > n + 1 && lower.compare(0, n, p) == 0) {
            token.erase(0, n);
            break;
        }
    }
    while (!token.empty() && token.front() == L'_') token.erase(token.begin());
    return token;
}

std::vector<std::wstring> Translator::SplitIdentifier(std::wstring token) {
    token = Trim(token);
    std::vector<std::wstring> chunks;
    std::wstring cur;
    for (wchar_t c : token) {
        if (c == L'.' || c == L'/' || c == L'\\' || c == L':') {
            if (!cur.empty()) {
                chunks.push_back(cur);
                cur.clear();
            }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) chunks.push_back(cur);

    std::vector<std::wstring> parts;
    for (auto chunk : chunks) {
        chunk = StripCommonPrefix(chunk);
        if (chunk.empty()) continue;
        std::wstring buf;
        for (size_t i = 0; i < chunk.size(); ++i) {
            wchar_t c = chunk[i];
            if (c == L'_' || c == L'-') {
                if (!buf.empty()) {
                    parts.push_back(buf);
                    buf.clear();
                }
                continue;
            }
            if (!buf.empty()) {
                wchar_t prev = buf.back();
                bool split = false;
                if (IsCharAlphaW(prev) && iswdigit(c)) split = true;
                else if (iswdigit(prev) && IsCharAlphaW(c)) split = true;
                else if (prev >= L'a' && prev <= L'z' && c >= L'A' && c <= L'Z') split = true;
                else if (prev >= L'A' && prev <= L'Z' && c >= L'A' && c <= L'Z') {
                    if (i + 1 < chunk.size() && chunk[i + 1] >= L'a' && chunk[i + 1] <= L'z') split = true;
                }
                if (split) {
                    parts.push_back(buf);
                    buf.clear();
                }
            }
            buf.push_back(c);
        }
        if (!buf.empty()) parts.push_back(buf);
    }
    return parts;
}

std::wstring Translator::KindName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Keyword: return L"关键字";
        case TokenKind::Type: return L"类型";
        case TokenKind::Function: return L"函数";
        case TokenKind::Variable: return L"变量名";
        case TokenKind::Constant: return L"常量";
        case TokenKind::Preprocessor: return L"预处理";
        default: return L"标识符";
    }
}

TokenKind Translator::Classify(const std::wstring& token, const std::wstring& lower, wchar_t next_char) const {
    if (!token.empty() && token.front() == L'#') return TokenKind::Preprocessor;
    if (keywords_.contains(lower)) return TokenKind::Keyword;
    if (types_.contains(lower)) return TokenKind::Type;
    if (next_char == L'(') return TokenKind::Function;
    if (AllUpperAscii(token) && token.size() > 1) return TokenKind::Constant;
    if (!token.empty() && token.front() >= L'A' && token.front() <= L'Z' && LooksLikeCamelOrSnake(token)) {
        return TokenKind::Type;
    }
    return TokenKind::Variable;
}

Translation Translator::Translate(const std::wstring& raw, wchar_t next_char, bool from_editor) const {
    Translation tr;
    std::wstring token = Trim(raw);
    while (token.size() >= 2) {
        wchar_t a = token.front();
        wchar_t b = token.back();
        if ((a == L'"' && b == L'"') || (a == L'\'' && b == L'\'') || (a == L'`' && b == L'`')) {
            token = Trim(token.substr(1, token.size() - 2));
        } else {
            break;
        }
    }
    tr.token = token;
    if (token.empty() || HasCjk(token) || token.size() > 96) return tr;

    bool only_punct = true;
    for (wchar_t c : token) {
        if (IsIdentChar(c)) {
            only_punct = false;
            break;
        }
    }
    if (only_punct) return tr;

    std::wstring lower = ToLower(token);
    tr.kind = Classify(token, lower, next_char);
    tr.kind_name = KindName(tr.kind);

    if (auto it = keywords_.find(lower); it != keywords_.end() && tr.parts.empty()) {
        tr.parts = {token};
        tr.part_zh = {it->second};
        tr.summary = it->second;
        tr.useful = true;
        tr.kind = TokenKind::Keyword;
        tr.kind_name = KindName(tr.kind);
        return tr;
    }
    if (auto it = types_.find(lower); it != types_.end()) {
        tr.parts = {token};
        tr.part_zh = {it->second};
        tr.summary = it->second;
        tr.useful = true;
        tr.kind = TokenKind::Type;
        tr.kind_name = KindName(tr.kind);
        if (next_char == L'(') {
            tr.kind = TokenKind::Function;
            tr.kind_name = KindName(tr.kind);
        }
        return tr;
    }

    tr.parts = SplitIdentifier(token);
    if (tr.parts.empty()) return tr;

    bool any_zh = false;
    tr.part_zh.reserve(tr.parts.size());
    for (const auto& p : tr.parts) {
        std::wstring zh = LookupWord(p);
        if (!zh.empty()) any_zh = true;
        tr.part_zh.push_back(std::move(zh));
    }
    tr.summary = JoinParts(tr.parts, tr.part_zh);
    tr.kind = Classify(token, lower, next_char);
    tr.kind_name = KindName(tr.kind);

    bool code_like = LooksLikeCamelOrSnake(token) || tr.parts.size() >= 2;
    if (from_editor) {
        tr.useful = any_zh || code_like;
    } else {
        tr.useful = code_like && (any_zh || tr.parts.size() >= 2);
    }
    return tr;
}

}  // namespace ctm
