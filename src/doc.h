#pragma once
#include "common.h"
#include <vector>

enum class BKind { Para, Heading, Code, Quote, Bullet, Ordered, Rule, Table, Image };

// One styled slice of inline text.
struct Run {
    std::wstring text;
    bool bold = false, italic = false, code = false, strike = false;
    int link = -1;
    int tok = 0;  // 0 none, 1 keyword, 2 string, 3 comment, 4 number
    bool operator==(const Run&) const = default;
};

struct SourceStyle {
    UINT32 start, length;
    bool bold, italic, code, strike, link;
};

struct Block {
    BKind kind = BKind::Para;
    int firstLine = -1, lastLine = -1;
    int level = 0;          // heading level 1..6
    int listDepth = 0;      // 0 = top level
    int quoteDepth = 0;
    int number = 0;         // ordered list index
    std::vector<Run> runs;
    std::wstring code;      // Code blocks
    std::wstring lang;
    bool fenced = false;
    std::vector<std::vector<std::vector<Run>>> cells;  // Table
    int headerRows = 0;
    std::wstring imgSrc, imgAlt;   // Image
    bool task = false, checked = false;  // task list item
    bool continuation = false;    // Source line within the preceding list item.
};

struct TocEntry {
    int level = 1;
    std::wstring text;
    int blockIndex = 0;
};

struct Doc {
    std::vector<Block> blocks;
    std::vector<std::wstring> links;
};

bool parseText(const std::wstring& text, Doc& doc, std::vector<SourceStyle>* styles = nullptr);
void buildToc(const Doc& doc, std::vector<TocEntry>& out);
