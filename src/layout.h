#pragma once
#include "common.h"
#include "doc.h"

// One laid-out block, ready to paint.
struct Item {
    int blockIndex = 0;
    BKind kind = BKind::Para;
    float y = 0, h = 0, x = 0, w = 0;
    ComPtr<IDWriteTextLayout> tl;

    int level = 0;
    int quoteDepth = 0;
    std::wstring marker;      // bullet / "1."
    bool markerRight = false;

    // Table
    std::vector<ComPtr<IDWriteTextLayout>> cells;
    std::vector<float> colX, colW, rowY, rowH;
    int rows = 0, cols = 0, headerRows = 0;
};

struct Layout {
    std::vector<Item> items;
    std::vector<float> blockY;   // top of each block, filled as it is laid out
    size_t next = 0;
    float contentH = 0;
    bool done = false;
};

struct App;

void layoutReset(App& app);
// Lays out blocks until the bottom of the viewport (untilY) is covered, or the
// time budget runs out. Leaves done == false when work remains.
void layoutStep(App& app, float untilY, int budgetMs);
// Drops every item at or after `blockIndex` and lays the rest out again
// (image decoding, panel toggles, edits switching back to preview).
void relayoutFrom(App& app, size_t blockIndex);
// Re-anchors the scroll position on the block currently at the top.
void reflowAnchor(App& app);
int blockAtScroll(const App& app);
