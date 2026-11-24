#include "app.h"
#include <algorithm>
#include <cmath>

namespace {

const float kMarginX = 30.f;
const float kMarginTop = 16.f;
const float kMarginBottom = 48.f;
const float kMaxContent = 860.f;
const float kListIndent = 24.f;
const float kQuoteIndent = 16.f;
const float kCodePad = 12.f;

float contentW(const App& a) {
    float w = a.contentRight() - 2 * kMarginX;
    if (w > kMaxContent) w = kMaxContent;
    if (w < 80.f) w = 80.f;
    return w;
}

float contentX(const App& a) { return std::floor((a.contentRight() - contentW(a)) * 0.5f); }

float gapBefore(const Block& b) {
    switch (b.kind) {
    case BKind::Heading:
        if (b.level <= 1) return 22.f;
        if (b.level == 2) return 20.f;
        return 16.f;
    case BKind::Code: return 6.f;
    case BKind::Rule: return 10.f;
    case BKind::Table: return 10.f;
    case BKind::Bullet:
    case BKind::Ordered: return 2.f;
    case BKind::Para: return 0.f;
    default: return 2.f;
    }
}

float gapAfter(const Block& b) {
    switch (b.kind) {
    case BKind::Heading:
        if (b.level <= 1) return 12.f;
        if (b.level == 2) return 10.f;
        return 8.f;
    case BKind::Code: return 16.f;
    case BKind::Rule: return 18.f;
    case BKind::Table: return 18.f;
    case BKind::Bullet:
    case BKind::Ordered: return 6.f;
    case BKind::Para: return 12.f;
    default: return 8.f;
    }
}

float layoutHeight(IDWriteTextLayout* tl) {
    if (!tl) return 0.f;
    DWRITE_TEXT_METRICS m = {};
    if (FAILED(tl->GetMetrics(&m))) return 0.f;
    return m.height;
}

std::vector<Run> singleRun(const std::wstring& s) {
    std::vector<Run> v(1);
    v[0].text = s;
    return v;
}

ComPtr<IDWriteTextLayout> makeLayout(App& app, const std::vector<Run>& runs,
                                     IDWriteTextFormat* fmt, float maxW) {
    struct Span { UINT32 start, len; const Run* r; };
    std::wstring s;
    std::vector<Span> spans;
    for (const auto& r : runs) {
        if (r.text.empty()) continue;
        spans.push_back({(UINT32)s.size(), (UINT32)r.text.size(), &r});
        s += r.text;
    }
    ComPtr<IDWriteTextLayout> tl;
    if (s.empty() || !fmt) return tl;
    if (FAILED(app.dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, maxW, 100000.f,
                                            tl.put())))
        return tl;

    for (const auto& sp : spans) {
        DWRITE_TEXT_RANGE range = {sp.start, sp.len};
        if (sp.r->bold) tl->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (sp.r->italic) tl->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (sp.r->strike) tl->SetStrikethrough(TRUE, range);
        if (sp.r->code) {
            tl->SetFontFamilyName(L"Consolas", range);
            tl->SetFontSize(app.codeSpanSize, range);
            tl->SetDrawingEffect(app.brCodeText.get(), range);
        }
        if (sp.r->link) tl->SetDrawingEffect(app.brLink.get(), range);
        switch (sp.r->tok) {
        case 1: tl->SetDrawingEffect(app.brKw.get(), range); break;
        case 2: tl->SetDrawingEffect(app.brStr.get(), range); break;
        case 3: tl->SetDrawingEffect(app.brCom.get(), range); break;
        case 4: tl->SetDrawingEffect(app.brNum.get(), range); break;
        default: break;
        }
    }
    return tl;
}

Item layoutTable(App& app, const Block& b, float y, float cx, float cw) {
    Item it;
    it.kind = BKind::Table;
    it.x = cx;
    it.w = cw;
    it.quoteDepth = b.quoteDepth;
    it.y = y + gapBefore(b);

    const float pad = 10.f;
    int rows = (int)b.cells.size();
    int cols = 0;
    for (const auto& r : b.cells) cols = std::max(cols, (int)r.size());
    if (!rows || !cols) { it.h = 0.f; return it; }

    std::vector<float> need(cols, 24.f);
    for (const auto& r : b.cells) {
        for (int c = 0; c < (int)r.size(); ++c) {
            auto tl = makeLayout(app, singleRun(r[c]), app.fmtTable.get(), 10000.f);
            float wid = 0.f;
            if (tl) {
                DWRITE_TEXT_METRICS m = {};
                tl->GetMetrics(&m);
                wid = m.widthIncludingTrailingWhitespace;
            }
            need[c] = std::max(need[c], wid);
        }
    }
    float total = 0.f;
    for (int c = 0; c < cols; ++c) { need[c] += pad * 2; total += need[c]; }
    if (total > cw) {
        float k = cw / total;
        for (auto& w : need) w = std::max(48.f, w * k);
    }
    total = 0.f;
    for (auto w : need) total += w;
    if (total < cw) {
        float extra = (cw - total) / (float)cols;
        for (auto& w : need) w += extra;
    }

    it.colW = need;
    it.colX.assign(cols, it.x);
    for (int c = 1; c < cols; ++c) it.colX[c] = it.colX[c - 1] + it.colW[c - 1];

    it.rows = rows;
    it.cols = cols;
    it.headerRows = std::min(b.headerRows, rows);
    it.cells.resize((size_t)rows * (size_t)cols);
    it.rowY.assign(rows, 0.f);
    it.rowH.assign(rows, 0.f);

    float ry = it.y;
    for (int r = 0; r < rows; ++r) {
        float h = 0.f;
        for (int c = 0; c < cols; ++c) {
            if (c >= (int)b.cells[r].size() || b.cells[r][c].empty()) continue;
            IDWriteTextFormat* f = (r < it.headerRows) ? app.fmtTableHead.get() : app.fmtTable.get();
            auto& slot = it.cells[(size_t)r * (size_t)cols + (size_t)c];
            slot = makeLayout(app, singleRun(b.cells[r][c]), f, it.colW[c] - pad * 2);
            h = std::max(h, layoutHeight(slot.get()));
        }
        it.rowY[r] = ry;
        it.rowH[r] = h + pad * 2;
        ry += it.rowH[r];
    }
    it.h = ry - it.y + 1.f;
    return it;
}

Item layoutBlock(App& app, const Block& b, float y, float cx, float cw) {
    if (b.kind == BKind::Table) return layoutTable(app, b, y, cx, cw);

    Item it;
    it.kind = b.kind;
    it.quoteDepth = b.quoteDepth;
    it.level = b.level;
    it.y = y + gapBefore(b);

    if (b.kind == BKind::Rule) {
        it.x = cx;
        it.w = cw;
        it.h = 2.f;
        return it;
    }

    float ind = b.quoteDepth * kQuoteIndent + b.listDepth * kListIndent;
    float x = cx + ind;
    float w = cw - ind;
    bool isList = (b.kind == BKind::Bullet || b.kind == BKind::Ordered);
    if (isList) { x += 20.f; w -= 20.f; }
    if (w < 60.f) w = 60.f;

    if (b.kind == BKind::Image) {
        std::wstring path = app.hasTab() ? resolveImagePath(app.tab(), b.imgSrc) : b.imgSrc;
        UINT iw = 0, ih = 0;
        int state = 0;
        bool known = imageInfo(path, &iw, &ih, &state);
        const float maxW = std::max(80.f, cw - b.quoteDepth * kQuoteIndent - b.listDepth * kListIndent);
        float drawW = known ? std::min((float)iw, maxW) : maxW;
        float drawH = known ? drawW * (float)ih / (float)iw : drawW * 0.5625f;
        if (drawH > 4000.f) drawH = 4000.f;
        it.x = cx + b.quoteDepth * kQuoteIndent + b.listDepth * kListIndent;
        it.w = drawW;
        it.h = drawH;
        it.imgPath = path;
        if (app.hasTab()) requestImage(app, path);
        return it;
    }

    if (b.kind == BKind::Code) {
        std::wstring code = b.code;
        while (!code.empty() && (code.back() == L'\n' || code.back() == L'\r')) code.pop_back();
        std::vector<Run> codeRuns;
        highlightCode(code, b.lang, codeRuns);
        it.tl = makeLayout(app, codeRuns, app.fmtCode.get(), std::max(60.f, w - kCodePad * 2));
        float h = layoutHeight(it.tl.get());
        it.h = h + kCodePad * 2;
        it.x = x;
        it.w = w;
        return it;
    }

    IDWriteTextFormat* fmt = app.fmtBody.get();
    if (b.kind == BKind::Heading) {
        int lv = b.level < 1 ? 1 : (b.level > 6 ? 6 : b.level);
        fmt = app.fmtHead[lv - 1].get();
    }
    it.tl = makeLayout(app, b.runs, fmt, w);
    if (it.tl) {
        DWRITE_LINE_METRICS lm[2] = {};
        UINT32 count = 0;
        if (SUCCEEDED(it.tl->GetLineMetrics(lm, 2, &count)) && count > 0 && lm[0].baseline > 0.f)
            it.baseline = lm[0].baseline;
    }
    float h = layoutHeight(it.tl.get());
    it.h = h > 0.f ? h : 10.f;
    it.x = x;
    it.w = w;

    if (b.kind == BKind::Bullet) {
        it.marker = b.task ? L"[]" : L"\u2022";
        it.markerRight = false;
    } else if (b.kind == BKind::Ordered) {
        it.marker = std::to_wstring(b.number) + L".";
        it.markerRight = true;
    }
    return it;
}

}  // namespace

void layoutReset(App& app) {
    app.tab().lay.items.clear();
    app.tab().lay.blockY.assign(app.tab().doc.blocks.size(), 0.f);
    app.tab().lay.next = 0;
    app.tab().lay.contentH = kMarginTop;
    app.tab().lay.done = app.tab().doc.blocks.empty();
}

void relayoutFrom(App& app, size_t blockIndex) {
    Layout& L = app.tab().lay;
    if (blockIndex > app.tab().doc.blocks.size()) blockIndex = app.tab().doc.blocks.size();
    while (!L.items.empty() && (size_t)L.items.back().blockIndex >= blockIndex) L.items.pop_back();
    L.next = blockIndex;
    L.done = false;
    L.contentH = L.items.empty()
                     ? kMarginTop
                     : L.items.back().y + L.items.back().h +
                           gapAfter(app.tab().doc.blocks[(size_t)L.items.back().blockIndex]);
}

int blockAtScroll(const App& app) {
    int best = 0;
    for (const auto& it : app.tab().lay.items) {
        if (it.y <= app.tab().scrollY + 1.f) best = it.blockIndex;
        else break;
    }
    return best;
}

void reflowAnchor(App& app) {
    int anchor = blockAtScroll(app);
    layoutReset(app);
    ensureLayoutForView(app);
    if (anchor >= 0 && (size_t)anchor < app.tab().lay.blockY.size())
        scrollTo(app, app.tab().lay.blockY[(size_t)anchor]);
    else
        scrollTo(app, 0.f);
}

void layoutStep(App& app, float untilY, int budgetMs) {
    Layout& L = app.tab().lay;
    if (L.done) return;
    DWORD t0 = GetTickCount();
    float cw = contentW(app);
    float cx = contentX(app);

    while (L.next < app.tab().doc.blocks.size()) {
        const Block& b = app.tab().doc.blocks[L.next];
        Item it = layoutBlock(app, b, L.contentH, cx, cw);
        it.blockIndex = (int)L.next;
        L.contentH = it.y + it.h + gapAfter(b);
        if (L.next < L.blockY.size()) L.blockY[L.next] = it.y;
        L.items.push_back(std::move(it));
        ++L.next;
        if (L.contentH >= untilY) break;
        if ((int)(GetTickCount() - t0) >= budgetMs) break;
    }

    if (L.next >= app.tab().doc.blocks.size()) {
        L.done = true;
        L.contentH += kMarginBottom;
    }
}
