#include "app.h"
#include <algorithm>
#include <cmath>

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
    ID2D1Brush* brush = app.brText.get();
    if (it.kind == BKind::Heading) brush = app.brText.get();
    D2D1_POINT_2F pt = {it.x, it.y};
    app.rt->DrawTextLayout(pt, it.tl.get(), brush, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void drawMarker(App& app, const Item& it) {
    if (it.marker.empty() || !it.tl) return;
    DWRITE_TEXT_METRICS m = {};
    if (FAILED(it.tl->GetMetrics(&m))) return;
    D2D1_RECT_F rc = rect(it.x - 30.f, it.y - 1.f, it.x - 4.f, it.y + m.height);
    IDWriteTextFormat* fmt = it.markerRight ? app.fmtMarkerRight.get() : app.fmtMarker.get();
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

void drawItem(App& app, const Item& it) {
    switch (it.kind) {
    case BKind::Rule:
        app.rt->FillRectangle(rect(it.x, it.y, it.x + it.w, it.y + 1.f), app.brRule.get());
        return;
    case BKind::Table:
        drawTable(app, it);
        return;
    case BKind::Code: {
        D2D1_ROUNDED_RECT rr;
        rr.rect = rect(it.x, it.y, it.x + it.w, it.y + it.h);
        rr.radiusX = 6.f;
        rr.radiusY = 6.f;
        app.rt->FillRoundedRectangle(rr, app.brCodeBg.get());
        if (it.tl) {
            D2D1_POINT_2F pt = {it.x + 12.f, it.y + 12.f};
            app.rt->DrawTextLayout(pt, it.tl.get(), app.brCodeText.get(),
                                   D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
        return;
    }
    default:
        if (it.quoteDepth > 0) {
            app.rt->FillRectangle(rect(it.x - 12.f, it.y, it.x - 9.f, it.y + it.h),
                                  app.brQuote.get());
        }
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
    app.fmtH[0] = makeFormat(app, L"Segoe UI", 30.f, DWRITE_FONT_WEIGHT_BOLD, 1.30f);
    app.fmtH[1] = makeFormat(app, L"Segoe UI", 23.f, DWRITE_FONT_WEIGHT_BOLD, 1.35f);
    app.fmtH[2] = makeFormat(app, L"Segoe UI", 19.f, DWRITE_FONT_WEIGHT_BOLD, 1.40f);
    app.fmtH[3] = makeFormat(app, L"Segoe UI", 16.5f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtH[4] = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtH[5] = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtCode = makeFormat(app, L"Consolas", 13.5f, DWRITE_FONT_WEIGHT_NORMAL, 1.55f);
    app.fmtTable = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_NORMAL, 1.45f);
    app.fmtTableHead = makeFormat(app, L"Segoe UI", 15.f, DWRITE_FONT_WEIGHT_BOLD, 1.45f);
    app.fmtMarker = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.6f);
    app.fmtMarkerRight = makeFormat(app, L"Segoe UI", 16.f, DWRITE_FONT_WEIGHT_NORMAL, 1.6f);
    if (app.fmtMarkerRight) app.fmtMarkerRight->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);

    app.th = makeTheme(app.themeDark);
    return app.fmtBody && app.fmtCode && app.fmtH[0];
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

    double t0 = nowMs();
    if (FAILED(app.d2d->CreateHwndRenderTarget(&rtp, &htp, app.rt.put()))) { trace("CreateHwndRenderTarget FAILED after %.1f ms", nowMs() - t0); return; }
    trace("CreateHwndRenderTarget ok in %.1f ms (type=%d)", nowMs() - t0, (int)rtp.type);
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
}

ScrollGeom scrollGeom(const App& app) {
    ScrollGeom g = {};
    float trackH = app.vh - 16.f;
    float contentH = std::max(app.lay.contentH, app.vh);
    g.visible = app.lay.contentH > app.vh + 1.f;
    g.trackW = 8.f;
    g.trackX = app.vw - g.trackW - 4.f;
    g.trackY = 8.f;
    g.trackH = trackH;
    float ratio = app.vh / contentH;
    g.thumbH = std::max(28.f, trackH * ratio);
    float maxScroll = std::max(1.f, contentH - app.vh);
    float t = app.scrollY / maxScroll;
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
    float contentH = std::max(app.lay.contentH, app.vh);
    scrollTo(app, t * (contentH - app.vh));
}

void scrollTo(App& app, float y) {
    if (y < 0.f) y = 0.f;
    if (app.lay.done) {
        float maxY = std::max(0.f, app.lay.contentH - app.vh);
        if (y > maxY) y = maxY;
    }
    app.scrollY = y;
    ensureLayoutForView(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void ensureLayoutForView(App& app) {
    if (app.lay.done) return;
    layoutStep(app, app.scrollY + app.vh * 1.5f, 8);
    if (!app.lay.done) SetTimer(app.hwnd, 1, 1, nullptr);
}

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

    D2D1_MATRIX_3X2_F tr = {1.f, 0.f, 0.f, 1.f, 0.f, -app.scrollY};
    app.rt->SetTransform(tr);

    if (app.lay.items.empty()) {
        app.rt->SetTransform(identity);
        const wchar_t* hint = L"把 .md 文件拖到这里，或按 Ctrl+O 打开";
        DWRITE_TEXT_METRICS m = {};
        ComPtr<IDWriteTextLayout> tl;
        if (app.fmtBody &&
            SUCCEEDED(app.dwrite->CreateTextLayout(hint, (UINT32)wcslen(hint), app.fmtBody.get(),
                                                   app.vw, 100.f, tl.put()))) {
            tl->GetMetrics(&m);
            D2D1_POINT_2F pt = {std::floor((app.vw - m.width) * 0.5f),
                                std::floor((app.vh - m.height) * 0.5f)};
            app.rt->DrawTextLayout(pt, tl.get(), app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
        }
    } else {
        for (const auto& it : app.lay.items) {
            if (it.y + it.h < app.scrollY - 200.f) continue;
            if (it.y > app.scrollY + app.vh + 200.f) break;
            drawItem(app, it);
        }
        app.rt->SetTransform(identity);
    }

    if (!app.lay.items.empty()) {
        ScrollGeom g = scrollGeom(app);
        if (g.visible && app.brThumb) {
            app.rt->SetTransform(identity);
            D2D1_ROUNDED_RECT rr;
            rr.rect = rect(g.trackX, g.thumbY, g.trackX + g.trackW, g.thumbY + g.thumbH);
            rr.radiusX = 4.f;
            rr.radiusY = 4.f;
            app.rt->FillRoundedRectangle(rr, app.brThumb.get());
        }
    }

    HRESULT hr = app.rt->EndDraw();

    static bool firstPaint = true;
    if (firstPaint) { firstPaint = false; trace("first frame presented"); }
    if (hr == D2DERR_RECREATE_TARGET) {
        releaseTarget(app);
        createTarget(app);
        layoutReset(app);
        ensureLayoutForView(app);
    }
}
