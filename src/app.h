#pragma once
#include "common.h"
#include "doc.h"
#include "theme.h"
#include "layout.h"

#define WM_APP_LAYOUT (WM_APP + 1)

struct App {
    HWND hwnd = nullptr;

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1HwndRenderTarget> rt;

    ComPtr<ID2D1SolidColorBrush> brText, brMuted, brLink, brCodeBg, brCodeText, brRule,
        brQuote, brTableHead, brTableLine, brThumb;

    ComPtr<IDWriteTextFormat> fmtBody, fmtQuote, fmtCode, fmtTable, fmtTableHead,
        fmtMarker, fmtMarkerRight;
    ComPtr<IDWriteTextFormat> fmtH[6];

    float codeSpanSize = 14.f;

    Theme th;
    Doc doc;
    Layout lay;

    float scrollY = 0.f;
    float vw = 0.f, vh = 0.f;   // viewport in DIPs
    float dpi = 96.f;
    bool themeDark = false;
    bool dragThumb = false;
    float dragGrab = 0.f;
    int forceTheme = 0;  // 0 = follow system, 1 = dark, 2 = light
    std::wstring pendingFile;
};

bool initGraphics(App& app);
void releaseTarget(App& app);
void createTarget(App& app);
void applyTheme(App& app);
void paint(App& app);
void scrollTo(App& app, float y);
void ensureLayoutForView(App& app);
struct ScrollGeom { float trackX, trackW, thumbY, thumbH, trackY, trackH; bool visible; };
ScrollGeom scrollGeom(const App& app);
void scrollToThumb(App& app, float thumbY);
