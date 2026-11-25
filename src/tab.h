#pragma once
#include "common.h"
#include "doc.h"
#include "layout.h"
#include <cstdint>
#include <string>
#include <vector>

enum class Mode { Preview, Source };
enum class LineEnding { LF, CRLF, CR };

struct FileStamp {
    uint64_t id = 0, size = 0, modified = 0;
    DWORD volume = 0;
    bool exists = false;
    bool operator==(const FileStamp&) const = default;
};

// Caret / selection inside the source buffer. Positions are (line, col) with
// col counted in UTF-16 units.
struct Pos {
    int line = 0, col = 0;
    bool operator==(const Pos& o) const { return line == o.line && col == o.col; }
    bool operator!=(const Pos& o) const { return !(*this == o); }
    bool operator<(const Pos& o) const { return line < o.line || (line == o.line && col < o.col); }
};

struct Editor {
    std::vector<std::wstring> lines{std::wstring()};
    Pos caret;
    Pos anchor;
    float scrollX = 0.f;
    int firstLine = 0;      // vertical scroll in lines
    int goalCol = -1;       // keeps the column when moving through short lines
    bool modified = false;

    // Undo: whole-buffer snapshots, coalesced while typing.
    struct Snapshot {
        std::vector<std::wstring> lines;
        Pos caret, anchor;
        uint64_t tick = 0;
        size_t bytes = 0;
    };
    std::vector<Snapshot> undo, redo;

    bool hasSelection() const { return !(caret == anchor); }
    void selRange(Pos* a, Pos* b) const {
        if (anchor < caret) { *a = anchor; *b = caret; }
        else { *a = caret; *b = anchor; }
    }
    const std::wstring& line(int i) const { return lines[(size_t)i]; }
    int lineCount() const { return (int)lines.size(); }
    std::wstring text() const;
    void setText(const std::wstring& s);

    void snapshot(uint64_t now);
    bool undoStep();
    bool redoStep();
    Snapshot capture(uint64_t tick) const;
    void trimUndo();

    void moveTo(Pos p, bool extend);
    void moveBy(int dLine, int dCol, bool extend);
    void moveLineEdge(bool toEnd, bool extend);
    void moveDocEdge(bool toEnd, bool extend);

    void insert(const std::wstring& s);
    void insertNewline(bool autoIndent, bool continueList);
    void backspace();
    void deleteForward();
    void deleteSelection();
    std::wstring selectedText() const;

    // Markdown helpers used by the toolbar; they wrap or prefix the selection.
    void wrapSelection(const std::wstring& before, const std::wstring& after);
    void prefixLines(const std::wstring& prefix);
    Pos indexToPos(size_t idx) const;
    size_t posToIndex(Pos p) const;
};

struct Tab {
    Doc doc;
    Layout lay;
    std::vector<TocEntry> toc;

    Mode mode = Mode::Preview;
    float scrollY = 0.f;
    Editor ed;

    std::wstring path, name;
    FileStamp fileStamp;
    bool diskChanged = false;
    bool dirty = false;
    bool utf8Bom = false;
    LineEnding lineEnding = LineEnding::LF;
    bool previewStale = false;
    bool revealEditor = false;
    int editFirst = -1, editLast = -1;
    int pendingBlock = -1;
    float pendingOffset = 0.f;
    float layoutWidth = -1.f;
};
