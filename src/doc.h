#pragma once
#include "common.h"
#include <vector>

enum class BKind { Para, Heading, Code, Quote, Bullet, Ordered, Rule, Table, Image };

// One styled slice of inline text.
struct Run {
    std::wstring text;
    bool bold = false, italic = false, code = false, strike = false;
    bool link = false;
    int tok = 0;  // 0 none, 1 keyword, 2 string, 3 comment, 4 number
};

struct Block {
    BKind kind = BKind::Para;
    int level = 0;          // heading level 1..6
    int listDepth = 0;      // 0 = top level
    int quoteDepth = 0;
    int number = 0;         // ordered list index
    std::vector<Run> runs;
    std::wstring code;      // Code blocks
    std::wstring lang;
    std::vector<std::vector<std::wstring>> cells;  // Table
    int headerRows = 0;
    std::wstring imgSrc, imgAlt;   // Image
    bool task = false, checked = false;  // task list item
};

struct TocEntry {
    int level = 1;
    std::wstring text;
    int blockIndex = 0;
};

struct Doc {
    std::vector<Block> blocks;
    std::wstring path;
    std::wstring name;
    float blank = 0;
};

bool loadDoc(const std::wstring& path, Doc& doc, std::wstring& err);
bool parseText(const std::wstring& text, Doc& doc);
void buildToc(const Doc& doc, std::vector<TocEntry>& out);
