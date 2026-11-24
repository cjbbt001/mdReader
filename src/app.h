#pragma once
#include "common.h"
#include "doc.h"
#include "layout.h"
#include "tab.h"
#include "theme.h"

#define WM_APP_LAYOUT (WM_APP + 1)
#define WM_APP_INIT (WM_APP + 2)
#define WM_APP_IMAGE (WM_APP + 3)

struct App {
    HWND hwnd = nullptr;

    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<ID2D1HwndRenderTarget> rt;

    ComPtr<ID2D1SolidColorBrush> brText, brMuted, brLink, brCodeBg, brCodeText, brRule,
        brQuote, brTableHead, brTableLine, brThumb, brPanel, brAccent, brSel, brHilite,
        brKw, brStr, brCom, brNum;

    ComPtr<IDWriteTextFormat> fmtBody, fmtQuote, fmtCode, fmtTable, fmtTableHead,
        fmtMarker, fmtMarkerRight, fmtToc, fmtBtn, fmtTab, fmtTabActive, fmtSource,
        fmtStatus, fmtIcon, fmtIconBold, fmtIconItalic, fmtIconMono, fmtHead[6];

    float codeSpanSize = 14.f;

    Theme th;
    std::vector<Tab> tabs;
    int active = -1;

    Tab& tab() { return tabs[(size_t)active]; }
    const Tab& tab() const { return tabs[(size_t)active]; }
    bool hasTab() const { return active >= 0 && active < (int)tabs.size(); }

    // ---- chrome geometry (DIPs) ----
    float tabStripH = 34.f;
    float toolbarH = 36.f;
    float statusH = 24.f;
    float outlineW = 236.f;

    float contentTop() const { return tabStripH + toolbarH; }
    float contentRight() const { return showOutline ? vw - outlineW : vw; }
    float viewH() const { return vh - contentTop() - statusH; }

    bool showOutline = false;
    float outlineScroll = 0.f;

    // ---- find bar ----
    bool findOpen = false;
    std::wstring findQuery;
    int findIndex = -1;
    int findCount = 0;

    // ---- interaction ----
    int hoverBtn = -1;
    int hoverCloseTab = -1;
    bool dragThumb = false;
    float dragGrab = 0.f;
    bool selecting = false;
    bool caretOn = true;

    float vw = 0.f, vh = 0.f;
    float dpi = 96.f;
    bool themeDark = false;
    int forceTheme = 0;  // 0 = follow system, 1 = dark, 2 = light
    std::wstring pendingFile;
};

bool initGraphics(App& app);
void releaseTarget(App& app);
void createTarget(App& app);
void applyTheme(App& app);
void paint(App& app);

// ---- document / tabs ----
void newTab(App& app, const std::wstring& path, const std::wstring& text);
void openPath(App& app, const std::wstring& path);
void closeTab(App& app, int index);
void selectTab(App& app, int index);
void switchMode(App& app, Mode mode);
void toggleMode(App& app);
bool saveTab(App& app);           // false when cancelled/failed
void saveTabAs(App& app);
void syncEditorToPreview(App& app);  // re-parse the buffer, re-anchor the view
void previewEditAt(App& app, int blockIndex);
int blockAtViewPoint(App& app, float x, float y);
int sourceLineForBlock(Tab& t, int blockIndex);

// ---- layout / scrolling ----
void scrollTo(App& app, float y);
void ensureLayoutForView(App& app);
void toggleOutline(App& app);
void scrollToBlock(App& app, int blockIndex);
void scrollOutline(App& app, float dy);
void reflowAnchor(App& app);
int blockAtScroll(const App& app);
struct ScrollGeom { float trackX, trackW, thumbY, thumbH, trackY, trackH; bool visible; };
ScrollGeom scrollGeom(const App& app);
void scrollToThumb(App& app, float thumbY);
D2D1_RECT_F outlineButtonRect(const App& app);
int outlineHit(const App& app, float x, float y);
void outlineEntryRect(const App& app, int index, float* top, float* bottom);

// ---- editor commands ----
void edInsertText(App& app, const std::wstring& s);
void edKey(App& app, WPARAM vk, bool ctrl, bool shift);
void edCopy(App& app, bool cut);
void edPaste(App& app);
void edUndo(App& app);
void edRedo(App& app);
void edMouseDown(App& app, float x, float y, bool extend);
void edMouseDrag(App& app, float x, float y);
void edSetCaretFromPoint(App& app, float x, float y, bool extend);
void edEnsureCaretVisible(App& app);
void edMoveCaretLine(App& app, int delta);
void applyFormat(App& app, int action);
void edSelectAll(App& app);
void edDeleteWordLeft(App& app);

// ---- find ----
void findOpenBar(App& app);
void findClose(App& app);
void findType(App& app, wchar_t ch);
void findBackspace(App& app);
void findNext(App& app, int dir);
void findRefresh(App& app);
void findJump(App& app);

// ---- images ----
void imagePoll(App& app);
std::wstring resolveImagePath(const Tab& t, const std::wstring& src);
bool imageInfo(const std::wstring& path, UINT* w, UINT* h, int* state);
void requestImage(App& app, const std::wstring& path);
ID2D1Bitmap* imageBitmap(const std::wstring& path);
void drawImageBlock(App& app, const Item& it, const std::wstring& path);

// ---- syntax highlighting ----
void highlightCode(const std::wstring& code, const std::wstring& lang, std::vector<Run>& out);

// ---- settings / shell ----
void loadState(App& app);
void saveState(App& app);
void registerShell(bool install);
void openHelp(App& app);
