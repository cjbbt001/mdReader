#include "app.h"
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>

#define WM_APP_INIT (WM_APP + 2)

static UINT dpiOf(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT d = fn ? fn(h) : 96;
    return d ? d : 96;
}

static bool systemDark() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

// uxtheme has no public API for dark classic scrollbars. These ordinals are
// what every app uses: 135 = SetPreferredAppMode, 133 = AllowDarkModeForWindow.
static void enableDarkScrollbars(HWND h) {
    static HMODULE ux = LoadLibraryW(L"uxtheme.dll");
    if (!ux) return;
    typedef int(WINAPI * SetPref)(int);
    typedef bool(WINAPI * AllowWin)(HWND, bool);
    static SetPref setPref = (SetPref)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    static AllowWin allowWin = (AllowWin)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    if (setPref) setPref(1);  // AllowDark
    if (allowWin) allowWin(h, true);
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}

static void applyDarkTitleBar(HWND h, bool dark) {
    typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE mod = LoadLibraryW(L"dwmapi.dll");
    static Fn fn = mod ? (Fn)GetProcAddress(mod, "DwmSetWindowAttribute") : nullptr;
    if (!fn) return;
    BOOL v = dark ? TRUE : FALSE;
    fn(h, 20, &v, sizeof(v));  // DWMWA_USE_IMMERSIVE_DARK_MODE (Win10 2004+)
    fn(h, 19, &v, sizeof(v));  // older Win10 builds
}

static void updateViewport(App& app) {
    RECT rc = {};
    GetClientRect(app.hwnd, &rc);
    app.vw = (float)(rc.right - rc.left) * 96.f / app.dpi;
    app.vh = (float)(rc.bottom - rc.top) * 96.f / app.dpi;
}

static void openFile(App& app, const std::wstring& path) {
    Doc doc;
    std::wstring err;
    if (!loadDoc(path, doc, err)) {
        MessageBoxW(app.hwnd, err.c_str(), L"MD Reader", MB_ICONWARNING);
        return;
    }
    app.doc = std::move(doc);
    buildToc(app.doc, app.toc);
    app.scrollY = 0.f;
    app.outlineScroll = 0.f;

    std::wstring title = app.doc.name + L" \u2014 MD Reader";
    SetWindowTextW(app.hwnd, title.c_str());

    layoutReset(app);
    ensureLayoutForView(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

static void openDialog(App& app) {
    // The common file dialog wants an apartment; pay for OLE only when used.
    static bool comInit = false;
    if (!comInit) { comInit = true; CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }

    wchar_t buf[1024] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter =
        L"Markdown \u6587\u4ef6\0*.md;*.markdown;*.mdown;*.mkd\0\u6240\u6709\u6587\u4ef6\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) openFile(app, buf);
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    App* app = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = (CREATESTRUCTW*)lp;
        app = (App*)cs->lpCreateParams;
        app->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)app);
        break;
    }
    case WM_SIZE:
        if (app) {
            app->dpi = (float)dpiOf(h);
            updateViewport(*app);
            if (app->rt) {
                RECT rc = {};
                GetClientRect(h, &rc);
                app->rt->SetDpi(app->dpi, app->dpi);
                D2D1_SIZE_U sz = {(UINT)rc.right, (UINT)rc.bottom};
                app->rt->Resize(sz);
                reflowAnchor(*app);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DPICHANGED:
        if (app) {
            app->dpi = (float)HIWORD(wp);
            RECT* r = (RECT*)lp;
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    case WM_ERASEBKGND:
        if (app && !app->rt) {
            HBRUSH b = CreateSolidBrush(themeBgColorRef(app->themeDark));
            RECT rc = {};
            GetClientRect(h, &rc);
            FillRect((HDC)wp, &rc, b);
            DeleteObject(b);
        }
        return 1;
    case WM_APP_INIT:
        if (app) {
            if (!initGraphics(*app)) {
                MessageBoxW(h, L"Direct2D 初始化失败", L"MD Reader", MB_ICONERROR);
                return 0;
            }
            app->dpi = (float)dpiOf(h);
            updateViewport(*app);
            createTarget(*app);
            DragAcceptFiles(h, TRUE);
            if (!app->pendingFile.empty()) openFile(*app, app->pendingFile);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (app) {
            ensureLayoutForView(*app);
            paint(*app);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (app && wp == 1) {
            layoutStep(*app, 1e9f, 8);
            if (app->lay.done) KillTimer(h, 1);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (app) {
            ScrollGeom g = scrollGeom(*app);
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            float dipX = (float)x * 96.f / app->dpi;
            float dipY = (float)y * 96.f / app->dpi;

            if (dipY < app->toolbarH) {
                D2D1_RECT_F b = outlineButtonRect(*app);
                if (dipX >= b.left && dipX <= b.right && dipY >= b.top && dipY <= b.bottom)
                    toggleOutline(*app);
                return 0;
            }
            int hit = outlineHit(*app, dipX, dipY);
            if (hit >= 0) {
                scrollToBlock(*app, app->toc[(size_t)hit].blockIndex);
                return 0;
            }
            if (g.visible && dipX >= g.trackX - 6.f && dipX <= g.trackX + g.trackW + 6.f) {
                if (dipY >= g.thumbY && dipY <= g.thumbY + g.thumbH) {
                    app->dragGrab = dipY - g.thumbY;
                } else {
                    app->dragGrab = g.thumbH * 0.5f;
                    scrollToThumb(*app, dipY - app->dragGrab);
                }
                app->dragThumb = true;
                SetCapture(h);
            }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (app && !app->dragThumb) {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            float dipX = (float)mx * 96.f / app->dpi;
            float dipY = (float)my * 96.f / app->dpi;
            D2D1_RECT_F b = outlineButtonRect(*app);
            int hover = -1;
            if (dipX >= b.left && dipX <= b.right && dipY >= b.top && dipY <= b.bottom) hover = 0;
            if (hover != app->hoverBtn) {
                app->hoverBtn = hover;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        if (app && app->dragThumb) {
            int y = GET_Y_LPARAM(lp);
            float dipY = (float)y * 96.f / app->dpi;
            scrollToThumb(*app, dipY - app->dragGrab);
        }
        return 0;
    case WM_LBUTTONUP:
        if (app && app->dragThumb) {
            app->dragThumb = false;
            ReleaseCapture();
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (app) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(h, &pt);
            if (app->showOutline && (float)pt.x * 96.f / app->dpi >= app->contentRight()) {
                scrollOutline(*app, -GET_WHEEL_DELTA_WPARAM(wp) / 120.f * 78.f);
                return 0;
            }
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == 0) lines = 3;
            float step = (float)lines * 22.f;
            scrollTo(*app, app->scrollY - (float)delta / 120.f * step);
        }
        return 0;
    case WM_KEYDOWN:
        if (app) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            switch (wp) {
            case VK_NEXT: scrollTo(*app, app->scrollY + app->viewH() * 0.92f); return 0;
            case VK_PRIOR: scrollTo(*app, app->scrollY - app->viewH() * 0.92f); return 0;
            case VK_HOME: scrollTo(*app, 0.f); return 0;
            case VK_END: scrollTo(*app, app->lay.contentH); return 0;
            case VK_DOWN: scrollTo(*app, app->scrollY + 48.f); return 0;
            case VK_UP: scrollTo(*app, app->scrollY - 48.f); return 0;
            case VK_SPACE: scrollTo(*app, app->scrollY + app->viewH() * 0.92f); return 0;
            case 'O':
                if (ctrl) { openDialog(*app); return 0; }
                break;
            case 'B':
                if (ctrl) { toggleOutline(*app); return 0; }
                break;
            }
        }
        break;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        if (n > 0 && app) {
            wchar_t path[1024] = L"";
            DragQueryFileW(hd, 0, path, 1024);
            openFile(*app, path);
        }
        DragFinish(hd);
        return 0;
    }
    case WM_SETTINGCHANGE:
        if (app && !app->forceTheme && lp &&
            wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
            bool dark = systemDark();
            if (dark != app->themeDark) {
                app->themeDark = dark;
                applyTheme(*app);
                applyDarkTitleBar(h, dark);
                if (dark) enableDarkScrollbars(h);
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    startClock();
    trace("winmain enter");
    typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
    if (auto fn = (SetDpiCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                            "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4);  // PER_MONITOR_AWARE_V2

    // No CoInitializeEx: nothing uses COM/OLE (D2D, DWrite and GetOpenFileName
    // all work without it). It costs ~30 ms at startup.
    App app;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--dark") == 0) app.forceTheme = 1;
            else if (wcscmp(argv[i], L"--light") == 0) app.forceTheme = 2;
            else if (!argv[i][0] || argv[i][0] != L'-') app.pendingFile = argv[i];
        }
        LocalFree(argv);
    }
    app.themeDark = app.forceTheme == 1 ? true : (app.forceTheme == 2 ? false : systemDark());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"MDReaderWindow";
    trace("class registered");
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"MDReaderWindow", L"MD Reader",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                1080, 900, nullptr, nullptr, hInst, &app);
    if (!hwnd) return 1;
    trace("createwindow done");
    applyDarkTitleBar(hwnd, app.themeDark);
    trace("dark caption set");
    trace("window created");
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    trace("window shown");
    PostMessageW(hwnd, WM_APP_INIT, 0, 0);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
