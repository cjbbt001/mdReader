#include "app.h"
#include <algorithm>
#include <cmath>

namespace {

const float kLineH = 20.f;
const float kTextPadX = 10.f;

}  // namespace

float srcLineH() { return kLineH; }

float srcGutterW(const App& app) {
    int digits = 1;
    if (app.hasTab()) {
        int n = app.tab().ed.lineCount();
        while (n >= 10) { n /= 10; ++digits; }
    }
    return 16.f + digits * 8.f + 10.f;
}

float textWidthOf(App& app, IDWriteTextFormat* fmt, const std::wstring& s) {
    if (s.empty()) return 0.f;
    ComPtr<IDWriteTextLayout> tl;
    if (FAILED(app.dwrite->CreateTextLayout(s.c_str(), (UINT32)s.size(), fmt, 100000.f, 200.f,
                                            tl.put())))
        return 0.f;
    DWRITE_TEXT_METRICS m = {};
    tl->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

float srcTextX(const App& app) { return srcGutterW(app) + kTextPadX - app.tab().ed.scrollX; }

float srcXForCol(App& app, int line, int col) {
    const Editor& ed = app.tab().ed;
    if (line < 0 || line >= ed.lineCount()) return 0.f;
    const std::wstring& l = ed.line(line);
    if (col <= 0) return srcTextX(app);
    if (col > (int)l.size()) col = (int)l.size();
    return srcTextX(app) + textWidthOf(app, app.fmtSource.get(), l.substr(0, (size_t)col));
}

int srcColForX(App& app, int line, float x) {
    const Editor& ed = app.tab().ed;
    if (line < 0 || line >= ed.lineCount()) return 0;
    const std::wstring& l = ed.line(line);
    if (l.empty()) return 0;
    float base = srcTextX(app);
    if (x <= base) return 0;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(app.dwrite->CreateTextLayout(l.data(), (UINT32)l.size(), app.fmtSource.get(),
                                            100000.f, kLineH, layout.put()))) return 0;
    DWRITE_HIT_TEST_METRICS hit = {};
    BOOL trailing = FALSE, inside = FALSE;
    if (FAILED(layout->HitTestPoint(x - base, kLineH / 2.f, &trailing, &inside, &hit))) return 0;
    return (int)(hit.textPosition + (trailing ? hit.length : 0));
}

void edEnsureCaretVisible(App& app) {
    Editor& ed = app.tab().ed;
    int visible = (int)std::floor(app.viewH() / kLineH);
    if (ed.caret.line < ed.firstLine) ed.firstLine = ed.caret.line;
    if (ed.caret.line >= ed.firstLine + visible - 1)
        ed.firstLine = std::max(0, ed.caret.line - visible + 2);
    if (ed.firstLine < 0) ed.firstLine = 0;

    float cx = srcXForCol(app, ed.caret.line, ed.caret.col);
    float left = srcGutterW(app);
    float right = app.contentRight() - 16.f;
    if (cx < left + 4.f) {
        ed.scrollX = std::max(0.f, ed.scrollX - (left + 4.f - cx));
        if (cx - srcGutterW(app) - kTextPadX < 0.f) {
            float need = textWidthOf(app, app.fmtSource.get(), ed.line(ed.caret.line).substr(0, (size_t)ed.caret.col));
            ed.scrollX = std::max(0.f, need - 20.f);
        }
    } else if (cx > right) {
        ed.scrollX += (cx - right) + 24.f;
    }
}

void edMoveCaretLine(App& app, int delta) {
    Editor& ed = app.tab().ed;
    ed.firstLine += delta;
    if (ed.firstLine > ed.lineCount() - 1) ed.firstLine = std::max(0, ed.lineCount() - 1);
    if (ed.firstLine < 0) ed.firstLine = 0;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void srcDraw(App& app) {
    if (!app.hasTab()) return;
    Tab& t = app.tab();
    Editor& ed = t.ed;

    D2D1_MATRIX_3X2_F identity = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
    app.rt->SetTransform(identity);

    float top = app.contentTop();
    float gutter = srcGutterW(app);
    app.rt->FillRectangle(rect(0.f, top, gutter, app.vh - app.statusH), app.brPanel.get());
    app.rt->FillRectangle(rect(gutter, top, gutter + 1.f, app.vh - app.statusH), app.brRule.get());

    app.rt->PushAxisAlignedClip(rect(0.f, top, app.contentRight(), app.vh - app.statusH),
                                D2D1_ANTIALIAS_MODE_ALIASED);

    int visible = (int)std::floor(app.viewH() / kLineH) + 1;
    Pos selA, selB;
    ed.selRange(&selA, &selB);
    bool hasSel = ed.hasSelection();

    for (int i = 0; i < visible; ++i) {
        int ln = ed.firstLine + i;
        if (ln >= ed.lineCount()) break;
        float y = top + i * kLineH;

        if (ln == ed.caret.line) {
            ComPtr<ID2D1SolidColorBrush> lb;
            app.rt->CreateSolidColorBrush(t.mode == Mode::Source ? app.th.tableHead : app.th.panel, lb.put());
            app.rt->FillRectangle(rect(gutter + 1.f, y, app.contentRight(), y + kLineH), lb.get());
        }

        wchar_t num[16];
        swprintf(num, 16, L"%d", ln + 1);
        app.rt->DrawText(num, (UINT32)wcslen(num), app.fmtStatus.get(),
                         rect(6.f, y, gutter - 6.f, y + kLineH),
                         ln == ed.caret.line ? app.brMuted.get() : app.brRule.get(),
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);

        const std::wstring& line = ed.line(ln);
        if (hasSel && ln >= selA.line && ln <= selB.line) {
            int c0 = ln == selA.line ? selA.col : 0;
            int c1 = ln == selB.line ? selB.col : (int)line.size();
            if (c1 > c0 || (ln < selB.line)) {
                float x0 = srcTextX(app) + textWidthOf(app, app.fmtSource.get(), line.substr(0, (size_t)c0));
                float x1 = srcXForCol(app, ln, c1) + (c1 >= (int)line.size() ? 6.f : 0.f);
                app.rt->FillRectangle(rect(std::max(gutter + 1.f, x0), y,
                                           std::max(gutter + 1.f, x1), y + kLineH), app.brSel.get());
            }
        }
        if (!line.empty()) {
            app.rt->PushAxisAlignedClip(rect(gutter + 1.f, y, app.contentRight(), y + kLineH),
                                        D2D1_ANTIALIAS_MODE_ALIASED);
            app.rt->DrawText(line.c_str(), (UINT32)line.size(), app.fmtSource.get(),
                             rect(srcTextX(app), y, srcTextX(app) + 20000.f, y + kLineH),
                             app.brText.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            app.rt->PopAxisAlignedClip();
        }
    }

    if (app.caretOn && !app.findOpen) {
        float cx = srcXForCol(app, ed.caret.line, ed.caret.col);
        float cy = top + (ed.caret.line - ed.firstLine) * kLineH;
        app.rt->FillRectangle(rect(cx, cy + 2.f, cx + 1.5f, cy + kLineH - 2.f), app.brText.get());
    }

    app.rt->PopAxisAlignedClip();
}

void srcHitTestPoint(App& app, float x, float y, Pos* p) {
    Editor& ed = app.tab().ed;
    int line = ed.firstLine + (int)std::floor((y - app.contentTop()) / kLineH);
    if (line < 0) line = 0;
    if (line >= ed.lineCount()) line = ed.lineCount() - 1;
    p->line = line;
    p->col = srcColForX(app, line, x);
}

float srcCaretX(App& app) { return srcXForCol(app, app.tab().ed.caret.line, app.tab().ed.caret.col); }
float srcCaretY(App& app) {
    return app.contentTop() + (app.tab().ed.caret.line - app.tab().ed.firstLine) * kLineH;
}
float srcLineHeight() { return kLineH; }
