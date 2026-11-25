#include "app.h"
#include <algorithm>
#include <cmath>
#include <string_view>

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
    case BKind::Ordered:
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
    case BKind::Ordered:
    case BKind::Para: return 0.f;
    default: return 8.f;
    }
}

float layoutHeight(IDWriteTextLayout* tl) {
    if (!tl) return 0.f;
    DWRITE_TEXT_METRICS m = {};
    if (FAILED(tl->GetMetrics(&m))) return 0.f;
    return m.height;
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
        if (sp.r->link >= 0) {
            tl->SetDrawingEffect(app.brLink.get(), range);
            tl->SetUnderline(TRUE, range);
        }
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
            auto tl = makeLayout(app, r[c], app.fmtTable.get(), 10000.f);
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
            slot = makeLayout(app, b.cells[r][c], f, it.colW[c] - pad * 2);
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
    if (isList) {
        const float gutter = b.kind == BKind::Ordered && !b.task ?
            std::max(20.f, 11.f + 9.f * (float)std::to_wstring(b.number).size()) : 20.f;
        x += gutter;
        w -= gutter;
    }
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
        if (!code.empty() && code.back() == L'\n') code.pop_back();
        std::vector<Run> codeRuns;
        highlightCode(code, b.lang, codeRuns);
        it.tl = makeLayout(app, codeRuns, app.fmtCode.get(), std::max(60.f, w - kCodePad * 2));
        float h = layoutHeight(it.tl.get());
        FLOAT lineHeight = 0.f, baseline = 0.f;
        DWRITE_LINE_SPACING_METHOD method;
        app.fmtCode->GetLineSpacing(&method, &lineHeight, &baseline);
        it.codePadY = b.fenced ? lineHeight : kCodePad;
        if (!b.code.empty()) h = std::max(h, lineHeight);
        it.h = h + it.codePadY * 2;
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
    FLOAT lineHeight = 0.f;
    DWRITE_LINE_SPACING_METHOD method;
    fmt->GetLineSpacing(&method, &lineHeight, &it.baseline);
    it.h = std::max(lineHeight, layoutHeight(it.tl.get()));
    if (b.kind == BKind::Heading && b.firstLine != b.lastLine) it.h += lineHeight;
    it.x = x;
    it.w = w;

    if (b.continuation) return it;
    if (b.task || b.kind == BKind::Bullet) {
        it.marker = b.task ? L"[]" : L"\u2022";
    } else if (b.kind == BKind::Ordered) {
        it.marker = std::to_wstring(b.number) + L".";
    }
    return it;
}

// Shift cached geometry when editing adds or removes real visual rows.
void shiftItem(Item& item, float distance) {
    item.y += distance;
    for (float& y : item.rowY) y += distance;
}

void shiftBlocks(Layout& layout, int first, float distance) {
    if (distance == 0.f) return;
    for (int i = first; i < (int)layout.next; ++i) {
        shiftItem(layout.items[(size_t)i], distance);
        layout.blockY[(size_t)i] += distance;
    }
    layout.contentH += distance;
}

bool sameAppearance(const Block& a, const Block& b) {
    return a.kind == b.kind && a.level == b.level && a.listDepth == b.listDepth &&
           a.quoteDepth == b.quoteDepth && a.number == b.number && a.runs == b.runs &&
           a.code == b.code && a.lang == b.lang && a.fenced == b.fenced && a.cells == b.cells &&
           a.headerRows == b.headerRows && a.imgSrc == b.imgSrc && a.imgAlt == b.imgAlt &&
           a.task == b.task && a.checked == b.checked && a.continuation == b.continuation &&
           (a.kind != BKind::Heading || (a.firstLine == a.lastLine) == (b.firstLine == b.lastLine));
}

float blockEnd(const Item& item, const Block& block) {
    return item.y + item.h + gapAfter(block);
}

}  // namespace

void clearPreviewLayout(App& app) {
    Layout& layout = app.tab().lay;
    const auto& edit = layout.edit;
    if (edit.block < 0) return;
    shiftBlocks(layout, edit.block + edit.replacesBlock, -edit.extra);
    layout.edit = {};
}

void layoutReset(App& app) {
    Tab& tab = app.tab();
    tab.lay = {};
    tab.lay.blockY.assign(tab.doc.blocks.size(), 0.f);
    tab.lay.contentH = kMarginTop;
    tab.lay.done = tab.doc.blocks.empty();
    tab.layoutWidth = app.contentRight();
}

void updateDocumentLayout(App& app, Doc doc) {
    Tab& tab = app.tab();
    Layout& layout = tab.lay;
    clearPreviewLayout(app);
    const auto& before = tab.doc.blocks;
    const auto& after = doc.blocks;
    size_t first = 0;
    while (first < std::min(before.size(), after.size()) &&
           sameAppearance(before[first], after[first])) ++first;

    if (first == after.size() && before.size() == after.size()) {
        tab.doc = std::move(doc);
        return;
    }
    size_t oldEnd = before.size(), newEnd = after.size();
    while (oldEnd > first && newEnd > first && sameAppearance(before[oldEnd - 1], after[newEnd - 1])) {
        --oldEnd;
        --newEnd;
    }

    const float oldHeight = layout.contentH;
    const int anchor = blockAtScroll(app);
    const float anchorOffset = tab.scrollY -
        (layout.next ? layout.blockY[(size_t)anchor] : 0.f);
    const size_t oldNext = layout.next;
    const size_t kept = std::min(first, oldNext);
    const size_t changed = newEnd - first;

    // Reuse the already measured suffix. Large insertions go through the normal
    // layout budget to keep the message loop responsive.
    std::vector<Item> suffix;
    if (first < oldNext && changed <= 32 && oldEnd < oldNext) {
        suffix.reserve(oldNext - oldEnd);
        for (size_t i = oldEnd; i < oldNext; ++i)
            suffix.push_back(std::move(layout.items[i]));
    }
    layout.items.resize(kept);
    layout.blockY.resize(after.size());
    layout.next = kept;
    layout.contentH = kept ? blockEnd(layout.items.back(), after[kept - 1]) : kMarginTop;
    if (kept == first && changed <= 32) {
        const float width = contentW(app), x = contentX(app);
        for (size_t i = first; i < newEnd; ++i) {
            Item item = layoutBlock(app, after[i], layout.contentH, x, width);
            item.blockIndex = (int)i;
            layout.blockY[i] = item.y;
            layout.contentH = blockEnd(item, after[i]);
            layout.items.push_back(std::move(item));
        }
        layout.next = newEnd;
        for (auto& item : suffix) {
            const size_t i = layout.next++;
            shiftItem(item, layout.contentH + gapBefore(after[i]) - item.y);
            item.blockIndex = (int)i;
            layout.blockY[i] = item.y;
            layout.contentH = blockEnd(item, after[i]);
            layout.items.push_back(std::move(item));
        }
    }
    layout.done = layout.next == after.size();
    if (layout.done && !after.empty()) layout.contentH += kMarginBottom;
    layout.heightFloor = layout.done ? 0.f : std::max(oldHeight, layout.heightFloor);
    auto updatedIndex = [&](int index) {
        if (index < (int)first) return index;
        if (index >= (int)oldEnd) return index - (int)oldEnd + (int)newEnd;
        return (int)first;
    };
    if (tab.pendingBlock >= 0) {
        tab.pendingBlock = after.empty() ? -1 :
            std::min(updatedIndex(tab.pendingBlock), (int)after.size() - 1);
    } else if (oldNext && anchorOffset >= 0.f && !after.empty()) {
        const int updated = std::min(updatedIndex(anchor), (int)after.size() - 1);
        if (updated < (int)layout.next) tab.scrollY = layout.blockY[(size_t)updated] + anchorOffset;
        else {
            tab.pendingBlock = updated;
            tab.pendingOffset = anchorOffset;
        }
    }
    tab.doc = std::move(doc);
}

int blockAtScroll(const App& app) {
    const Tab& tab = app.tab();
    const auto end = tab.lay.blockY.begin() + tab.lay.next;
    auto it = std::upper_bound(tab.lay.blockY.begin(), end, tab.scrollY + 1.f);
    return it == tab.lay.blockY.begin() ? 0 : (int)(it - tab.lay.blockY.begin()) - 1;
}

void reflowAnchor(App& app) {
    Tab& tab = app.tab();
    const int anchor = tab.pendingBlock >= 0 ? tab.pendingBlock : blockAtScroll(app);
    const float offset = tab.pendingBlock >= 0 ? tab.pendingOffset :
                         (tab.lay.next ? tab.scrollY - tab.lay.blockY[(size_t)anchor] : 0.f);
    layoutReset(app);
    if (!tab.doc.blocks.empty()) scrollToBlock(app, anchor, offset);
    else ensureLayoutForView(app);
}


namespace {

bool inlineBlock(BKind kind) {
    return kind == BKind::Para || kind == BKind::Heading || kind == BKind::Quote ||
           kind == BKind::Bullet || kind == BKind::Ordered;
}

bool space(wchar_t ch) { return ch == L' ' || ch == L'\t'; }

size_t markdownPrefix(std::wstring_view line, BKind& kind, int& level) {
    size_t pos = 0;
    auto skipSpace = [&] { while (pos < line.size() && space(line[pos])) ++pos; };
    skipSpace();
    while (pos < line.size() && line[pos] == L'>') {
        ++pos;
        skipSpace();
    }
    auto boundary = [&](size_t at) { return at == line.size() || space(line[at]); };
    const size_t marker = pos;
    while (pos < line.size() && line[pos] >= L'0' && line[pos] <= L'9') ++pos;
    const bool ordered = pos > marker && pos - marker <= 9 && pos < line.size() &&
                         (line[pos] == L'.' || line[pos] == L')');
    const bool bullet = pos == marker && pos < line.size() &&
                        (line[pos] == L'-' || line[pos] == L'+' || line[pos] == L'*');
    if ((ordered || bullet) && boundary(pos + 1)) {
        kind = ordered ? BKind::Ordered : BKind::Bullet;
        ++pos;
        skipSpace();
        if (pos + 3 <= line.size() && line[pos] == L'[' && line[pos + 2] == L']' &&
            (line[pos + 1] == L' ' || line[pos + 1] == L'x' || line[pos + 1] == L'X') &&
            boundary(pos + 3)) {
            pos += 3;
            skipSpace();
        }
    } else pos = marker;

    const size_t heading = pos;
    while (pos < line.size() && line[pos] == L'#') ++pos;
    if (pos > heading && pos - heading <= 6 && boundary(pos)) {
        kind = BKind::Heading;
        level = (int)(pos - heading);
        skipSpace();
        return pos;
    }
    return heading;
}

void styleMarkdown(App& app, IDWriteTextLayout* layout, const std::vector<SourceStyle>& styles,
                   UINT32 offset, UINT32 length) {
    if (!layout) return;
    auto mute = [&](UINT32 from, UINT32 to) {
        if (from >= to) return;
        layout->SetDrawingEffect(app.brMuted.get(), {from, to - from});
    };
    UINT32 styledEnd = 0;
    for (const auto& styled : styles) {
        const UINT32 end = styled.start + styled.length;
        if (end <= offset) continue;
        const UINT32 start = std::max(styled.start, offset) - offset;
        const DWRITE_TEXT_RANGE range = {start, end - offset - start};
        mute(styledEnd, start);
        if (styled.bold) layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
        if (styled.italic) layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
        if (styled.strike) layout->SetStrikethrough(TRUE, range);
        if (styled.code) {
            layout->SetFontFamilyName(L"Consolas", range);
            layout->SetFontSize(app.codeSpanSize, range);
            layout->SetDrawingEffect(app.brCodeText.get(), range);
        }
        if (styled.link) {
            layout->SetDrawingEffect(app.brLink.get(), range);
            layout->SetUnderline(TRUE, range);
        }
        styledEnd = end - offset;
    }
    mute(styledEnd, length);
}

void fitEditorRows(IDWriteTextLayout* text, float height, float width) {
    if (layoutHeight(text) <= height + 0.1f) return;

    // Extra Markdown syntax can take the wrapped text past its preview height.
    // Keep the block in place and give that syntax room inside the editor.
    text->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TEXT_METRICS metrics = {};
    text->GetMetrics(&metrics);
    if (metrics.height > height + 0.1f || metrics.lineCount <= 1) return;

    float low = width, high = std::max(width, metrics.widthIncludingTrailingWhitespace + 1.f);
    text->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    for (int i = 0; i < 16 && high - low > 1.f; ++i) {
        const float trial = (high + low) * 0.5f;
        text->SetMaxWidth(trial);
        if (layoutHeight(text) > height + 0.1f) low = trial;
        else high = trial;
    }
    text->SetMaxWidth(high);
}

}  // namespace

void layoutPreviewEditor(App& app) {
    Tab& tab = app.tab();
    Layout& layout = tab.lay;
    if (tab.editFirst < 0) return;
    clearPreviewLayout(app);
    const int index = blockForLine(tab.doc, tab.editFirst);
    const Block* original = index < (int)tab.doc.blocks.size() ? &tab.doc.blocks[(size_t)index] : nullptr;
    const bool replaces = original && original->firstLine <= tab.editFirst;
    if (index >= (int)layout.next && !layout.done) return;

    const Item* base = replaces ? &layout.items[(size_t)index] : nullptr;
    const float width = contentW(app), left = contentX(app);
    std::wstring raw;
    for (int line = tab.editFirst; line <= tab.editLast; ++line) {
        if (line > tab.editFirst) raw += L'\n';
        raw += tab.ed.line(line);
    }
    Doc local;
    std::vector<SourceStyle> styles;
    // Standalone parsing of a nested item needs its container indentation removed.
    const size_t nonSpace = raw.find_first_not_of(L" \t");
    size_t removed = base && inlineBlock(base->kind) && nonSpace != std::wstring::npos &&
                     nonSpace >= 4 ? nonSpace : 0;
    parseText(raw.substr(removed), local, &styles);
    for (auto& style : styles) style.start += (UINT32)removed;
    BKind kind = base ? base->kind : BKind::Para;
    int level = base ? original->level : 0;
    const size_t firstBreak = raw.find(L'\n');
    const std::wstring_view firstLine = std::wstring_view(raw).substr(0, firstBreak);
    BKind markerKind = kind;
    int markerLevel = level;
    const UINT32 markerEnd = inlineBlock(kind) ? (UINT32)markdownPrefix(firstLine, markerKind, markerLevel) : 0;
    if (!base) {
        kind = markerKind;
        level = markerLevel;
    }
    auto* format = app.fmtBody.get();
    if (kind == BKind::Heading)
        format = app.fmtHead[std::clamp(level, 1, 6) - 1].get();
    else if (kind == BKind::Code)
        format = app.fmtCode.get();

    const bool list = kind == BKind::Bullet || kind == BKind::Ordered;
    // Headings use a single layout so the visible hashes sit on the baseline
    // with the title, and wrapped lines start at the heading's left edge.
    const bool hangingIndent = list || (base && base->quoteDepth > 0 && kind != BKind::Heading);
    auto& edit = layout.edit;
    edit.x = base ? base->x : left + (list ? 20.f : 0.f);
    UINT32 prefix = 0;
    if (hangingIndent && markerEnd) {
        app.dwrite->CreateTextLayout(raw.data(), markerEnd, format, 10000.f, 100.f, edit.prefix.put());
        if (edit.prefix) {
            edit.prefix->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TEXT_METRICS metrics = {};
            edit.prefix->GetMetrics(&metrics);
            const float prefixX = std::max(2.f, edit.x - metrics.widthIncludingTrailingWhitespace);
            const float bodyX = prefixX + metrics.widthIncludingTrailingWhitespace;
            if (bodyX <= app.contentRight() - 40.f) {
                prefix = markerEnd;
                edit.prefixX = prefixX;
                edit.x = bodyX;
                edit.prefix->SetDrawingEffect(app.brMuted.get(), {0, prefix});
            } else edit.prefix.reset();
        }
    }
    std::wstring display = raw.substr(prefix);

    edit.block = index;
    edit.replacesBlock = replaces;
    edit.prefixLength = prefix;
    edit.y = base ? base->y : index < (int)layout.next ? layout.blockY[(size_t)index]
             : layout.contentH - (layout.next ? kMarginBottom : 0.f);
    if (base && kind == BKind::Rule) edit.y -= 9.f;
    if (base && kind == BKind::Code) {
        edit.x += kCodePad;
        if (!original->fenced) edit.y += base->codePadY;
    }
    float textWidth = base ? base->w + base->x - edit.x : width - (edit.x - left);
    if (kind == BKind::Code) textWidth -= kCodePad;
    if (kind == BKind::Rule || kind == BKind::Image) textWidth = width;
    textWidth = std::max(20.f, textWidth);
    app.dwrite->CreateTextLayout(display.data(), (UINT32)display.size(), format,
                                 textWidth, 100000.f, edit.text.put());
    styleMarkdown(app, edit.text.get(), styles, prefix, (UINT32)display.size());
    FLOAT lineHeight = 0.f, baseline = 0.f;
    DWRITE_LINE_SPACING_METHOD method;
    format->GetLineSpacing(&method, &lineHeight, &baseline);
    edit.lineHeight = lineHeight;
    float reservedBottom = edit.y;
    if (base) {
        reservedBottom = base->y + base->h;
        if (kind == BKind::Rule) reservedBottom += gapAfter(*original);
    } else if (index == (int)layout.next && layout.done && layout.next) {
        reservedBottom = layout.contentH;
    }
    if (edit.text) {
        if (base) fitEditorRows(edit.text.get(), reservedBottom - edit.y, textWidth);
        DWRITE_TEXT_METRICS metrics = {};
        edit.text->GetMetrics(&metrics);
        edit.maxScrollX = std::max(0.f, metrics.widthIncludingTrailingWhitespace + 2.f -
                                         (app.contentRight() - 14.f - edit.x));
    }
    edit.h = std::max(lineHeight, layoutHeight(edit.text.get()));
    const float extra = edit.y + edit.h - reservedBottom;
    edit.extra = extra > 0.1f ? extra : 0.f;
    shiftBlocks(layout, index + replaces, edit.extra);
}

void layoutStep(App& app, float untilY, int budgetMs) {
    Layout& L = app.tab().lay;
    if (L.done) return;
    DWORD t0 = GetTickCount();
    float cw = contentW(app);
    float cx = contentX(app);

    const auto& blocks = app.tab().doc.blocks;
    while (L.next < blocks.size()) {
        const Block& b = blocks[L.next];
        Item item = layoutBlock(app, b, L.contentH, cx, cw);
        item.blockIndex = (int)L.next;
        L.contentH = blockEnd(item, b);
        L.blockY[L.next++] = item.y;
        L.items.push_back(std::move(item));
        if (L.contentH >= untilY && app.tab().pendingBlock < (int)L.next) break;
        if ((int)(GetTickCount() - t0) >= budgetMs) break;
    }
    if (L.next == blocks.size()) {
        L.done = true;
        L.contentH += kMarginBottom;
        L.heightFloor = 0.f;
    }
}
