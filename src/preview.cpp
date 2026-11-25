#include "app.h"
#include <algorithm>
#include <cmath>
#include <shellapi.h>
#include <shlwapi.h>
#include <unordered_set>

namespace {

size_t editOffset(const Tab& tab, Pos pos) {
    if (pos.line < tab.editFirst) return 0;
    size_t offset = 0;
    for (int line = tab.editFirst; line <= tab.editLast; ++line) {
        if (line == pos.line) return offset + pos.col;
        offset += tab.ed.line(line).size();
        if (line < tab.editLast) ++offset;
    }
    return offset;
}

Pos editPosition(const Tab& tab, size_t offset) {
    for (int line = tab.editFirst; line <= tab.editLast; ++line) {
        size_t length = tab.ed.line(line).size();
        if (offset <= length || line == tab.editLast)
            return {line, (int)std::min(length, offset)};
        offset -= length + 1;
    }
    return tab.ed.caret;
}

const Item* itemAt(const App& app, float y) {
    const auto& items = app.tab().lay.items;
    auto it = std::lower_bound(items.begin(), items.end(), y,
                               [](const Item& item, float target) { return item.y + item.h <= target; });
    return it != items.end() && it->y <= y ? &*it : nullptr;
}

const Item* topItem(const Tab& tab) {
    auto it = std::upper_bound(tab.lay.items.begin(), tab.lay.items.end(), tab.scrollY,
                               [](float y, const Item& item) { return y < item.y; });
    return it == tab.lay.items.begin() ? nullptr : &*std::prev(it);
}

int topLine(const Tab& tab) {
    if (tab.pendingBlock >= 0) return tab.doc.blocks[(size_t)tab.pendingBlock].firstLine;
    const Item* top = topItem(tab);
    if (!top) return 0;
    return tab.doc.blocks[(size_t)top->blockIndex].firstLine;
}

void anchorLine(App& app, int line, float offset) {
    Tab& tab = app.tab();
    if (!tab.doc.blocks.empty())
        scrollToBlock(app, std::min(blockForLine(tab.doc, line), (int)tab.doc.blocks.size() - 1), offset);
    else {
        tab.scrollY = 0.f;
        ensureLayoutForView(app);
    }
}

void setEditRange(Tab& tab) {
    const int line = tab.ed.caret.line;
    tab.editFirst = tab.editLast = line;
    int index = blockForLine(tab.doc, line);
    if (index < (int)tab.doc.blocks.size()) {
        const Block& block = tab.doc.blocks[(size_t)index];
        if (block.firstLine <= line) {
            tab.editFirst = block.firstLine;
            tab.editLast = std::min(block.lastLine, tab.ed.lineCount() - 1);
        }
    }
}

void revealEditor(App& app, bool scrollVertically) {
    Tab& tab = app.tab();
    tab.revealEditor = scrollVertically;
    ensureLayoutForView(app);
    revealPreviewCaret(app, scrollVertically);
    app.caretOn = true;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void refreshDoc(App& app) {
    Doc doc;
    parseText(app.tab().ed.text(), doc);
    updateDocumentLayout(app, std::move(doc));
    buildToc(app.tab().doc, app.tab().toc);
    app.tab().previewStale = false;
}

}  // namespace

int blockForLine(const Doc& doc, int line) {
    auto it = std::lower_bound(doc.blocks.begin(), doc.blocks.end(), line,
                               [](const Block& block, int target) { return block.lastLine < target; });
    return (int)(it - doc.blocks.begin());
}

void syncEditorToPreview(App& app) {
    Tab& tab = app.tab();
    const int line = tab.ed.firstLine;
    if (tab.previewStale) {
        parseText(tab.ed.text(), tab.doc);
        buildToc(tab.doc, tab.toc);
        tab.previewStale = false;
        tab.pendingBlock = -1;
        layoutReset(app);
    }
    anchorLine(app, line, -6.f);
}

void finishPreviewEdit(App& app) {
    Tab& tab = app.tab();
    if (tab.editFirst < 0) return;
    clearPreviewLayout(app);
    tab.editFirst = tab.editLast = -1;
    tab.revealEditor = false;
    tab.ed.moveTo(tab.ed.caret, false);
    if (tab.previewStale) refreshDoc(app);
    ensureLayoutForView(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

namespace {

void focusPreviewEdit(App& app, Pos pos, bool extend, bool scrollVertically) {
    Tab& tab = app.tab();
    if (tab.editFirst >= 0 && (pos.line < tab.editFirst || pos.line > tab.editLast)) {
        Pos anchor = tab.ed.anchor;
        finishPreviewEdit(app);
        if (extend) tab.ed.anchor = anchor;
    }
    tab.ed.moveTo(pos, extend);
    if (tab.editFirst < 0) {
        setEditRange(tab);
        layoutPreviewEditor(app);
    }
    revealEditor(app, scrollVertically);
}

}  // namespace

void startPreviewEdit(App& app, Pos pos, bool extend) {
    focusPreviewEdit(app, pos, extend, true);
}

void updatePreviewEditor(App& app, bool changed) {
    Tab& tab = app.tab();
    if (changed) {
        tab.dirty = true;
        tab.previewStale = true;
        app.matches.clear();
        app.findIndex = -1;
        app.findCount = 0;
    }
    if (tab.mode == Mode::Source) {
        edEnsureCaretVisible(app);
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return;
    }

    const int line = tab.ed.caret.line;
    if (changed) {
        const int previousFirst = tab.editFirst;
        const float scrollX = tab.lay.edit.scrollX;
        refreshDoc(app);
        setEditRange(tab);
        layoutPreviewEditor(app);
        if (previousFirst == tab.editFirst)
            tab.lay.edit.scrollX = std::min(scrollX, tab.lay.edit.maxScrollX);
    } else if (line < tab.editFirst || line > tab.editLast) {
        startPreviewEdit(app, tab.ed.caret, true);
        return;
    }
    revealEditor(app, true);
}

bool previewCaretPoint(App& app, float& x, float& y, float& height) {
    const Tab& tab = app.tab();
    const PreviewEdit& edit = tab.lay.edit;
    if (edit.block < 0) return false;
    UINT32 offset = (UINT32)editOffset(tab, tab.ed.caret);
    const bool prefix = offset < edit.prefixLength && edit.prefix;
    const auto& layout = prefix ? edit.prefix : edit.text;
    DWRITE_HIT_TEST_METRICS hit = {};
    x = y = 0.f;
    if (layout)
        layout->HitTestTextPosition(prefix ? offset : offset - edit.prefixLength, FALSE, &x, &y, &hit);
    x += prefix ? edit.prefixX : edit.x - edit.scrollX;
    // Hit testing reports glyph bounds, which differ across fallback fonts.
    // Place the caret on the uniform editor row even when its first glyph changes.
    if (edit.lineHeight > 0.f) y = std::round(y / edit.lineHeight) * edit.lineHeight;
    y += edit.y + app.contentTop() - tab.scrollY;
    height = edit.lineHeight;
    return true;
}

void revealPreviewCaret(App& app, bool scrollVertically) {
    Tab& tab = app.tab();
    if (scrollVertically && !tab.revealEditor) return;
    float x, y, height;
    if (!previewCaretPoint(app, x, y, height)) return;
    PreviewEdit& edit = tab.lay.edit;
    if (edit.maxScrollX > 0.f) {
        const float right = app.contentRight() - 16.f;
        if (editOffset(tab, tab.ed.caret) < edit.prefixLength) edit.scrollX = 0.f;
        else if (x < edit.x) edit.scrollX = std::max(0.f, edit.scrollX + x - edit.x);
        else if (x > right) edit.scrollX = std::min(edit.maxScrollX, edit.scrollX + x - right);
    }
    if (!scrollVertically) return;
    const float oldY = tab.scrollY;
    float desiredY = oldY;
    if (y < app.contentTop()) desiredY += y - app.contentTop() - 10.f;
    else if (y + height > app.vh - app.statusH)
        desiredY += y + height - (app.vh - app.statusH) + 10.f;
    const float maxY = std::max(0.f, tab.lay.contentH - app.viewH());
    tab.scrollY = std::clamp(desiredY, 0.f, maxY);
    tab.revealEditor = !tab.lay.done && desiredY > maxY;
}

void drawPreviewEditor(App& app) {
    const Tab& tab = app.tab();
    const PreviewEdit& edit = tab.lay.edit;
    if (edit.block < 0 || edit.y + edit.h < tab.scrollY ||
        edit.y > tab.scrollY + app.viewH()) return;
    Pos start, end;
    tab.ed.selRange(&start, &end);
    const UINT32 first = (UINT32)editOffset(tab, start);
    const UINT32 last = (UINT32)editOffset(tab, end);
    if (edit.replacesBlock && tab.lay.items[(size_t)edit.block].kind == BKind::Image)
        app.rt->FillRectangle(rect(edit.x, edit.y, app.contentRight() - 8.f, edit.y + edit.h),
                              app.brPanel.get());

    auto draw = [&](IDWriteTextLayout* layout, float x, UINT32 offset, UINT32 limit) {
        if (!layout) return;
        const UINT32 from = std::max(first, offset);
        const UINT32 to = std::min(last, limit);
        if (to > from) {
            UINT32 count = 0;
            layout->HitTestTextRange(from - offset, to - from, x, edit.y, nullptr, 0, &count);
            std::vector<DWRITE_HIT_TEST_METRICS> hits(count);
            if (count && SUCCEEDED(layout->HitTestTextRange(from - offset, to - from, x, edit.y,
                                                           hits.data(), count, &count))) {
                for (const auto& h : hits)
                    app.rt->FillRectangle(rect(h.left, h.top, h.left + h.width, h.top + h.height),
                                          app.brSel.get());
            }
        }
        app.rt->DrawTextLayout({x, edit.y}, layout, app.brText.get());
    };
    draw(edit.prefix.get(), edit.prefixX, 0, edit.prefixLength);
    app.rt->PushAxisAlignedClip(rect(edit.x, edit.y, app.contentRight() - 12.f, edit.y + edit.h),
                                D2D1_ANTIALIAS_MODE_ALIASED);
    draw(edit.text.get(), edit.x - edit.scrollX, edit.prefixLength, UINT32_MAX);
    app.rt->PopAxisAlignedClip();

    float x, y, height;
    if (app.caretOn && !app.findOpen && GetFocus() == app.hwnd &&
        previewCaretPoint(app, x, y, height) && x < app.contentRight() - 12.f &&
        (x >= edit.x || editOffset(tab, tab.ed.caret) < edit.prefixLength)) {
        y += tab.scrollY - app.contentTop();
        app.rt->FillRectangle(rect(x, y + 1.f, x + 1.5f, y + height - 1.f), app.brText.get());
    }
}

bool previewHitEditor(App& app, float x, float y, Pos& pos) {
    const Tab& tab = app.tab();
    const PreviewEdit& edit = tab.lay.edit;
    const float localY = y - app.contentTop() + tab.scrollY;
    if (edit.block < 0 || localY < edit.y || localY > edit.y + edit.h) return false;
    const bool prefix = edit.prefix && x < edit.x && localY < edit.y + edit.lineHeight;
    const auto& layout = prefix ? edit.prefix : edit.text;
    DWRITE_HIT_TEST_METRICS hit = {};
    BOOL trailing = FALSE, inside = FALSE;
    if (layout)
        layout->HitTestPoint(x - (prefix ? edit.prefixX : edit.x - edit.scrollX), localY - edit.y,
                             &trailing, &inside, &hit);
    pos = editPosition(tab, (prefix ? 0 : edit.prefixLength) +
                            hit.textPosition + (trailing ? hit.length : 0));
    return true;
}

bool previewMouseDown(App& app, float x, float y, bool extend) {
    Tab& tab = app.tab();
    Pos pos;
    if (previewHitEditor(app, x, y, pos)) {
        tab.ed.moveTo(pos, extend);
        app.caretOn = true;
        InvalidateRect(app.hwnd, nullptr, FALSE);
        return true;
    }

    const float localY = y - app.contentTop() + tab.scrollY;
    const Item* item = itemAt(app, localY);
    if (!item) {
        if (tab.lay.done && (tab.lay.items.empty() ||
            localY > tab.lay.items.back().y + tab.lay.items.back().h) &&
            x >= 20.f && x < app.contentRight() - 20.f) {
            pos.line = tab.ed.lineCount() - 1;
            pos.col = (int)tab.ed.line(pos.line).size();
        } else {
            finishPreviewEdit(app);
            return false;
        }
    } else {
        if (x < item->x - 24.f || x > item->x + item->w) {
            finishPreviewEdit(app);
            return false;
        }
        const Block& block = tab.doc.blocks[(size_t)item->blockIndex];
        pos.line = block.firstLine;
        DWRITE_HIT_TEST_METRICS hit = {};
        BOOL trailing = FALSE, inside = FALSE;
        if (item->tl && item->kind != BKind::Code &&
            SUCCEEDED(item->tl->HitTestPoint(x - item->x, localY - item->y, &trailing, &inside, &hit))) {
            size_t offset = hit.textPosition + (trailing ? hit.length : 0);
            // Map visible text back through the Markdown markers when entering a block.
            for (const auto& run : block.runs) {
                for (wchar_t ch : run.text) {
                    bool found = false;
                    for (int line = pos.line; line <= block.lastLine; ++line) {
                        size_t at = tab.ed.line(line).find(ch, line == pos.line ? pos.col : 0);
                        if (at == std::wstring::npos) continue;
                        pos = {line, (int)at};
                        found = true;
                        break;
                    }
                    if (offset == 0) {
                        focusPreviewEdit(app, pos, extend, false);
                        return true;
                    }
                    --offset;
                    if (found) ++pos.col;
                }
            }
        }
    }
    focusPreviewEdit(app, pos, extend, false);
    return true;
}

namespace {

int linkAt(const std::vector<Run>& runs, IDWriteTextLayout* layout, float x, float y) {
    DWRITE_HIT_TEST_METRICS hit = {};
    BOOL trailing = FALSE, inside = FALSE;
    if (!layout || FAILED(layout->HitTestPoint(x, y, &trailing, &inside, &hit)) || !inside) return -1;
    size_t offset = hit.textPosition;
    for (const auto& run : runs) {
        if (offset < run.text.size()) return run.link;
        offset -= run.text.size();
    }
    return -1;
}

std::wstring unescapeUrl(std::wstring value) {
    UrlUnescapeW(value.data(), nullptr, nullptr, URL_UNESCAPE_INPLACE | URL_UNESCAPE_AS_UTF8);
    value.resize(wcslen(value.c_str()));
    return value;
}

std::wstring headingSlug(const std::wstring& text) {
    std::wstring slug;
    for (wchar_t c : text) {
        if (iswspace(c)) slug += L'-';
        else if (iswalnum(c) || c == L'-' || c == L'_' ||
                 (c >= 0x80 && !iswpunct(c))) slug += (wchar_t)towlower(c);
    }
    return slug;
}

}  // namespace

int previewLinkAt(App& app, float x, float y) {
    if (!app.hasTab() || app.tab().mode != Mode::Preview || app.findOpen ||
        x >= app.contentRight() || y < app.contentTop() || y >= app.vh - app.statusH) return -1;
    const auto& tab = app.tab();
    const float localY = y - app.contentTop() + tab.scrollY;
    const Item* item = itemAt(app, localY);
    if (!item || (tab.lay.edit.replacesBlock && tab.lay.edit.block == item->blockIndex)) return -1;
    const Block& block = tab.doc.blocks[(size_t)item->blockIndex];
    if (item->kind == BKind::Table) {
        for (int r = 0; r < item->rows; ++r) {
            if (localY < item->rowY[r] || localY >= item->rowY[r] + item->rowH[r]) continue;
            for (int c = 0; c < (int)block.cells[(size_t)r].size(); ++c) {
                if (x >= item->colX[c] && x < item->colX[c] + item->colW[c])
                    return linkAt(block.cells[r][c], item->cells[(size_t)r * item->cols + c].get(),
                                  x - item->colX[c] - 10.f, localY - item->rowY[r] - 10.f);
            }
        }
        return -1;
    }
    return linkAt(block.runs, item->tl.get(), x - item->x, localY - item->y);
}

void openPreviewLink(App& app, int id) {
    std::wstring target = app.tab().doc.links[(size_t)id];
    finishPreviewEdit(app);
    if (_wcsnicmp(target.c_str(), L"https://", 8) == 0 ||
        _wcsnicmp(target.c_str(), L"http://", 7) == 0) {
        ShellExecuteW(app.hwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    size_t hash = target.find(L'#');
    std::wstring fragment = hash == std::wstring::npos ? L"" : unescapeUrl(target.substr(hash + 1));
    std::wstring path = unescapeUrl(target.substr(0, hash));
    if (!path.empty()) {
        if (path.find(L':') != std::wstring::npos || path.front() == L'/' || path.front() == L'\\')
            return;
        const wchar_t* ext = PathFindExtensionW(path.c_str());
        if (_wcsicmp(ext, L".md") && _wcsicmp(ext, L".markdown") &&
            _wcsicmp(ext, L".mdown") && _wcsicmp(ext, L".mkd")) return;
        const std::wstring base = app.tab().path;
        if (base.empty() || !openPath(app, base.substr(0, base.find_last_of(L"\\/") + 1) + path)) return;
        if (app.tab().mode == Mode::Source) switchMode(app, Mode::Preview);
    }
    if (fragment.empty()) return;
    std::unordered_set<std::wstring> used;
    for (const auto& heading : app.tab().toc) {
        const std::wstring base = headingSlug(heading.text);
        std::wstring slug = base;
        for (int suffix = 1; !used.insert(slug).second; ++suffix)
            slug = base + L"-" + std::to_wstring(suffix);
        if (_wcsicmp(fragment.c_str(), slug.c_str()) == 0 ||
            _wcsicmp(fragment.c_str(), heading.text.c_str()) == 0) {
            scrollToBlock(app, heading.blockIndex);
            return;
        }
    }
}

void rememberReadingPosition(App& app, const Tab& tab) {
    if (tab.path.empty()) return;
    const int line = tab.mode == Mode::Source ? tab.ed.firstLine : topLine(tab);
    float offset = 0.f;
    if (tab.mode == Mode::Preview) {
        if (tab.pendingBlock >= 0) offset = tab.pendingOffset;
        else if (const Item* top = topItem(tab)) offset = tab.scrollY - top->y;
    }
    auto& positions = app.readingPositions;
    auto found = std::find_if(positions.begin(), positions.end(),
                              [&](const auto& pos) { return _wcsicmp(pos.path.c_str(), tab.path.c_str()) == 0; });
    if (found != positions.end()) positions.erase(found);
    positions.push_back({tab.path, line, offset});
    if (positions.size() > 100) positions.erase(positions.begin());
}

void restoreReadingPosition(App& app) {
    const Tab& tab = app.tab();
    if (tab.path.empty()) return;
    for (const auto& pos : app.readingPositions) {
        if (_wcsicmp(pos.path.c_str(), tab.path.c_str()) != 0) continue;
        anchorLine(app, std::min(pos.line, tab.ed.lineCount() - 1), pos.offset);
        break;
    }
}

void reloadTabFromDisk(App& app, int index, const std::wstring& text) {
    Tab& tab = app.tabs[(size_t)index];
    const bool active = app.active == index;
    const bool editing = tab.editFirst >= 0;
    const Pos caret = tab.ed.caret;
    const int sourceTop = tab.ed.firstLine;
    const float sourceX = tab.ed.scrollX;
    const Item* top = topItem(tab);
    const int line = topLine(tab);
    const float offset = tab.pendingBlock >= 0 ? tab.pendingOffset : top ? tab.scrollY - top->y : 0.f;

    tab.ed.setText(text);
    tab.ed.moveTo(caret, false);
    tab.ed.firstLine = std::min(sourceTop, tab.ed.lineCount() - 1);
    tab.ed.scrollX = sourceX;
    parseText(text, tab.doc);
    buildToc(tab.doc, tab.toc);
    tab.previewStale = false;
    tab.editFirst = tab.editLast = -1;
    tab.revealEditor = false;
    tab.scrollY = 0.f;
    const bool hasScrollAnchor = tab.pendingBlock >= 0 || top;
    tab.pendingBlock = hasScrollAnchor ?
        std::min(blockForLine(tab.doc, line), (int)tab.doc.blocks.size() - 1) : -1;
    tab.pendingOffset = offset;
    if (active) {
        layoutReset(app);
        if (editing) setEditRange(tab);
        ensureLayoutForView(app);
        if (app.findOpen) findRefresh(app);
        InvalidateRect(app.hwnd, nullptr, FALSE);
    } else {
        tab.lay = {};
        tab.layoutWidth = -1.f;
    }
}
