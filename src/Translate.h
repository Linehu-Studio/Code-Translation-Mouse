#pragma once

#include "Util.h"
#include <unordered_map>

namespace ctm {

enum class TokenKind {
    Keyword,
    Type,
    Function,
    Variable,
    Constant,
    Preprocessor,
    Unknown
};

struct Translation {
    std::wstring token;
    TokenKind kind = TokenKind::Unknown;
    std::wstring kind_name;
    std::vector<std::wstring> parts;
    std::vector<std::wstring> part_zh;
    std::wstring summary;
    bool useful = false;
};

class Translator {
public:
    bool Load();
    Translation Translate(const std::wstring& token, wchar_t next_char, bool from_editor) const;

    static std::vector<std::wstring> SplitIdentifier(std::wstring token);

private:
    void ParseDictionary(std::string_view utf8, bool override_existing);
    bool LoadFromFile(const std::wstring& path, bool override_existing);
    bool LoadFromResource();

    std::wstring LookupWord(std::wstring_view part) const;
    TokenKind Classify(const std::wstring& token, const std::wstring& lower, wchar_t next_char) const;
    static std::wstring KindName(TokenKind kind);
    static std::wstring StripCommonPrefix(std::wstring token);

    std::unordered_map<std::wstring, std::wstring> keywords_;
    std::unordered_map<std::wstring, std::wstring> types_;
    std::unordered_map<std::wstring, std::wstring> words_;
};

}  // namespace ctm
