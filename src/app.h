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
        brQuote, brTableHead, brTableLine, brThumb, brPanel;

    ComPtr<IDWriteTextFormat> fmtBody, fmtQuote, fmtCode, fmtTable, fmtTableHead,
        fmtMarker, fmtMarkerRight, fmtToc, fmtBtn;
    ComPtr<IDWriteTextFormat> fmtH[6];

    float codeSpanSize = 14.f;

    Theme th;
    Doc doc;
    Layout lay;
    std::vector<TocEntry> toc;

    // Chrome
    float toolbarH = 30.f;
    bool showOutline = false;
    float outlineW = 236.f;
    float outlineScroll = 0.f;
    int hoverBtn = -1;

    float viewH() const { return vh - toolbarH; }
    float contentRight() const { return showOutline ? vw - outlineW : vw; }

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
void toggleOutline(App& app);
void scrollToBlock(App& app, int blockIndex);
void scrollOutline(App& app, float dy);
D2D1_RECT_F outlineButtonRect(const App& app);
// Outline panel hit testing: index of the entry under (x, y) in DIPs, or -1.
int outlineHit(const App& app, float x, float y);
void outlineEntryRect(const App& app, int index, float* top, float* bottom);
