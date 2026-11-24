#pragma once
#include "app.h"

enum class ChromeAction {
    None = 0,
    NewTab,
    CloseTab,
    SelectTab,
    Outline,
    ToggleMode,
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
ChromeHit chromeHitTest(App& app, float x, float y);
D2D1_RECT_F toolButtonRect(const App& app, int index);
void toolButtonRects(const App& app, D2D1_RECT_F* out);
int toolButtonIndexAt(const App& app, float x, float y);
D2D1_RECT_F tabRect(const App& app, int index);
D2D1_RECT_F newTabRect(const App& app);
D2D1_RECT_F closeTabRect(const App& app, int index);
D2D1_RECT_F findBarRect(const App& app);
