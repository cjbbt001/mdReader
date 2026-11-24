#include "tab.h"
#include <algorithm>

namespace {
uint64_t nowTick() { return GetTickCount64(); }
bool isWordChar(wchar_t c) { return iswalnum((wint_t)c) || c == L'_'; }
}  // namespace

std::wstring Editor::text() const {
    std::wstring s;
    size_t total = 0;
    for (const auto& l : lines) total += l.size() + 1;
    s.reserve(total);
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) s += L'\n';
        s += lines[i];
    }
    return s;
}

void Editor::setText(const std::wstring& s) {
    lines.clear();
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') { lines.push_back(cur); cur.clear(); }
        else cur += c;
    }
    lines.push_back(cur);
    caret = anchor = Pos{0, 0};
    scrollX = 0.f;
    firstLine = 0;
    undo.clear();
    redo.clear();
    modified = false;
}

void Editor::snapshot(uint64_t now) {
    if (!undo.empty() && now - undo.back().tick < 400 && !undo.back().lines.empty() &&
        undo.back().lines.size() == lines.size()) {
        // Coalesce rapid typing into one step: keep the older snapshot.
        undo.back().tick = now;
        return;
    }
    Snapshot s;
    s.lines = lines;
    s.caret = caret;
    s.anchor = anchor;
    s.tick = now;
    undo.push_back(std::move(s));
    if (undo.size() > 200) undo.erase(undo.begin());
    redo.clear();
}

bool Editor::undoStep() {
    if (undo.empty()) return false;
    Snapshot cur;
    cur.lines = lines;
    cur.caret = caret;
    cur.anchor = anchor;
    cur.tick = nowTick();
    redo.push_back(std::move(cur));

    Snapshot s = std::move(undo.back());
    undo.pop_back();
    lines = std::move(s.lines);
    caret = s.caret;
    anchor = s.anchor;
    if (caret.line >= (int)lines.size()) caret.line = (int)lines.size() - 1;
    if (caret.col > (int)lines[(size_t)caret.line].size()) caret.col = (int)lines[(size_t)caret.line].size();
    modified = true;
    return true;
}

bool Editor::redoStep() {
    if (redo.empty()) return false;
    Snapshot cur;
    cur.lines = lines;
    cur.caret = caret;
    cur.anchor = anchor;
    cur.tick = nowTick();
    undo.push_back(std::move(cur));

    Snapshot s = std::move(redo.back());
    redo.pop_back();
    lines = std::move(s.lines);
    caret = s.caret;
    anchor = s.anchor;
    if (caret.line >= (int)lines.size()) caret.line = (int)lines.size() - 1;
    modified = true;
    return true;
}

void Editor::moveTo(Pos p, bool extend) {
    if (p.line < 0) p.line = 0;
    if (p.line >= (int)lines.size()) p.line = (int)lines.size() - 1;
    int len = (int)lines[(size_t)p.line].size();
    if (p.col < 0) p.col = 0;
    if (p.col > len) p.col = len;
    caret = p;
    if (!extend) anchor = caret;
    goalCol = -1;
}

void Editor::moveBy(int dLine, int dCol, bool extend) {
    if (dCol) {
        int col = caret.col + dCol;
        if (col < 0) {
            if (caret.line > 0) { caret.line--; caret.col = (int)lines[(size_t)caret.line].size(); }
            else caret.col = 0;
        } else if (col > (int)lines[(size_t)caret.line].size()) {
            if (caret.line + 1 < (int)lines.size()) { caret.line++; caret.col = 0; }
            else caret.col = (int)lines[(size_t)caret.line].size();
        } else {
            caret.col = col;
        }
        goalCol = -1;
    }
    if (dLine) {
        if (goalCol < 0) goalCol = caret.col;
        int line = caret.line + dLine;
        if (line < 0) line = 0;
        if (line >= (int)lines.size()) line = (int)lines.size() - 1;
        caret.line = line;
        int len = (int)lines[(size_t)line].size();
        caret.col = goalCol > len ? len : goalCol;
    }
    if (!extend) anchor = caret;
}

void Editor::moveLineEdge(bool toEnd, bool extend) {
    caret.col = toEnd ? (int)lines[(size_t)caret.line].size() : 0;
    if (!extend) anchor = caret;
    goalCol = -1;
}

void Editor::moveDocEdge(bool toEnd, bool extend) {
    if (toEnd) caret = Pos{(int)lines.size() - 1, (int)lines.back().size()};
    else caret = Pos{0, 0};
    if (!extend) anchor = caret;
    goalCol = -1;
}

void Editor::deleteSelection() {
    if (!hasSelection()) return;
    Pos a, b;
    selRange(&a, &b);
    if (a.line == b.line) {
        lines[(size_t)a.line].erase((size_t)a.col, (size_t)(b.col - a.col));
    } else {
        std::wstring head = lines[(size_t)a.line].substr(0, (size_t)a.col);
        std::wstring tail = lines[(size_t)b.line].substr((size_t)b.col);
        lines.erase(lines.begin() + a.line + 1, lines.begin() + b.line + 1);
        lines[(size_t)a.line] = head + tail;
    }
    caret = anchor = a;
    modified = true;
}

std::wstring Editor::selectedText() const {
    if (!hasSelection()) return std::wstring();
    Pos a, b;
    selRange(&a, &b);
    if (a.line == b.line) return lines[(size_t)a.line].substr((size_t)a.col, (size_t)(b.col - a.col));
    std::wstring out = lines[(size_t)a.line].substr((size_t)a.col);
    for (int i = a.line + 1; i < b.line; ++i) { out += L'\n'; out += lines[(size_t)i]; }
    out += L'\n';
    out += lines[(size_t)b.line].substr(0, (size_t)b.col);
    return out;
}

void Editor::insert(const std::wstring& s) {
    if (s.empty()) return;
    deleteSelection();
    std::wstring& l = lines[(size_t)caret.line];
    l.insert((size_t)caret.col, s);
    caret.col += (int)s.size();
    anchor = caret;
    modified = true;
}

void Editor::insertNewline(bool autoIndent, bool continueList) {
    snapshot(nowTick());
    deleteSelection();
    std::wstring& l = lines[(size_t)caret.line];
    std::wstring head = l.substr(0, (size_t)caret.col);
    std::wstring tail = l.substr((size_t)caret.col);

    std::wstring indent;
    if (autoIndent) {
        size_t i = 0;
        while (i < head.size() && (head[i] == L' ' || head[i] == L'\t')) ++i;
        indent = head.substr(0, i);
    }
    if (continueList) {
        // "- ", "* ", "1. ", "> ", "- [ ] " continue on the next line
        size_t i = 0;
        while (i < head.size() && (head[i] == L' ' || head[i] == L'\t')) ++i;
        std::wstring rest = head.substr(i);
        std::wstring marker;
        if (rest.size() >= 2 && (rest[0] == L'-' || rest[0] == L'*' || rest[0] == L'+') &&
            rest[1] == L' ')
            marker = rest.substr(0, 2);
        else if (rest.size() >= 2 && rest[0] == L'>' && rest[1] == L' ') marker = rest.substr(0, 2);
        else {
            size_t k = 0;
            while (k < rest.size() && iswdigit(rest[k])) ++k;
            if (k > 0 && k + 1 < rest.size() && (rest[k] == L'.' || rest[k] == L')') &&
                rest[k + 1] == L' ') {
                int n = _wtoi(rest.substr(0, k).c_str());
                wchar_t buf[32];
                swprintf(buf, 32, L"%d%c ", n + 1, rest[k]);
                marker = buf;
            }
        }
        indent += marker;
    }

    l = head;
    lines.insert(lines.begin() + caret.line + 1, indent + tail);
    caret.line++;
    caret.col = (int)indent.size();
    anchor = caret;
    modified = true;
}

void Editor::backspace() {
    snapshot(nowTick());
    if (hasSelection()) { deleteSelection(); return; }
    if (caret.col > 0) {
        lines[(size_t)caret.line].erase((size_t)caret.col - 1, 1);
        caret.col--;
    } else if (caret.line > 0) {
        int prevLen = (int)lines[(size_t)caret.line - 1].size();
        lines[(size_t)caret.line - 1] += lines[(size_t)caret.line];
        lines.erase(lines.begin() + caret.line);
        caret.line--;
        caret.col = prevLen;
    }
    anchor = caret;
    modified = true;
}

void Editor::deleteForward() {
    snapshot(nowTick());
    if (hasSelection()) { deleteSelection(); return; }
    std::wstring& l = lines[(size_t)caret.line];
    if (caret.col < (int)l.size()) {
        l.erase((size_t)caret.col, 1);
    } else if (caret.line + 1 < (int)lines.size()) {
        l += lines[(size_t)caret.line + 1];
        lines.erase(lines.begin() + caret.line + 1);
    }
    modified = true;
}

Pos Editor::indexToPos(size_t idx) const {
    Pos p;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (idx <= lines[i].size()) { p.line = (int)i; p.col = (int)idx; return p; }
        idx -= lines[i].size() + 1;
    }
    p.line = (int)lines.size() - 1;
    p.col = (int)lines.back().size();
    return p;
}

size_t Editor::posToIndex(Pos p) const {
    size_t idx = 0;
    for (int i = 0; i < p.line && i < (int)lines.size(); ++i) idx += lines[(size_t)i].size() + 1;
    return idx + (size_t)p.col;
}

void Editor::wrapSelection(const std::wstring& before, const std::wstring& after) {
    snapshot(nowTick());
    if (!hasSelection()) {
        // No selection: insert the pair and put the caret between them.
        std::wstring& l = lines[(size_t)caret.line];
        l.insert((size_t)caret.col, before + after);
        caret.col += (int)before.size();
        anchor = caret;
    } else {
        Pos a, b;
        selRange(&a, &b);
        if (a.line == b.line) {
            std::wstring& l = lines[(size_t)a.line];
            l.insert((size_t)b.col, after);
            l.insert((size_t)a.col, before);
            caret = Pos{a.line, b.col + (int)before.size() + (int)after.size()};
            anchor = Pos{a.line, a.col + (int)before.size()};
        } else {
            lines[(size_t)b.line].insert((size_t)b.col, after);
            lines[(size_t)a.line].insert((size_t)a.col, before);
            caret = Pos{b.line, b.col + (int)before.size() + (int)after.size()};
            anchor = Pos{a.line, a.col + (int)before.size()};
        }
    }
    modified = true;
}

void Editor::prefixLines(const std::wstring& prefix) {
    snapshot(nowTick());
    Pos a, b;
    selRange(&a, &b);
    bool allEmpty = true;
    for (int i = a.line; i <= b.line; ++i) {
        const std::wstring& l = lines[(size_t)i];
        size_t k = 0;
        while (k < l.size() && (l[k] == L' ' || l[k] == L'\t')) ++k;
        if (l.compare(k, prefix.size(), prefix) != 0) { allEmpty = false; break; }
    }
    if (allEmpty) {
        for (int i = a.line; i <= b.line; ++i) {
            std::wstring& l = lines[(size_t)i];
            size_t k = 0;
            while (k < l.size() && (l[k] == L' ' || l[k] == L'\t')) ++k;
            l.erase(k, prefix.size());
            if (l.empty() && i == a.line && a.line == b.line) caret.col = a.col;
        }
    } else {
        for (int i = a.line; i <= b.line; ++i) {
            std::wstring& l = lines[(size_t)i];
            size_t k = 0;
            while (k < l.size() && (l[k] == L' ' || l[k] == L'\t')) ++k;
            l.insert(k, prefix);
        }
        caret.col += (int)prefix.size();
        anchor.col += (int)prefix.size();
    }
    modified = true;
}
