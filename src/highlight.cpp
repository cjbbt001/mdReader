#include "app.h"
#include <algorithm>

namespace {

bool isIdentStart(wchar_t c) { return iswalpha((wint_t)c) || c == L'_' || c == L'$'; }
bool isIdent(wchar_t c) { return iswalpha((wint_t)c) || iswdigit((wint_t)c) || c == L'_' || c == L'$'; }
bool isDigit(wchar_t c) { return iswdigit((wint_t)c) != 0; }

const wchar_t* kCKeywords[] = {
    L"alignas", L"alignof", L"auto", L"bool", L"break", L"case", L"catch", L"char", L"class",
    L"const", L"constexpr", L"continue", L"default", L"delete", L"do", L"double", L"else",
    L"enum", L"explicit", L"export", L"extern", L"false", L"float", L"for", L"friend", L"goto",
    L"if", L"inline", L"int", L"interface", L"long", L"namespace", L"new", L"noexcept",
    L"nullptr", L"operator", L"override", L"private", L"protected", L"public", L"register",
    L"return", L"short", L"signed", L"sizeof", L"static", L"struct", L"switch", L"template",
    L"this", L"throw", L"true", L"try", L"typedef", L"typename", L"union", L"unsigned",
    L"using", L"virtual", L"void", L"volatile", L"while", L"let", L"var", L"function",
    L"async", L"await", L"import", L"from", L"type", L"of", L"in", L"instanceof", L"typeof",
    L"yield", L"extends", L"implements", L"package", L"super", L"def", L"elif", L"except",
    L"finally", L"global", L"lambda", L"None", L"True", L"False", L"and", L"or", L"not",
    L"with", L"as", L"pass", L"raise", L"del", L"assert", L"match", L"fn", L"mut", L"impl",
    L"trait", L"pub", L"use", L"mod", L"self", L"Self", L"ref", L"move", L"loop", L"unsafe",
    L"where", L"dyn", L"go", L"defer", L"chan", L"select", L"range", L"func", L"map",
};

bool isKeyword(const std::wstring& word) {
    for (const wchar_t* k : kCKeywords)
        if (word == k) return true;
    return false;
}

void push(std::vector<Run>& out, const std::wstring& text, int tok) {
    if (text.empty()) return;
    if (!out.empty() && out.back().tok == tok) {
        out.back().text += text;
        return;
    }
    Run r;
    r.text = text;
    r.tok = tok;
    out.push_back(std::move(r));
}

}  // namespace

// A small, dialect-agnostic lexer: comments, strings, numbers and a shared
// keyword table. Enough to make code blocks readable without a real parser.
void highlightCode(const std::wstring& code, const std::wstring& lang, std::vector<Run>& out) {
    out.clear();
    std::wstring l = lang;
    for (auto& c : l) c = (wchar_t)towlower((wint_t)c);

    bool hashComments = l == L"py" || l == L"python" || l == L"sh" || l == L"bash" ||
                        l == L"shell" || l == L"yaml" || l == L"yml" || l == L"toml" ||
                        l == L"ps1" || l == L"powershell" || l == L"r" || l == L"rb";
    bool inBlockComment = false;
    bool inMarkdownFence = false;

    size_t i = 0;
    const size_t n = code.size();
    std::wstring plain;

    auto flushPlain = [&]() {
        if (!plain.empty()) {
            push(out, plain, 0);
            plain.clear();
        }
    };

    while (i < n) {
        wchar_t c = code[i];
        wchar_t next = i + 1 < n ? code[i + 1] : 0;

        if (inBlockComment) {
            size_t end = code.find(L"*/", i);
            if (end == std::wstring::npos) {
                push(out, code.substr(i), 3);
                return;
            }
            push(out, code.substr(i, end + 2 - i), 3);
            i = end + 2;
            inBlockComment = false;
            continue;
        }

        if (c == L'\n') {
            plain += c;
            ++i;
            if (hashComments) flushPlain();
            continue;
        }

        // comments
        if (c == L'/' && next == L'/') {
            flushPlain();
            size_t end = code.find(L'\n', i);
            if (end == std::wstring::npos) end = n;
            push(out, code.substr(i, end - i), 3);
            i = end;
            continue;
        }
        if (c == L'/' && next == L'*') {
            flushPlain();
            inBlockComment = true;
            continue;
        }
        if (c == L'#' && hashComments) {
            flushPlain();
            size_t end = code.find(L'\n', i);
            if (end == std::wstring::npos) end = n;
            push(out, code.substr(i, end - i), 3);
            i = end;
            continue;
        }
        if (l == L"md" || l == L"markdown") {
            if (i == 0 || code[i - 1] == L'\n') {
                if (code.compare(i, 3, L"```") == 0) inMarkdownFence = !inMarkdownFence;
            }
            if (inMarkdownFence) {
                plain += c;
                ++i;
                continue;
            }
            if (c == L'#') {
                flushPlain();
                size_t end = code.find(L'\n', i);
                if (end == std::wstring::npos) end = n;
                push(out, code.substr(i, end - i), 1);
                i = end;
                continue;
            }
        }

        // strings
        if (c == L'"' || c == L'\'' || c == L'`') {
            flushPlain();
            wchar_t quote = c;
            size_t j = i + 1;
            while (j < n) {
                if (code[j] == L'\\') { j += 2; continue; }
                if (code[j] == quote) { ++j; break; }
                if (code[j] == L'\n' && quote != L'`') break;
                ++j;
            }
            push(out, code.substr(i, j - i), 2);
            i = j;
            continue;
        }

        // numbers
        if (isDigit(c) && (i == 0 || !isIdent(code[i - 1]))) {
            flushPlain();
            size_t j = i;
            while (j < n && (isIdent(code[j]) || code[j] == L'.')) ++j;
            push(out, code.substr(i, j - i), 4);
            i = j;
            continue;
        }

        // identifiers / keywords
        if (isIdentStart(c)) {
            size_t j = i;
            while (j < n && isIdent(code[j])) ++j;
            std::wstring word = code.substr(i, j - i);
            if (isKeyword(word)) {
                flushPlain();
                push(out, word, 1);
            } else {
                plain += word;
            }
            i = j;
            continue;
        }

        plain += c;
        ++i;
    }
    flushPlain();
}
