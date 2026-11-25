#pragma once
#include "common.h"
#include "doc.h"

// One laid-out block, ready to paint.
struct Item {
    int blockIndex = 0;
    BKind kind = BKind::Para;
    float y = 0, h = 0, x = 0, w = 0;
    ComPtr<IDWriteTextLayout> tl;

    int quoteDepth = 0;
    float baseline = 20.f;   // first line baseline, relative to y
    float codePadY = 12.f;
    std::wstring marker;      // bullet / "1."
    std::wstring imgPath;     // resolved image path

    // Table
    std::vector<ComPtr<IDWriteTextLayout>> cells;
    std::vector<float> colX, colW, rowY, rowH;
    int rows = 0, cols = 0, headerRows = 0;
};

struct PreviewEdit {
    ComPtr<IDWriteTextLayout> text, prefix;
    int block = -1;
    bool replacesBlock = false;
    UINT32 prefixLength = 0;
    float x = 0.f, y = 0.f, h = 0.f, lineHeight = 0.f, prefixX = 0.f, extra = 0.f;
    float scrollX = 0.f, maxScrollX = 0.f;
};

struct Layout {
    std::vector<Item> items;
    std::vector<float> blockY;   // top of each block, filled as it is laid out
    size_t next = 0;
    PreviewEdit edit;
    float contentH = 0, heightFloor = 0;
    bool done = false;
};

struct App;

void layoutReset(App& app);
// Lays out blocks until the bottom of the viewport (untilY) is covered, or the
// time budget runs out. Leaves done == false when work remains.
void layoutStep(App& app, float untilY, int budgetMs);
void updateDocumentLayout(App& app, Doc doc);
void clearPreviewLayout(App& app);
void layoutPreviewEditor(App& app);
// Re-anchors the scroll position on the block currently at the top.
void reflowAnchor(App& app);
int blockAtScroll(const App& app);
