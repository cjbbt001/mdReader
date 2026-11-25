#include "app.h"
#include "chrome.h"
#include <algorithm>
#include <cmath>

const ToolButton kToolButtons[] = {
    // left: open, view mode, theme
    {ChromeAction::Open, L"打开 (Ctrl+O)", false, ToolGroup::Left},
    {ChromeAction::ToggleMode, L"源码/预览 (Ctrl+E)", false, ToolGroup::Left},
    {ChromeAction::ToggleTheme, L"深色/浅色", false, ToolGroup::Left},

    // middle: markdown commands
    {ChromeAction::H1, L"一级标题", true, ToolGroup::Middle},
    {ChromeAction::H2, L"二级标题", true, ToolGroup::Middle},
    {ChromeAction::H3, L"三级标题", true, ToolGroup::Middle},
    {ChromeAction::Bold, L"加粗 (Ctrl+B)", true, ToolGroup::Middle},
    {ChromeAction::Italic, L"斜体 (Ctrl+I)", true, ToolGroup::Middle},
    {ChromeAction::Strike, L"删除线", true, ToolGroup::Middle},
    {ChromeAction::List, L"列表", true, ToolGroup::Middle},
    {ChromeAction::Quote, L"引用", true, ToolGroup::Middle},
    {ChromeAction::Code, L"行内代码", true, ToolGroup::Middle},
    {ChromeAction::Link, L"链接 (Ctrl+K)", true, ToolGroup::Middle},
    {ChromeAction::Image, L"图片", true, ToolGroup::Middle},

    // right: save + panel
    {ChromeAction::Save, L"保存 (Ctrl+S)", false, ToolGroup::Right},
    {ChromeAction::Outline, L"大纲 (Ctrl+J)", false, ToolGroup::Right},
};
const int kToolButtonCount = (int)(sizeof(kToolButtons) / sizeof(kToolButtons[0]));

namespace {

const float kTabMinW = 96.f;
const float kBtn = 30.f;
const float kBtnGap = 2.f;
const float kGroupGap = 12.f;
const float kCapBtnW = 46.f;

const float kTabsLeft = 38.f;
const float kTabGap = 2.f;
const float kTabButton = 28.f;
const float kPreferredTabWidth = 150.f;

struct TabStrip {
    int first, count;
    float width;
    D2D1_RECT_F add, overflow;
};

TabStrip tabStrip(const App& app) {
    const int total = (int)app.tabs.size();
    const float right = app.vw - kCapBtnW * 3.f - 6.f;
    const float available = std::max(0.f, right - kTabsLeft - kTabButton);
    const bool overflow = total * (kTabMinW + kTabGap) > available;
    const float tabsWidth = std::max(0.f, available - (overflow ? kTabButton : 0.f));
    const int count = total ? std::min(total, std::max(1, (int)(tabsWidth / (kTabMinW + kTabGap)))) : 0;
    const float width = count ? std::max(0.f, std::min(kPreferredTabWidth, tabsWidth / count - kTabGap)) : 0.f;
    const int first = std::clamp(app.active - count + 1, 0, total - count);
    const float addLeft = kTabsLeft + count * (width + kTabGap);
    auto button = [&](float left) {
        return rect(left, 4.f, left + kTabButton, app.tabStripH - 2.f);
    };
    return {first, count, width, button(addLeft), overflow ? button(addLeft + kTabButton) : D2D1_RECT_F{}};
}

D2D1_RECT_F tabRect(const App& app, const TabStrip& strip, int index) {
    const float x = kTabsLeft + (index - strip.first) * (strip.width + kTabGap);
    return rect(x, 3.f, x + strip.width, app.tabStripH);
}

bool inRect(D2D1_RECT_F r, float x, float y) {
    return r.right > r.left && x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

D2D1_RECT_F tabClose(D2D1_RECT_F tab) {
    if (tab.right - tab.left < 38.f) return {};
    return rect(tab.right - 22.f, tab.top + 7.f, tab.right - 8.f, tab.bottom - 7.f);
}

void drawCheckPath(App& app, D2D1_RECT_F r, ID2D1Brush* b, bool checked) {
    D2D1_ROUNDED_RECT rr = {r, 3.f, 3.f};
    app.rt->DrawRoundedRectangle(rr, b, 1.2f);
    if (checked) {
        app.rt->DrawLine(D2D1_POINT_2F{r.left + 3.f, r.top + r.bottom * 0.5f - r.top * 0.5f + 3.f},
                         D2D1_POINT_2F{r.left + 6.f, r.bottom - 4.f}, b, 1.6f);
        app.rt->DrawLine(D2D1_POINT_2F{r.left + 6.f, r.bottom - 4.f},
                         D2D1_POINT_2F{r.right - 3.f, r.top + 4.f}, b, 1.6f);
    }
}

void drawIcon(App& app, ChromeAction a, D2D1_RECT_F r, ID2D1Brush* fg) {
    float cx = (r.left + r.right) * 0.5f;
    float cy = (r.top + r.bottom) * 0.5f;
    float w = r.right - r.left;
    float h = r.bottom - r.top;

    auto line = [&](float x1, float y1, float x2, float y2, float sw = 1.4f) {
        app.rt->DrawLine(D2D1_POINT_2F{x1, y1}, D2D1_POINT_2F{x2, y2}, fg, sw);
    };
    auto text = [&](const wchar_t* s, IDWriteTextFormat* f) {
        app.rt->DrawText(s, (UINT32)wcslen(s), f, r, fg, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    switch (a) {
    case ChromeAction::Open: {
        // folder
        app.rt->DrawRectangle(rect(cx - 7.f, cy - 5.f, cx + 7.f, cy + 5.f), fg, 1.3f);
        line(cx - 7.f, cy - 3.f, cx - 2.f, cy - 3.f);
        line(cx - 2.f, cy - 3.f, cx - 0.5f, cy - 5.5f);
        break;
    }
    case ChromeAction::Save: {
        app.rt->DrawRectangle(rect(cx - 6.5f, cy - 6.5f, cx + 6.5f, cy + 6.5f), fg, 1.3f);
        app.rt->FillRectangle(rect(cx - 3.f, cy - 6.5f, cx + 3.f, cy - 2.f), fg);
        app.rt->DrawRectangle(rect(cx - 4.f, cy + 1.f, cx + 4.f, cy + 6.5f), fg, 1.1f);
        break;
    }
    case ChromeAction::Undo:
        line(cx + 6.f, cy + 4.f, cx - 4.f, cy + 4.f);
        line(cx - 4.f, cy + 4.f, cx - 4.f, cy - 3.f);
        line(cx - 4.f, cy - 3.f, cx + 5.f, cy - 3.f);
        line(cx - 7.f, cy + 1.f, cx - 4.f, cy + 4.f);
        line(cx - 7.f, cy + 7.f, cx - 4.f, cy + 4.f);
        break;
    case ChromeAction::Redo:
        line(cx - 6.f, cy + 4.f, cx + 4.f, cy + 4.f);
        line(cx + 4.f, cy + 4.f, cx + 4.f, cy - 3.f);
        line(cx + 4.f, cy - 3.f, cx - 5.f, cy - 3.f);
        line(cx + 7.f, cy + 1.f, cx + 4.f, cy + 4.f);
        line(cx + 7.f, cy + 7.f, cx + 4.f, cy + 4.f);
        break;
    case ChromeAction::ToggleMode: {
        bool source = app.hasTab() && app.tab().mode == Mode::Source;
        if (source) {
            text(L"</>", app.fmtIconMono.get());
        } else {
            // eye
            app.rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 8.f, 5.f}, fg, 1.3f);
            app.rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 2.2f, 2.2f}, fg);
        }
        break;
    }
    case ChromeAction::Outline:
        // side panel with a content column
        app.rt->DrawRoundedRectangle(
            D2D1_ROUNDED_RECT{rect(cx - 8.f, cy - 6.5f, cx + 8.f, cy + 6.5f), 2.5f, 2.5f}, fg, 1.3f);
        line(cx + 2.f, cy - 6.f, cx + 2.f, cy + 6.f, 1.1f);
        line(cx + 4.f, cy - 3.f, cx + 6.5f, cy - 3.f, 1.1f);
        line(cx + 4.f, cy, cx + 6.5f, cy, 1.1f);
        line(cx + 4.f, cy + 3.f, cx + 6.5f, cy + 3.f, 1.1f);
        line(cx - 6.f, cy - 3.f, cx - 1.f, cy - 3.f, 1.1f);
        line(cx - 6.f, cy, cx - 1.f, cy, 1.1f);
        break;
    case ChromeAction::ToggleTheme: {
        bool dark = app.themeDark;
        if (!dark) {
            // sun: circle plus eight rays
            app.rt->DrawEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx, cy}, 3.4f, 3.4f}, fg, 1.3f);
            for (int k = 0; k < 8; ++k) {
                float a = (float)k * 3.14159265f / 4.f;
                float dx = std::cos(a), dy = std::sin(a);
                line(cx + dx * 5.6f, cy + dy * 5.6f, cx + dx * 7.6f, cy + dy * 7.6f, 1.2f);
            }
        } else {
            // moon: crescent
            app.rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx - 0.6f, cy}, 6.2f, 6.2f}, fg);
            ComPtr<ID2D1SolidColorBrush> bg;
            app.rt->CreateSolidColorBrush(app.th.bg, bg.put());
            app.rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx + 3.4f, cy - 2.6f}, 5.6f, 5.6f},
                                bg.get());
        }
        break;
    }
    case ChromeAction::H1: text(L"H1", app.fmtIcon.get()); break;
    case ChromeAction::H2: text(L"H2", app.fmtIcon.get()); break;
    case ChromeAction::H3: text(L"H3", app.fmtIcon.get()); break;
    case ChromeAction::Bold: text(L"B", app.fmtIconBold.get()); break;
    case ChromeAction::Italic: text(L"I", app.fmtIconItalic.get()); break;
    case ChromeAction::Strike:
        text(L"S", app.fmtIcon.get());
        line(cx - 4.5f, cy + 0.5f, cx + 4.5f, cy + 0.5f, 1.4f);
        break;
    case ChromeAction::List:
        for (int i = 0; i < 3; ++i) {
            float y = cy - 5.f + i * 5.f;
            app.rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx - 7.f, y}, 1.4f, 1.4f}, fg);
            line(cx - 3.f, y, cx + 7.f, y);
        }
        break;
    case ChromeAction::Quote:
        line(cx - 6.f, cy - 6.f, cx - 6.f, cy + 6.f, 2.f);
        line(cx - 2.f, cy - 4.f, cx + 7.f, cy - 4.f);
        line(cx - 2.f, cy + 2.f, cx + 4.f, cy + 2.f);
        break;
    case ChromeAction::Code: text(L"{ }", app.fmtIconMono.get()); break;
    case ChromeAction::Link:
        app.rt->DrawRoundedRectangle(
            D2D1_ROUNDED_RECT{rect(cx - 7.f, cy - 5.f, cx + 1.f, cy + 1.f), 3.f, 3.f}, fg, 1.3f);
        app.rt->DrawRoundedRectangle(
            D2D1_ROUNDED_RECT{rect(cx - 1.f, cy - 1.f, cx + 7.f, cy + 5.f), 3.f, 3.f}, fg, 1.3f);
        break;
    case ChromeAction::Image:
        app.rt->DrawRoundedRectangle(
            D2D1_ROUNDED_RECT{rect(cx - 7.f, cy - 6.f, cx + 7.f, cy + 6.f), 2.f, 2.f}, fg, 1.3f);
        app.rt->FillEllipse(D2D1_ELLIPSE{D2D1_POINT_2F{cx - 3.f, cy - 2.5f}, 1.4f, 1.4f}, fg);
        line(cx - 6.f, cy + 4.f, cx - 1.f, cy - 1.f);
        line(cx - 1.f, cy - 1.f, cx + 2.f, cy + 2.f);
        line(cx + 2.f, cy + 2.f, cx + 6.f, cy - 1.5f);
        break;
    default:
        break;
    }
}

}  // namespace

void toolButtonRects(const App& app, D2D1_RECT_F* out) {
    float w[3] = {0.f, 0.f, 0.f};
    for (int i = 0; i < kToolButtonCount; ++i) w[(int)kToolButtons[i].group] += kBtn + kBtnGap;
    for (auto& v : w) v -= kBtnGap;

    float leftX = 10.f;
    float rightX = app.vw - 10.f - w[2];
    float midX = (app.vw - w[1]) * 0.5f;
    if (midX < leftX + w[0] + 18.f) midX = leftX + w[0] + 18.f;
    if (midX + w[1] > rightX - 18.f) midX = rightX - 18.f - w[1];

    float cursor[3] = {leftX, midX, rightX};
    float top = app.tabStripH + (app.toolbarH - kBtn) * 0.5f;
    for (int i = 0; i < kToolButtonCount; ++i) {
        int g = (int)kToolButtons[i].group;
        out[i] = rect(cursor[g], top, cursor[g] + kBtn, top + kBtn);
        cursor[g] += kBtn + kBtnGap;
    }
}

int toolButtonIndexAt(const App& app, float x, float y) {
    if (y < app.tabStripH || y > app.contentTop()) return -1;
    D2D1_RECT_F r[kToolButtonCount];
    toolButtonRects(app, r);
    for (int i = 0; i < kToolButtonCount; ++i)
        if (x >= r[i].left - 1.f && x <= r[i].right + 1.f) return i;
    return -1;
}

D2D1_RECT_F toolButtonRect(const App& app, int index) {
    D2D1_RECT_F r[kToolButtonCount];
    toolButtonRects(app, r);
    if (index < 0 || index >= kToolButtonCount) return rect(0, 0, 0, 0);
    return r[index];
}


D2D1_RECT_F tabOverflowRect(const App& app) { return tabStrip(app).overflow; }

D2D1_RECT_F captionIconRect(const App& app) {
    float size = 21.f;
    float top = (app.tabStripH - size) * 0.5f;
    return rect(8.f, top, 8.f + size, top + size);
}

namespace {
D2D1_RECT_F capButton(const App& app, int index) {
    float right = app.vw - (float)(2 - index) * kCapBtnW;
    return rect(right - kCapBtnW, 0.f, right, app.tabStripH);
}
}  // namespace

D2D1_RECT_F captionMinRect(const App& app) { return capButton(app, 0); }
D2D1_RECT_F captionMaxRect(const App& app) { return capButton(app, 1); }
D2D1_RECT_F captionCloseRect(const App& app) { return capButton(app, 2); }

bool captionIsEmpty(const App& app, float x, float y) {
    if (y >= app.tabStripH) return false;
    if (x < 34.f) return false;  // icon area
    if (x >= captionMinRect(app).left) return false;
    return chromeHitTest(app, x, y).action == ChromeAction::None;
}

D2D1_RECT_F findBarRect(const App& app) {
    return rect(80.f, app.tabStripH + 4.f, app.vw - 130.f, app.contentTop() - 4.f);
}

namespace {

void drawTabStrip(App& app) {
    app.rt->FillRectangle(rect(0.f, 0.f, app.vw, app.tabStripH), app.brPanel.get());

    {
        // Page-with-folded-corner mark in the theme's text colour, so it sits on
        // the caption background instead of carrying a white plate.
        D2D1_RECT_F ir = captionIconRect(app);
        float w = ir.right - ir.left, h = ir.bottom - ir.top;
        float L = ir.left + w * 0.20f, R = ir.left + w * 0.80f;
        float T = ir.top + h * 0.10f, B = ir.top + h * 0.92f;
        float fold = w * 0.24f;
        float rx = R - fold;
        ID2D1Brush* fg = app.brText.get();
        float lw = 1.15f;
        app.rt->DrawLine(D2D1_POINT_2F{L, B}, D2D1_POINT_2F{R, B}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{R, B}, D2D1_POINT_2F{R, T + fold}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{R, T + fold}, D2D1_POINT_2F{rx, T + fold}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{rx, T + fold}, D2D1_POINT_2F{rx, T}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{rx, T}, D2D1_POINT_2F{L, T}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{L, T}, D2D1_POINT_2F{L, B}, fg, lw);
        app.rt->DrawLine(D2D1_POINT_2F{rx, T}, D2D1_POINT_2F{R, T + fold}, fg, lw);
    }

    const TabStrip strip = tabStrip(app);
    for (int i = strip.first; i < strip.first + strip.count; ++i) {
        D2D1_RECT_F r = tabRect(app, strip, i);
        bool sel = i == app.active;
        D2D1_COLOR_F bg = sel ? app.th.bg : app.th.panel;
        ComPtr<ID2D1SolidColorBrush> b;
        app.rt->CreateSolidColorBrush(bg, b.put());
        D2D1_ROUNDED_RECT rr = {rect(r.left, r.top + 2.f, r.right, r.bottom - 2.f), 5.f, 5.f};
        app.rt->FillRoundedRectangle(rr, b.get());

        std::wstring title = app.tabs[(size_t)i].name.empty() ? L"\u65e0\u6807\u9898"
                                                             : app.tabs[(size_t)i].name;
        const bool dirty = app.tabs[(size_t)i].dirty;
        const float titleRight = r.right - (dirty ? 36.f : 24.f);
        if (dirty)
            app.rt->FillEllipse({{r.right - 29.f, (r.top + r.bottom) * 0.5f}, 2.f, 2.f},
                                app.brMuted.get());
        app.rt->DrawText(title.c_str(), (UINT32)title.size(),
                         sel ? app.fmtTabActive.get() : app.fmtTab.get(),
                         rect(r.left + 10.f, r.top + 2.f, titleRight, r.bottom - 2.f),
                         sel ? app.brText.get() : app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

        D2D1_RECT_F c = tabClose(r);
        if (c.right <= c.left) continue;
        bool hot = app.hoverCloseTab == i;
        ID2D1Brush* cb = hot ? app.brText.get() : app.brMuted.get();
        if (hot) {
            ComPtr<ID2D1SolidColorBrush> hb;
            app.rt->CreateSolidColorBrush(app.th.tableHead, hb.put());
            app.rt->FillRoundedRectangle(D2D1_ROUNDED_RECT{c, 3.f, 3.f}, hb.get());
        }
        float m = 3.5f;
        app.rt->DrawLine(D2D1_POINT_2F{c.left + m, c.top + m},
                         D2D1_POINT_2F{c.right - m, c.bottom - m}, cb, 1.2f);
        app.rt->DrawLine(D2D1_POINT_2F{c.right - m, c.top + m},
                         D2D1_POINT_2F{c.left + m, c.bottom - m}, cb, 1.2f);
    }

    app.rt->DrawText(L"+", 1, app.fmtIcon.get(), strip.add, app.brMuted.get(),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (strip.overflow.right > strip.overflow.left) {
        const float x = (strip.overflow.left + strip.overflow.right) * 0.5f;
        const float y = (strip.overflow.top + strip.overflow.bottom) * 0.5f;
        app.rt->DrawLine({x - 4.f, y - 2.f}, {x, y + 2.f}, app.brMuted.get(), 1.3f);
        app.rt->DrawLine({x, y + 2.f}, {x + 4.f, y - 2.f}, app.brMuted.get(), 1.3f);
    }

    // ---- window buttons ----
    D2D1_RECT_F btn[3] = {captionMinRect(app), captionMaxRect(app), captionCloseRect(app)};
    const wchar_t* glyph[3] = {L"\uE921", app.maximized ? L"\uE923" : L"\uE922", L"\uE8BB"};
    for (int i = 0; i < 3; ++i) {
        bool hot = app.ncHover == i + 1;
        if (hot) {
            ComPtr<ID2D1SolidColorBrush> hb;
            D2D1_COLOR_F col = (i == 2) ? rgb(0xC42B1C) : app.th.tableHead;
            app.rt->CreateSolidColorBrush(col, hb.put());
            app.rt->FillRectangle(btn[i], hb.get());
        }
        ID2D1Brush* fg = (hot && i == 2) ? nullptr : app.brText.get();
        if (hot && i == 2) {
            ComPtr<ID2D1SolidColorBrush> white;
            app.rt->CreateSolidColorBrush(rgb(0xFFFFFF), white.put());
            app.rt->DrawText(glyph[i], 1, app.fmtCaption.get(), btn[i], white.get(),
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
        } else {
            app.rt->DrawText(glyph[i], 1, app.fmtCaption.get(), btn[i], fg,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    }

    app.rt->FillRectangle(rect(0.f, app.tabStripH - 1.f, app.vw, app.tabStripH),
                          app.brRule.get());
}

void drawToolbar(App& app) {
    ComPtr<ID2D1SolidColorBrush> bg;
    app.rt->CreateSolidColorBrush(app.th.bg, bg.put());
    app.rt->FillRectangle(rect(0.f, app.tabStripH, app.vw, app.contentTop()), bg.get());
    app.rt->FillRectangle(rect(0.f, app.contentTop() - 1.f, app.vw, app.contentTop()),
                          app.brRule.get());

    bool editing = app.hasTab();
    D2D1_RECT_F r[kToolButtonCount];
    toolButtonRects(app, r);

    for (int i = 0; i < kToolButtonCount; ++i) {
        bool disabled = kToolButtons[i].needsEditor && !editing;
        bool active = false;
        switch (kToolButtons[i].action) {
        case ChromeAction::Outline: active = app.showOutline; break;
        case ChromeAction::ToggleMode: active = editing && app.tab().mode == Mode::Source; break;
        default: break;
        }

        if ((app.hoverBtn == i || active) && !disabled) {
            ComPtr<ID2D1SolidColorBrush> hb;
            app.rt->CreateSolidColorBrush(active ? app.th.tableHead : app.th.panel, hb.put());
            app.rt->FillRoundedRectangle(
                D2D1_ROUNDED_RECT{rect(r[i].left, r[i].top + 1.f, r[i].right, r[i].bottom - 1.f), 5.f,
                                  5.f},
                hb.get());
        }
        ID2D1Brush* fg =
            disabled ? app.brRule.get() : (active ? app.brAccent.get() : app.brText.get());
        drawIcon(app, kToolButtons[i].action,
                 rect(r[i].left + 5.f, r[i].top + 5.f, r[i].right - 5.f, r[i].bottom - 5.f), fg);
    }
}

void drawFindBar(App& app) {
    ComPtr<ID2D1SolidColorBrush> bg;
    app.rt->CreateSolidColorBrush(app.th.bg, bg.put());
    app.rt->FillRectangle(rect(0.f, app.tabStripH, app.vw, app.contentTop()), bg.get());
    app.rt->FillRectangle(rect(0.f, app.contentTop() - 1.f, app.vw, app.contentTop()), app.brRule.get());
    app.rt->DrawText(L"查找", 2, app.fmtStatus.get(),
                     rect(14.f, app.tabStripH + 4.f, 70.f, app.contentTop() - 4.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    D2D1_RECT_F f = findBarRect(app);
    app.rt->DrawRoundedRectangle(D2D1_ROUNDED_RECT{f, 4.f, 4.f}, app.brRule.get(), 1.2f);
    std::wstring shown = app.findQuery;
    app.rt->DrawText(shown.c_str(), (UINT32)shown.size(), app.fmtStatus.get(),
                     rect(f.left + 8.f, f.top, f.right - 8.f, f.bottom),
                     app.brText.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (app.findQuery.empty()) {
        app.rt->DrawText(L"输入要查找的内容", 8, app.fmtStatus.get(),
                         rect(f.left + 8.f, f.top, f.right - 8.f, f.bottom),
                         app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    // caret in the field
    if (app.caretOn) {
        ComPtr<IDWriteTextLayout> tl;
        float cx = f.left + 8.f;
        if (SUCCEEDED(app.dwrite->CreateTextLayout(shown.c_str(), (UINT32)shown.size(),
                                                   app.fmtStatus.get(), 10000.f, 100.f, tl.put()))) {
            DWRITE_TEXT_METRICS m = {};
            tl->GetMetrics(&m);
            cx += m.widthIncludingTrailingWhitespace;
        }
        app.rt->FillRectangle(rect(cx, f.top + 4.f, cx + 1.f, f.bottom - 4.f), app.brText.get());
    }

    wchar_t count[64];
    if (app.findQuery.empty())
        wcscpy(count, L"");
    else if (app.findCount == 0)
        wcscpy(count, L"无结果");
    else
        swprintf(count, 64, L"%d / %d", app.findIndex + 1, app.findCount);
    app.rt->DrawText(count, (UINT32)wcslen(count), app.fmtStatus.get(),
                     rect(f.right + 8.f, app.tabStripH + 4.f, app.vw - 90.f, app.contentTop() - 4.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

    app.rt->DrawText(L"\u2191", 1, app.fmtIcon.get(),
                     rect(app.vw - 88.f, app.tabStripH + 3.f, app.vw - 62.f, app.contentTop() - 3.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    app.rt->DrawText(L"\u2193", 1, app.fmtIcon.get(),
                     rect(app.vw - 62.f, app.tabStripH + 3.f, app.vw - 36.f, app.contentTop() - 3.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    app.rt->DrawText(L"\u00d7", 1, app.fmtIcon.get(),
                     rect(app.vw - 36.f, app.tabStripH + 3.f, app.vw - 12.f, app.contentTop() - 3.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void drawStatus(App& app) {
    float y = app.vh - app.statusH;
    app.rt->FillRectangle(rect(0.f, y, app.vw, app.vh), app.brPanel.get());
    app.rt->FillRectangle(rect(0.f, y, app.vw, y + 1.f), app.brRule.get());

    float ty = y + 4.f;
    float tb = app.vh - 1.f;
    auto put = [&](const std::wstring& s, float l, float r) {
        if (s.empty()) return;
        app.rt->DrawText(s.c_str(), (UINT32)s.size(), app.fmtStatus.get(), rect(l, ty, r, tb),
                         app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    std::wstring left, mid, mode;
    if (app.hasTab()) {
        const Tab& t = app.tab();
        if (t.mode == Mode::Source) {
            wchar_t buf[64];
            swprintf(buf, 64, L"行 %d，列 %d", t.ed.caret.line + 1, t.ed.caret.col + 1);
            left = buf;
            swprintf(buf, 64, L"%d 行", t.ed.lineCount());
            mid = buf;
        } else {
            wchar_t buf[64];
            swprintf(buf, 64, L"%d 个块", (int)t.doc.blocks.size());
            left = buf;

        }
        if (t.diskChanged) mid = L"文件已在外部更改";
        mode = t.mode == Mode::Source ? L"源码" : L"预览";
    }

    put(left, 12.f, 120.f);
    put(mid, 120.f, 250.f);
    put(mode, 260.f, 330.f);
    if (app.hasTab() && !app.tab().path.empty())
        put(app.tab().path, 340.f, app.vw - 74.f);
    put(L"UTF-8", app.vw - 64.f, app.vw - 10.f);
}

}  // namespace

void drawChrome(App& app) {
    drawTabStrip(app);
    if (app.findOpen)
        drawFindBar(app);
    else
        drawToolbar(app);
    drawStatus(app);
}

ChromeHit chromeHitTest(const App& app, float x, float y) {
    ChromeHit h;
    if (y < app.tabStripH) {
        const TabStrip strip = tabStrip(app);
        if (inRect(strip.add, x, y)) return {ChromeAction::NewTab};
        if (inRect(strip.overflow, x, y)) return {ChromeAction::TabMenu};
        for (int i = strip.first; i < strip.first + strip.count; ++i) {
            const auto tab = tabRect(app, strip, i);
            if (inRect(tabClose(tab), x, y)) return {ChromeAction::CloseTab, i};
            if (inRect(tab, x, y)) return {ChromeAction::SelectTab, i};
        }
        return h;
    }

    if (app.findOpen) {
        if (x >= app.vw - 88.f && x <= app.vw - 62.f) { h.action = ChromeAction::FindPrev; return h; }
        if (x >= app.vw - 62.f && x <= app.vw - 36.f) { h.action = ChromeAction::FindNext; return h; }
        if (x >= app.vw - 36.f) { h.action = ChromeAction::CloseFind; return h; }
        return h;
    }

    int idx = toolButtonIndexAt(app, x, y);
    if (idx >= 0) {
        h.action = kToolButtons[idx].action;
        h.button = idx;
    }
    return h;
}
