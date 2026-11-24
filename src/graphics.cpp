#include "app.h"
#include "chrome.h"
#include <algorithm>
#include <cmath>

void srcDraw(App& app);

namespace {

ComPtr<IDWriteTextFormat> makeFormat(App& app, const wchar_t* family, float size,
                                     DWRITE_FONT_WEIGHT weight, float lineMul) {
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(app.dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                            DWRITE_FONT_STRETCH_NORMAL, size, L"", f.put())))
        return f;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, size * lineMul, size * lineMul * 0.78f);
    f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    return f;
}

void drawText(App& app, const Item& it) {
    if (!it.tl) return;
    D2D1_POINT_2F pt = {it.x, it.y};
    app.rt->DrawTextLayout(pt, it.tl.get(), app.brText.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void drawMarker(App& app, const Item& it) {
    if (it.marker.empty() || !it.tl) return;

    if (it.marker == L"[]") {
        float s = 11.f;
        float x = it.x - 21.f;
        float y = it.y + it.baseline - 9.f;
        D2D1_RECT_F box = rect(x, y, x + s, y + s);
        app.rt->DrawRoundedRectangle(D2D1_ROUNDED_RECT{box, 2.5f, 2.5f}, app.brMuted.get(), 1.2f);
        if (app.hasTab()) {
            const Tab& t = app.tab();
            if (it.blockIndex >= 0 && (size_t)it.blockIndex < t.doc.blocks.size()) {
                if (t.doc.blocks[(size_t)it.blockIndex].checked) {
                    app.rt->DrawLine(D2D1_POINT_2F{x + 2.5f, y + 5.5f},
                                     D2D1_POINT_2F{x + 4.5f, y + 8.f}, app.brAccent.get(), 1.6f);
                    app.rt->DrawLine(D2D1_POINT_2F{x + 4.5f, y + 8.f},
                                     D2D1_POINT_2F{x + 8.5f, y + 2.5f}, app.brAccent.get(), 1.6f);
                }
            }
        }
        return;
    }

    D2D1_RECT_F rc = rect(it.x - 26.f, it.y + it.baseline - 20.f, it.x - 7.f,
                          it.y + it.baseline + 6.f);
    IDWriteTextFormat* fmt = app.fmtMarkerRight.get();
    app.rt->DrawText(it.marker.c_str(), (UINT32)it.marker.size(), fmt, rc, app.brMuted.get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void drawTable(App& app, const Item& it) {
    if (!it.rows || !it.cols) return;
    float left = it.colX[0];
    float right = it.colX[it.cols - 1] + it.colW[it.cols - 1];
    float bottom = it.rowY[it.rows - 1] + it.rowH[it.rows - 1];

    if (it.headerRows > 0) {
        float hb = it.rowY[it.headerRows - 1] + it.rowH[it.headerRows - 1];
        app.rt->FillRectangle(rect(left, it.y, right, hb), app.brTableHead.get());
    }
    for (int r = 0; r < it.rows; ++r) {
        float ty = it.rowY[r] + it.rowH[r];
        app.rt->FillRectangle(rect(left, ty, right, ty + 1.f), app.brTableLine.get());
    }
    app.rt->FillRectangle(rect(left, it.y, right, it.y + 1.f), app.brTableLine.get());
    for (int c = 0; c <= it.cols; ++c) {
        float x = (c == it.cols) ? right : it.colX[c];
        app.rt->FillRectangle(rect(x, it.y, x + 1.f, bottom), app.brTableLine.get());
    }
    for (int r = 0; r < it.rows; ++r) {
        for (int c = 0; c < it.cols; ++c) {
            auto& tl = it.cells[(size_t)r * (size_t)it.cols + (size_t)c];
            if (!tl) continue;
            D2D1_POINT_2F pt = {it.colX[c] + 10.f, it.rowY[r] + 10.f};
            app.rt->DrawTextLayout(pt, tl.get(), app.brText.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    }
}

void drawCode(App& app, const Item& it) {
    D2D1_ROUNDED_RECT rr;
    rr.rect = rect(it.x, it.y, it.x + it.w, it.y + it.h);
    rr.radiusX = 6.f;
    rr.radiusY = 6.f;
    app.rt->FillRoundedRectangle(rr, app.brCodeBg.get());
    if (it.tl) {
        D2D1_POINT_2F pt = {it.x + 12.f, it.y + 12.f};
        app.rt->DrawTextLayout(pt, it.tl.get(), app.brCodeText.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
    }
}

void drawItem(App& app, const Item& it) {
    switch (it.kind) {
    case BKind::Rule:
        app.rt->FillRectangle(rect(it.x, it.y, it.x + it.w, it.y + 1.f), app.brRule.get());
        return;
    case BKind::Table:
        drawTable(app, it);
        return;
    case BKind::Code:
        drawCode(app, it);
        return;
    case BKind::Image:
        drawImageBlock(app, it, it.imgPath);
        return;
    default:
        if (it.quoteDepth > 0)
            app.rt->FillRectangle(rect(it.x - 12.f, it.y, it.x - 9.f, it.y + it.h),
                                  app.brQuote.get());
        drawText(app, it);
        drawMarker(app, it);
        return;
    }
}

}  // namespace

bool initGraphics(App& app) {
    ID2D1Factory* df = nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &df))) return false;
    app.d2d = ComPtr<ID2D1Factory>(df);

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   (IUnknown**)app.dwrite.putUnk())))
        return false;

    app.fmtBody = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.62f);
    app.fmtQuote = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.62f);
    app.fmtHead[0] = makeFormat(app, L"Segoe UI", 30.f, DWRITE_FONT_WEIGHT_BOLD, 1.30f);
    app.fmtHead[1] = makeFormat(app, L"Segoe UI", 23.f, DWRITE_FONT_WEIGHT_BOLD, 1.35f);
    app.fmtHead[2] = makeFormat(app, L"Segoe UI", 19.f, DWRITE_FONT_WEIGHT_BOLD, 1.40f);
    app.fmtHead[3] = makeFormat(app, L"Segoe UI", 16.5f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtHead[4] = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtHead[5] = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtCode = makeFormat(app, L"Consolas", 13.5f, DWRITE_FONT_WEIGHT_NORMAL, 1.55f);
    app.fmtTable = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_NORMAL, 1.45f);
    app.fmtTableHead = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtMarker = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.6f);
    app.fmtMarkerRight = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.6f);
    if (app.fmtMarkerRight) app.fmtMarkerRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    app.fmtToc = makeFormat(app, L"Segoe UI", 14.f, DWRITE_FONT_WEIGHT_NORMAL, 1.5f);
    if (app.fmtToc) {
        app.fmtToc->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        ComPtr<IDWriteInlineObject> ellipsis;
        DWRITE_TRIMMING tr = {DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        if (SUCCEEDED(app.dwrite->CreateEllipsisTrimmingSign(app.fmtToc.get(), ellipsis.put())))
            app.fmtToc->SetTrimming(&tr, ellipsis.get());
    }
    app.fmtBtn = makeFormat(app, L"Segoe UI", 13.5f, DWRITE_FONT_WEIGHT_NORMAL, 1.4f);
    if (app.fmtBtn) {
        app.fmtBtn->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.fmtBtn->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.fmtBtn->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    app.fmtTab = makeFormat(app, L"Segoe UI", 13.f, DWRITE_FONT_WEIGHT_NORMAL, 1.35f);
    app.fmtTabActive = makeFormat(app, L"Segoe UI", 13.f, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1.35f);
    for (IDWriteTextFormat* f : {app.fmtTab.get(), app.fmtTabActive.get()})
        if (f) f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    app.fmtStatus = makeFormat(app, L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_NORMAL, 1.4f);
    if (app.fmtStatus) app.fmtStatus->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    app.fmtSource = makeFormat(app, L"Consolas", 14.f, DWRITE_FONT_WEIGHT_NORMAL, 1.43f);
    if (app.fmtSource) {
        app.fmtSource->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        app.fmtSource->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 20.f, 15.f);
    }
    app.fmtIcon = makeFormat(app, L"Segoe UI", 12.5f, DWRITE_FONT_WEIGHT_SEMI_BOLD, 1.2f);
    if (app.fmtIcon) {
        app.fmtIcon->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.fmtIcon->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.fmtIcon->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    app.fmtIconBold = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_BOLD, 1.2f);
    if (app.fmtIconBold) {
        app.fmtIconBold->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.fmtIconBold->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.fmtIconBold->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    for (const wchar_t* family : {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"}) {
        ComPtr<IDWriteTextFormat> f;
        if (SUCCEEDED(app.dwrite->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                   10.f, L"", f.put()))) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            app.fmtCaption = std::move(f);
            break;
        }
    }
    app.fmtIconMono = makeFormat(app, L"Consolas", 11.5f, DWRITE_FONT_WEIGHT_NORMAL, 1.2f);
    if (app.fmtIconMono) {
        app.fmtIconMono->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        app.fmtIconMono->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        app.fmtIconMono->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    {
        ComPtr<IDWriteTextFormat> f;
        if (SUCCEEDED(app.dwrite->CreateTextFormat(L"Times New Roman", nullptr,
                                                   DWRITE_FONT_WEIGHT_NORMAL,
                                                   DWRITE_FONT_STYLE_ITALIC,
                                                   DWRITE_FONT_STRETCH_NORMAL, 16.f, L"",
                                                   f.put()))) {
            f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            f->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 18.f, 14.f);
            app.fmtIconItalic = std::move(f);
        }
    }

    app.th = makeTheme(app.themeDark);
    return app.fmtBody && app.fmtCode && app.fmtHead[0];
}

void releaseTarget(App& app) {
    app.rt.reset();
    app.brText.reset();
    app.brMuted.reset();
    app.brLink.reset();
    app.brCodeBg.reset();
    app.brCodeText.reset();
    app.brRule.reset();
    app.brQuote.reset();
    app.brTableHead.reset();
    app.brTableLine.reset();
    app.brThumb.reset();
    app.brPanel.reset();
    app.brAccent.reset();
    app.brSel.reset();
    app.brHilite.reset();
    app.brKw.reset();
    app.brStr.reset();
    app.brCom.reset();
    app.brNum.reset();
}

void createTarget(App& app) {
    if (!app.d2d || !app.hwnd) return;
    RECT rc = {};
    GetClientRect(app.hwnd, &rc);
    UINT pw = (UINT)std::max<LONG>(1, rc.right - rc.left);
    UINT ph = (UINT)std::max<LONG>(1, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtp = {};
    rtp.type = D2D1_RENDER_TARGET_TYPE_SOFTWARE;
    rtp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    rtp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    rtp.dpiX = app.dpi;
    rtp.dpiY = app.dpi;
    rtp.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    rtp.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;

    D2D1_HWND_RENDER_TARGET_PROPERTIES htp = {};
    htp.hwnd = app.hwnd;
    htp.pixelSize.width = pw;
    htp.pixelSize.height = ph;
    htp.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

    if (FAILED(app.d2d->CreateHwndRenderTarget(&rtp, &htp, app.rt.put()))) return;
    app.rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    auto mk = [&](ComPtr<ID2D1SolidColorBrush>& b) {
        app.rt->CreateSolidColorBrush(rgb(0), b.put());
    };
    mk(app.brText);
    mk(app.brMuted);
    mk(app.brLink);
    mk(app.brCodeBg);
    mk(app.brCodeText);
    mk(app.brRule);
    mk(app.brQuote);
    mk(app.brTableHead);
    mk(app.brTableLine);
    mk(app.brThumb);
    mk(app.brPanel);
    mk(app.brAccent);
    mk(app.brSel);
    mk(app.brHilite);
    mk(app.brKw);
    mk(app.brStr);
    mk(app.brCom);
    mk(app.brNum);

    applyTheme(app);
}

void applyTheme(App& app) {
    app.th = makeTheme(app.themeDark);
    if (app.brText) app.brText->SetColor(app.th.text);
    if (app.brMuted) app.brMuted->SetColor(app.th.muted);
    if (app.brLink) app.brLink->SetColor(app.th.link);
    if (app.brCodeBg) app.brCodeBg->SetColor(app.th.codeBg);
    if (app.brCodeText) app.brCodeText->SetColor(app.th.codeText);
    if (app.brRule) app.brRule->SetColor(app.th.rule);
    if (app.brQuote) app.brQuote->SetColor(app.th.quoteBar);
    if (app.brTableHead) app.brTableHead->SetColor(app.th.tableHead);
    if (app.brTableLine) app.brTableLine->SetColor(app.th.tableLine);
    if (app.brThumb) app.brThumb->SetColor(app.th.thumb);
    if (app.brPanel) app.brPanel->SetColor(app.th.panel);
    if (app.brAccent) app.brAccent->SetColor(app.th.link);
    if (app.brSel) app.brSel->SetColor(app.th.selection);
    if (app.brHilite) app.brHilite->SetColor(app.th.highlight);
    if (app.brKw) app.brKw->SetColor(app.th.kw);
    if (app.brStr) app.brStr->SetColor(app.th.str);
    if (app.brCom) app.brCom->SetColor(app.th.com);
    if (app.brNum) app.brNum->SetColor(app.th.num);
}

ScrollGeom scrollGeom(const App& app) {
    ScrollGeom g = {};
    float trackH = app.viewH() - 16.f;
    float contentH = std::max(app.tab().lay.contentH, app.viewH());
    g.visible = app.tab().lay.contentH > app.viewH() + 1.f;
    g.trackW = 8.f;
    g.trackX = app.contentRight() - g.trackW - 4.f;
    g.trackY = app.contentTop() + 8.f;
    g.trackH = trackH;
    float ratio = app.viewH() / contentH;
    g.thumbH = std::max(28.f, trackH * ratio);
    float maxScroll = std::max(1.f, contentH - app.viewH());
    float t = app.tab().scrollY / maxScroll;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    g.thumbY = g.trackY + (trackH - g.thumbH) * t;
    return g;
}

void scrollToThumb(App& app, float thumbY) {
    ScrollGeom g = scrollGeom(app);
    float span = g.trackH - g.thumbH;
    if (span <= 0.f) return;
    float t = (thumbY - g.trackY) / span;
    if (t < 0.f) t = 0.f;
    if (t > 1.f) t = 1.f;
    float contentH = std::max(app.tab().lay.contentH, app.viewH());
    scrollTo(app, t * (contentH - app.viewH()));
}

void scrollTo(App& app, float y) {
    if (y < 0.f) y = 0.f;
    if (app.hasTab() && app.tab().lay.done) {
        float maxY = std::max(0.f, app.tab().lay.contentH - app.viewH());
        if (y > maxY) y = maxY;
    }
    if (app.hasTab()) app.tab().scrollY = y;
    ensureLayoutForView(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void ensureLayoutForView(App& app) {
    if (!app.hasTab() || app.tab().lay.done) return;
    layoutStep(app, app.tab().scrollY + app.viewH() * 1.5f, 8);
    if (!app.hasTab() || !app.tab().lay.done) SetTimer(app.hwnd, 1, 1, nullptr);
}

// ------------------------------------------------------------------ outline

namespace {

const float kTocItemH = 26.f;
const float kTocTopPad = 10.f;

int currentOutlineIndex(const App& app) {
    int cur = -1;
    for (int i = 0; i < (int)app.tab().toc.size(); ++i) {
        size_t b = (size_t)app.tab().toc[i].blockIndex;
        if (b >= app.tab().lay.blockY.size()) break;
        if (app.tab().lay.blockY[b] <= app.tab().scrollY + 14.f) cur = i;
        else break;
    }
    return cur;
}

void clampOutlineScroll(App& app) {
    float panelH = app.viewH() - kTocTopPad * 2.f;
    float total = (float)app.tab().toc.size() * kTocItemH;
    float maxS = std::max(0.f, total - panelH);
    if (app.outlineScroll < 0.f) app.outlineScroll = 0.f;
    if (app.outlineScroll > maxS) app.outlineScroll = maxS;
}

void followOutline(App& app) {
    int cur = currentOutlineIndex(app);
    if (cur < 0) return;
    float panelH = app.viewH() - kTocTopPad * 2.f;
    float top = (float)cur * kTocItemH - app.outlineScroll;
    if (top < 0.f) app.outlineScroll = (float)cur * kTocItemH;
    else if (top + kTocItemH > panelH)
        app.outlineScroll = (float)cur * kTocItemH + kTocItemH - panelH;
    clampOutlineScroll(app);
}

void drawOutline(App& app) {
    float x0 = app.contentRight();
    if (x0 >= app.vw - 1.f) return;
    app.rt->FillRectangle(rect(x0, app.contentTop(), app.vw, app.vh - app.statusH),
                          app.brPanel.get());
    app.rt->FillRectangle(rect(x0, app.contentTop(), x0 + 1.f, app.vh - app.statusH),
                          app.brRule.get());
    app.rt->PushAxisAlignedClip(rect(x0 + 1.f, app.contentTop(), app.vw, app.vh - app.statusH),
                                D2D1_ANTIALIAS_MODE_ALIASED);

    if (app.tab().toc.empty()) {
        app.rt->DrawText(L"\u6ca1\u6709\u6807\u9898", 4, app.fmtToc.get(),
                         rect(x0 + 14.f, app.contentTop() + 14.f, app.vw - 14.f,
                              app.contentTop() + 60.f),
                         app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    followOutline(app);
    int cur = currentOutlineIndex(app);
    for (int i = 0; i < (int)app.tab().toc.size(); ++i) {
        float top = 0.f, bottom = 0.f;
        outlineEntryRect(app, i, &top, &bottom);
        if (bottom < app.contentTop()) continue;
        if (top > app.vh) break;
        float indent = 12.f + (float)(app.tab().toc[i].level - 1) * 12.f;
        D2D1_RECT_F r = rect(x0 + indent, top, app.vw - 12.f, bottom);
        if (i == cur)
            app.rt->FillRoundedRectangle(
                D2D1_ROUNDED_RECT{rect(x0 + 6.f, top + 2.f, app.vw - 6.f, bottom - 2.f), 4.f, 4.f},
                app.brTableHead.get());
        app.rt->DrawText(app.tab().toc[i].text.c_str(), (UINT32)app.tab().toc[i].text.size(),
                         app.fmtToc.get(), r,
                         i == cur ? app.brText.get() : app.brMuted.get(),
                         D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    app.rt->PopAxisAlignedClip();
}

}  // namespace


void outlineEntryRect(const App& app, int index, float* top, float* bottom) {
    float y = app.contentTop() + kTocTopPad + (float)index * kTocItemH - app.outlineScroll;
    *top = y;
    *bottom = y + kTocItemH;
}

int outlineHit(const App& app, float x, float y) {
    if (!app.showOutline || !app.hasTab()) return -1;
    if (x < app.contentRight() || y < app.contentTop()) return -1;
    float rel = y - app.contentTop() - kTocTopPad + app.outlineScroll;
    if (rel < 0.f) return -1;
    int i = (int)(rel / kTocItemH);
    if (i < 0 || i >= (int)app.tab().toc.size()) return -1;
    return i;
}

void scrollOutline(App& app, float dy) {
    if (!app.showOutline) return;
    float panelH = app.viewH() - 20.f;
    float total = (float)app.tab().toc.size() * 26.f;
    float maxS = std::max(0.f, total - panelH);
    app.outlineScroll += dy;
    if (app.outlineScroll < 0.f) app.outlineScroll = 0.f;
    if (app.outlineScroll > maxS) app.outlineScroll = maxS;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void scrollToBlock(App& app, int blockIndex) {
    if (!app.hasTab()) return;
    if (blockIndex < 0 || (size_t)blockIndex >= app.tab().lay.blockY.size()) return;
    scrollTo(app, std::max(0.f, app.tab().lay.blockY[(size_t)blockIndex] - 6.f));
}

void toggleOutline(App& app) {
    if (!app.hasTab()) return;
    int anchor = blockAtScroll(app);
    app.showOutline = !app.showOutline;
    if (!app.showOutline) app.outlineScroll = 0.f;
    layoutReset(app);
    ensureLayoutForView(app);
    if (anchor >= 0 && (size_t)anchor < app.tab().lay.blockY.size())
        scrollTo(app, app.tab().lay.blockY[(size_t)anchor]);
    else
        scrollTo(app, 0.f);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ paint

void paint(App& app) {
    if (!app.d2d) return;
    if (!app.rt) {
        createTarget(app);
        if (!app.rt) return;
    }
    app.rt->BeginDraw();
    D2D1_MATRIX_3X2_F identity = {1.f, 0.f, 0.f, 1.f, 0.f, 0.f};
    app.rt->SetTransform(identity);
    app.rt->Clear(app.th.bg);

    if (!app.hasTab()) {
        drawChrome(app);
        HRESULT hr = app.rt->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) { releaseTarget(app); createTarget(app); }
        return;
    }

    Tab& t = app.tab();
    float cRight = app.contentRight();

    if (t.mode == Mode::Source) {
        srcDraw(app);
    } else {
        app.rt->PushAxisAlignedClip(rect(0.f, app.contentTop(), cRight, app.vh - app.statusH),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        D2D1_MATRIX_3X2_F tr = {1.f, 0.f, 0.f, 1.f, 0.f, app.contentTop() - t.scrollY};
        app.rt->SetTransform(tr);

        if (t.lay.items.empty()) {
            app.rt->SetTransform(identity);
            const wchar_t* hint = L"\u6b64\u9875\u4e3a\u7a7a\uff0c\u6309 Ctrl+E \u5207\u5230\u6e90\u7801";
            ComPtr<IDWriteTextLayout> tl;
            DWRITE_TEXT_METRICS m = {};
            if (SUCCEEDED(app.dwrite->CreateTextLayout(hint, (UINT32)wcslen(hint),
                                                       app.fmtBody.get(), cRight, 100.f, tl.put()))) {
                tl->GetMetrics(&m);
                D2D1_POINT_2F pt = {std::floor((cRight - m.width) * 0.5f),
                                    app.contentTop() + std::floor((app.viewH() - m.height) * 0.5f)};
                app.rt->DrawTextLayout(pt, tl.get(), app.brMuted.get(),
                                       D2D1_DRAW_TEXT_OPTIONS_NONE);
            }
        } else {
            for (const auto& it : t.lay.items) {
                if (it.y + it.h < t.scrollY - 200.f) continue;
                if (it.y > t.scrollY + app.viewH() + 200.f) break;
                drawItem(app, it);
            }
            app.rt->SetTransform(identity);
        }
        app.rt->PopAxisAlignedClip();

        ScrollGeom g = scrollGeom(app);
        if (g.visible && app.brThumb) {
            D2D1_ROUNDED_RECT rr;
            rr.rect = rect(g.trackX, g.thumbY, g.trackX + g.trackW, g.thumbY + g.thumbH);
            rr.radiusX = 4.f;
            rr.radiusY = 4.f;
            app.rt->FillRoundedRectangle(rr, app.brThumb.get());
        }
        if (app.showOutline) drawOutline(app);
    }

    drawChrome(app);

    HRESULT hr = app.rt->EndDraw();
    static bool firstPaint = true;
    if (firstPaint) {
        firstPaint = false;
        trace("first frame presented");
    }
    if (hr == D2DERR_RECREATE_TARGET) {
        releaseTarget(app);
        createTarget(app);
        layoutReset(app);
        ensureLayoutForView(app);
    }
}
