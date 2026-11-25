#pragma once
#include "app.h"

enum class ChromeAction {
    None = 0,
    NewTab,
    TabMenu,
    CloseTab,
    SelectTab,
    Outline,
    ToggleMode,
    ToggleTheme,
    H1,
    H2,
    H3,
    Bold,
    Italic,
    Strike,
    List,
    Quote,
    Code,
    Link,
    Image,
    Find,
    Open,
    Save,
    Undo,
    Redo,
    FindNext,
    FindPrev,
    CloseFind,
};

struct ChromeHit {
    ChromeAction action = ChromeAction::None;
    int index = -1;  // tab index for SelectTab / CloseTab
    int button = -1; // toolbar button index
};

enum class ToolGroup { Left, Middle, Right };

struct ToolButton {
    ChromeAction action;
    const wchar_t* tip;
    bool needsEditor;
    ToolGroup group;
};

extern const ToolButton kToolButtons[];
extern const int kToolButtonCount;

void drawChrome(App& app);          // tab strip, toolbar, find bar, status bar
ChromeHit chromeHitTest(const App& app, float x, float y);
D2D1_RECT_F toolButtonRect(const App& app, int index);
void toolButtonRects(const App& app, D2D1_RECT_F* out);
int toolButtonIndexAt(const App& app, float x, float y);
D2D1_RECT_F tabOverflowRect(const App& app);
D2D1_RECT_F findBarRect(const App& app);
D2D1_RECT_F captionIconRect(const App& app);
D2D1_RECT_F captionMinRect(const App& app);
D2D1_RECT_F captionMaxRect(const App& app);
D2D1_RECT_F captionCloseRect(const App& app);
bool captionIsEmpty(const App& app, float x, float y);
