#include "tab.h"
#include <algorithm>

namespace {
uint64_t nowTick() { return GetTickCount64(); }
bool splitsSurrogates(const std::wstring& text, int col) {
    return col > 0 && col < (int)text.size() &&
           IS_HIGH_SURROGATE(text[(size_t)col - 1]) &&
           IS_LOW_SURROGATE(text[(size_t)col]);
}
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
    size_t start = 0;
    while (true) {
        size_t end = s.find_first_of(L"\r\n", start);
        lines.push_back(s.substr(start, end == std::wstring::npos ? end : end - start));
        if (end == std::wstring::npos) break;
        start = end + 1;
        if (s[end] == L'\r' && start < s.size() && s[start] == L'\n') ++start;
    }
    caret = anchor = Pos{0, 0};
    scrollX = 0.f;
    firstLine = 0;
    undo.clear();
    redo.clear();
    modified = false;
}

void Editor::snapshot(uint64_t now) {
    const bool branched = !redo.empty();
    redo.clear();
    if (!branched && !undo.empty() && now - undo.back().tick < 400 && !undo.back().lines.empty() &&
        undo.back().lines.size() == lines.size()) {
        // Coalesce rapid typing into one step: keep the older snapshot.
        undo.back().tick = now;
        return;
    }
    undo.push_back(capture(now));
    trimUndo();
}

Editor::Snapshot Editor::capture(uint64_t tick) const {
    Snapshot state{lines, caret, anchor, tick};
    state.bytes = state.lines.size() * sizeof(std::wstring);
    for (const auto& line : state.lines)
        state.bytes += (line.size() + 1) * sizeof(wchar_t);
    return state;
}

void Editor::trimUndo() {
    constexpr size_t budget = 16 * 1024 * 1024;
    size_t bytes = 0;
    for (const auto& entry : undo) bytes += entry.bytes;
    size_t first = 0;
    while (undo.size() - first > 1 && (undo.size() - first > 200 || bytes > budget))
        bytes -= undo[first++].bytes;
    if (first) undo.erase(undo.begin(), undo.begin() + first);
}

bool Editor::undoStep() {
    if (undo.empty()) return false;
    redo.push_back(capture(nowTick()));

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
    undo.push_back(capture(nowTick()));
    trimUndo();

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
    if (splitsSurrogates(lines[(size_t)p.line], p.col)) --p.col;
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
            if (splitsSurrogates(lines[(size_t)caret.line], col)) col += dCol > 0 ? 1 : -1;
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
        caret.col = std::min(goalCol, len);
        if (splitsSurrogates(lines[(size_t)line], caret.col)) --caret.col;
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
    size_t end = s.find_first_of(L"\r\n");
    if (end == std::wstring::npos) {
        lines[(size_t)caret.line].insert((size_t)caret.col, s);
        caret.col += (int)s.size();
    } else {
        std::wstring tail = lines[(size_t)caret.line].substr((size_t)caret.col);
        lines[(size_t)caret.line].replace((size_t)caret.col, std::wstring::npos, s, 0, end);
        std::vector<std::wstring> added;
        while (end != std::wstring::npos) {
            size_t start = end + 1;
            if (s[end] == L'\r' && start < s.size() && s[start] == L'\n') ++start;
            end = s.find_first_of(L"\r\n", start);
            added.push_back(s.substr(start, end == std::wstring::npos ? end : end - start));
        }
        caret.col = (int)added.back().size();
        added.back() += tail;
        lines.insert(lines.begin() + caret.line + 1,
                     std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
        caret.line += (int)added.size();
    }
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
            if (k > 0 && k <= 9 && k + 1 < rest.size() && (rest[k] == L'.' || rest[k] == L')') &&
                rest[k + 1] == L' ') {
                int n = _wtoi(rest.substr(0, k).c_str());
                marker = std::to_wstring(n + 1) + rest[k] + L' ';
            }
        }
        size_t markerEnd = marker.empty() ? 0 : rest.find(L' ') + 1;
        if (!marker.empty() && marker.front() != L'>' &&
            rest.size() >= markerEnd + 3 && rest[markerEnd] == L'[' && rest[markerEnd + 2] == L']' &&
            (rest[markerEnd + 1] == L' ' || rest[markerEnd + 1] == L'x' || rest[markerEnd + 1] == L'X') &&
            (rest.size() == markerEnd + 3 || rest[markerEnd + 3] == L' ')) {
            marker += L"[ ] ";
            markerEnd += 3;
        }
        if (markerEnd && tail.empty() && rest.find_first_not_of(L" \t", markerEnd) == std::wstring::npos) {
            l = indent;
            caret.col = (int)indent.size();
            anchor = caret;
            modified = true;
            return;
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
    if (!hasSelection() && caret == Pos{}) return;
    snapshot(nowTick());
    if (hasSelection()) { deleteSelection(); return; }
    if (caret.col > 0) {
        auto& line = lines[(size_t)caret.line];
        int start = caret.col - 1;
        if (splitsSurrogates(line, start)) --start;
        line.erase((size_t)start, (size_t)(caret.col - start));
        caret.col = start;
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
    if (!hasSelection() && caret.line == lineCount() - 1 &&
        caret.col == (int)lines.back().size()) return;
    snapshot(nowTick());
    if (hasSelection()) { deleteSelection(); return; }
    std::wstring& l = lines[(size_t)caret.line];
    if (caret.col < (int)l.size()) {
        l.erase((size_t)caret.col, splitsSurrogates(l, caret.col + 1) ? 2 : 1);
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
            if (caret.line == i && caret.col > (int)k)
                caret.col = std::max((int)k, caret.col - (int)prefix.size());
            if (anchor.line == i && anchor.col > (int)k)
                anchor.col = std::max((int)k, anchor.col - (int)prefix.size());
        }
    } else {
        for (int i = a.line; i <= b.line; ++i) {
            std::wstring& l = lines[(size_t)i];
            size_t k = 0;
            while (k < l.size() && (l[k] == L' ' || l[k] == L'\t')) ++k;
            l.insert(k, prefix);
            if (caret.line == i && caret.col >= (int)k) caret.col += (int)prefix.size();
            if (anchor.line == i && anchor.col >= (int)k) anchor.col += (int)prefix.size();
        }
    }
    modified = true;
}
