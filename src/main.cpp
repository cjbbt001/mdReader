#include "app.h"
#include "chrome.h"
#include <commdlg.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <imm.h>

void srcDraw(App& app);
void toggleTheme(App& app);
float srcGutterW(const App& app);
void srcHitTestPoint(App& app, float x, float y, Pos* p);
void srcDrawEmbedded(App& app);
float srcCaretX(App& app);
float srcCaretY(App& app);
float srcLineHeight();

namespace {

const float kCaretBlinkMs = 530.f;

std::wstring exePath() {
    wchar_t buf[1024];
    GetModuleFileNameW(nullptr, buf, 1024);
    return buf;
}

std::wstring statePath() {
    wchar_t* p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) {
        dir = p;
        CoTaskMemFree(p);
    }
    dir += L"\\MDReader";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\state.txt";
}

UINT dpiOf(HWND h) {
    typedef UINT(WINAPI * Fn)(HWND);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT d = fn ? fn(h) : 96;
    return d ? d : 96;
}

bool systemDark() {
    DWORD v = 1, sz = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) != ERROR_SUCCESS)
        return false;
    return v == 0;
}

// Windows 11 draws the rounded outline for us; without the caption we still
// want the corners and the 1px border it paints around them.
void applyWindowShape(HWND h) {
    typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE mod = LoadLibraryW(L"dwmapi.dll");
    static Fn fn = mod ? (Fn)GetProcAddress(mod, "DwmSetWindowAttribute") : nullptr;
    if (!fn) return;
    const DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
    const int DWMWCP_ROUND = 2;
    int pref = DWMWCP_ROUND;
    fn(h, DWMWA_WINDOW_CORNER_PREFERENCE, &pref, sizeof(pref));
}

void applyDarkTitleBar(HWND h, bool dark) {
    typedef HRESULT(WINAPI * Fn)(HWND, DWORD, LPCVOID, DWORD);
    static HMODULE mod = LoadLibraryW(L"dwmapi.dll");
    static Fn fn = mod ? (Fn)GetProcAddress(mod, "DwmSetWindowAttribute") : nullptr;
    if (!fn) return;
    BOOL v = dark ? TRUE : FALSE;
    fn(h, 20, &v, sizeof(v));
    fn(h, 19, &v, sizeof(v));
}

void updateViewport(App& app) {
    RECT rc = {};
    GetClientRect(app.hwnd, &rc);
    app.vw = (float)(rc.right - rc.left) * 96.f / app.dpi;
    app.vh = (float)(rc.bottom - rc.top) * 96.f / app.dpi;
}

std::wstring readAllText(const std::wstring& path, bool* ok) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) { if (ok) *ok = false; return std::wstring(); }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    if (ok) *ok = true;
    return widen(s.c_str(), s.size());
}

bool writeAllText(const std::wstring& path, const std::wstring& text) {
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], need, nullptr, nullptr);
    std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(utf8.data(), (std::streamsize)utf8.size());
    return f.good();
}

std::wstring fileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void setTitle(App& app) {
    // The caption is custom-drawn (tabs replace the title text); the window text
    // only feeds the taskbar and Alt+Tab.
    (void)app;
    SetWindowTextW(app.hwnd, L"MD Reader");
}

}  // namespace

// ------------------------------------------------------------------ tabs

void newTab(App& app, const std::wstring& path, const std::wstring& text) {
    Tab t;
    t.path = path;
    t.name = path.empty() ? std::wstring() : fileNameOf(path);
    parseText(text, t.doc);
    buildToc(t.doc, t.toc);
    t.ed.setText(text);
    app.tabs.push_back(std::move(t));
    app.active = (int)app.tabs.size() - 1;
    app.showOutline = app.showOutline && !app.tab().toc.empty();
    layoutReset(app);
    ensureLayoutForView(app);
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void openPath(App& app, const std::wstring& pathIn) {
    wchar_t full[1024] = L"";
    std::wstring path = pathIn;
    if (GetFullPathNameW(pathIn.c_str(), 1024, full, nullptr)) path = full;
    for (int i = 0; i < (int)app.tabs.size(); ++i) {
        if (_wcsicmp(app.tabs[(size_t)i].path.c_str(), path.c_str()) == 0) {
            selectTab(app, i);
            return;
        }
    }
    bool ok = false;
    std::wstring text = readAllText(path, &ok);
    if (!ok) {
        MessageBoxW(app.hwnd, L"无法打开文件", L"MD Reader", MB_ICONWARNING);
        return;
    }
    newTab(app, path, text);
}

void selectTab(App& app, int index) {
    if (index < 0 || index >= (int)app.tabs.size()) return;
    app.active = index;
    app.outlineScroll = 0.f;
    if (!app.tab().lay.done) {
        ensureLayoutForView(app);
    }
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void closeTab(App& app, int index) {
    if (index < 0 || index >= (int)app.tabs.size()) return;
    if (app.tabs[(size_t)index].dirty) {
        app.active = index;
        std::wstring name = app.tab().name.empty() ? L"无标题" : app.tab().name;
        std::wstring msg = name + L" 尚未保存，要保存吗？";
        int r = MessageBoxW(app.hwnd, msg.c_str(), L"MD Reader", MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDCANCEL) return;
        if (r == IDYES && !saveTab(app)) return;
    }
    app.tabs.erase(app.tabs.begin() + index);
    if (app.tabs.empty()) {
        app.active = -1;
    } else if (app.active >= (int)app.tabs.size()) {
        app.active = (int)app.tabs.size() - 1;
    } else if (app.active > index) {
        app.active--;
    }
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void syncEditorToPreview(App& app) {
    if (!app.hasTab()) return;
    Tab& t = app.tab();
    std::wstring text = t.ed.text();
    Doc doc;
    parseText(text, doc);
    t.doc = std::move(doc);
    buildToc(t.doc, t.toc);
    float frac = t.ed.lineCount() > 1 ? (float)t.ed.firstLine / (float)t.ed.lineCount() : 0.f;
    layoutReset(app);
    ensureLayoutForView(app);
    scrollTo(app, frac * std::max(0.f, t.lay.contentH - app.viewH()));
}

void switchMode(App& app, Mode mode) {
    if (!app.hasTab() || app.tab().mode == mode) return;
    Tab& t = app.tab();
    if (mode == Mode::Preview) {
        syncEditorToPreview(app);
    } else {
        t.ed.firstLine = 0;
        app.findOpen = false;
        edEnsureCaretVisible(app);
    }
    t.mode = mode;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void toggleMode(App& app) {
    if (!app.hasTab()) return;
    switchMode(app, app.tab().mode == Mode::Preview ? Mode::Source : Mode::Preview);
}

bool saveTab(App& app) {
    if (!app.hasTab()) return true;
    Tab& t = app.tab();
    if (t.path.empty()) {
        saveTabAs(app);
        return !t.path.empty();
    }
    if (!writeAllText(t.path, t.ed.text())) {
        MessageBoxW(app.hwnd, L"保存失败", L"MD Reader", MB_ICONWARNING);
        return false;
    }
    t.dirty = false;
    t.ed.modified = false;
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

void saveTabAs(App& app) {
    if (!app.hasTab()) return;
    wchar_t buf[1024] = L"";
    Tab& t = app.tab();
    if (!t.name.empty()) wcscpy(buf, t.name.c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Markdown\0*.md;*.markdown\0所有文件\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return;
    t.path = buf;
    t.name = fileNameOf(buf);
    saveTab(app);
}

// ------------------------------------------------------------------ editor input

void edInsertText(App& app, const std::wstring& s) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;
    Tab& t = app.tab();
    t.ed.snapshot(GetTickCount64());
    t.ed.insert(s);
    t.dirty = true;
    edEnsureCaretVisible(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edSelectAll(App& app) {
    if (!app.hasTab()) return;
    Editor& ed = app.tab().ed;
    ed.anchor = Pos{0, 0};
    ed.caret = Pos{ed.lineCount() - 1, (int)ed.lines.back().size()};
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edCopy(App& app, bool cut) {
    if (!app.hasTab()) return;
    Editor& ed = app.tab().ed;
    std::wstring sel = ed.selectedText();
    if (sel.empty()) return;
    if (OpenClipboard(app.hwnd)) {
        EmptyClipboard();
        size_t bytes = (sel.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            memcpy(p, sel.c_str(), bytes);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
        }
        CloseClipboard();
    }
    if (cut) {
        ed.snapshot(GetTickCount64());
        ed.deleteSelection();
        app.tab().dirty = true;
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

namespace {

bool isImageExt(const std::wstring& path) {
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return false;
    std::wstring ext = path.substr(dot);
    for (auto& c : ext) c = (wchar_t)towlower((wint_t)c);
    static const wchar_t* kExts[] = {L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".webp",
                                     L".tif", L".tiff"};
    for (const wchar_t* e : kExts)
        if (ext == e) return true;
    return false;
}

// attachment/ lives next to the document; both paths are returned.
bool attachmentDir(App& app, std::wstring* dir, std::wstring* rel) {
    if (!app.hasTab() || app.tab().path.empty()) {
        MessageBoxW(app.hwnd, L"\u5148\u628a\u6587\u6863\u4fdd\u5b58\u5230\u78c1\u76d8\uff0c\u56fe\u7247\u4f1a\u653e\u5230\u540c\u7ea7\u76ee\u5f55\u7684 attachment \u6587\u4ef6\u5939",
                    L"MD Reader", MB_ICONINFORMATION);
        return false;
    }
    const std::wstring& p = app.tab().path;
    size_t slash = p.find_last_of(L"\\/");
    std::wstring base = (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash + 1);
    *dir = base + L"attachment\\";
    *rel = L"attachment/";
    CreateDirectoryW((base + L"attachment").c_str(), nullptr);
    return true;
}

std::wstring uniqueAttachName(const std::wstring& dir, const std::wstring& wanted) {
    std::wstring stem = wanted, ext;
    size_t dot = wanted.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        stem = wanted.substr(0, dot);
        ext = wanted.substr(dot);
    }
    std::wstring candidate = wanted;
    for (int n = 1; n < 1000; ++n) {
        if (GetFileAttributesW((dir + candidate).c_str()) == INVALID_FILE_ATTRIBUTES) return candidate;
        candidate = stem + L"-" + std::to_wstring(n) + ext;
    }
    return wanted;
}

void insertImageRef(App& app, const std::wstring& relLink) {
    Editor& ed = app.tab().ed;
    ed.snapshot(GetTickCount64());
    std::wstring line = std::wstring(L"![") + L"](" + relLink + L")";
    if (ed.caret.col > 0) line = std::wstring(L"\n") + line;
    ed.insert(line + std::wstring(L"\n"));
    app.tab().dirty = true;
    edEnsureCaretVisible(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// Files copied in Explorer: keep the original bytes and link them.
bool pasteImageFiles(App& app) {
    if (!OpenClipboard(app.hwnd)) return false;
    HANDLE h = GetClipboardData(CF_HDROP);
    std::vector<std::wstring> files;
    if (h) {
        HDROP hd = (HDROP)h;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            wchar_t buf[1024] = L"";
            if (DragQueryFileW(hd, i, buf, 1024) && isImageExt(buf)) files.push_back(buf);
        }
    }
    CloseClipboard();
    if (files.empty()) return false;

    std::wstring dir, rel;
    if (!attachmentDir(app, &dir, &rel)) return true;

    for (const auto& f : files) {
        std::wstring name = uniqueAttachName(dir, fileNameOf(f));
        if (!CopyFileW(f.c_str(), (dir + name).c_str(), TRUE)) continue;
        insertImageRef(app, rel + name);
    }
    return true;
}

// "PNG" clipboard format: raw PNG bytes, written straight through.
bool pastePngStream(App& app) {
    UINT fmt = RegisterClipboardFormatW(L"PNG");
    if (!fmt || !IsClipboardFormatAvailable(fmt)) return false;
    if (!OpenClipboard(app.hwnd)) return false;
    HANDLE h = GetClipboardData(fmt);
    std::vector<BYTE> bytes;
    if (h) {
        SIZE_T size = GlobalSize(h);
        const BYTE* p = (const BYTE*)GlobalLock(h);
        if (p && size > 8 && p[0] == 0x89 && p[1] == 0x50 && p[2] == 0x4E && p[3] == 0x47)
            bytes.assign(p, p + size);
        if (p) GlobalUnlock(h);
    }
    CloseClipboard();
    if (bytes.empty()) return false;

    std::wstring dir, rel;
    if (!attachmentDir(app, &dir, &rel)) return true;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t raw[128];
    swprintf(raw, 128, L"image-%02d%02d%02d-%02d%02d%02d.png", st.wYear % 100, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond);
    std::wstring name = uniqueAttachName(dir, raw);
    std::ofstream f((dir + name).c_str(), std::ios::binary | std::ios::trunc);
    if (!f) return true;
    f.write((const char*)bytes.data(), (std::streamsize)bytes.size());
    f.close();
    insertImageRef(app, rel + name);
    return true;
}

// Raw bitmap on the clipboard: encode to PNG through WIC.
bool pasteDib(App& app) {
    std::wstring dir, rel;
    if (!attachmentDir(app, &dir, &rel)) return true;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t raw[128];
    for (int n = 0; n < 100; ++n) {
        if (n == 0)
            swprintf(raw, 128, L"image-%02d%02d%02d-%02d%02d%02d.png", st.wYear % 100, st.wMonth,
                     st.wDay, st.wHour, st.wMinute, st.wSecond);
        else
            swprintf(raw, 128, L"image-%02d%02d%02d-%02d%02d%02d-%d.png", st.wYear % 100, st.wMonth,
                     st.wDay, st.wHour, st.wMinute, st.wSecond, n);
        std::wstring name = uniqueAttachName(dir, raw);
        if (!saveClipboardImage(app.hwnd, dir + name)) return true;
        insertImageRef(app, rel + name);
        return true;
    }
    return true;
}

}  // namespace

void edPaste(App& app) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;

    bool hasText = IsClipboardFormatAvailable(CF_UNICODETEXT) != 0;
    bool hasFiles = IsClipboardFormatAvailable(CF_HDROP) != 0;
    bool hasDib = IsClipboardFormatAvailable(CF_DIB) != 0;
    UINT pngFmt = RegisterClipboardFormatW(L"PNG");
    bool hasPng = pngFmt && IsClipboardFormatAvailable(pngFmt);

    if (hasFiles && pasteImageFiles(app)) return;
    if (hasText) {
        if (!OpenClipboard(app.hwnd)) return;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            const wchar_t* p = (const wchar_t*)GlobalLock(h);
            if (p) {
                std::wstring s = p;
                for (auto& c : s)
                    if (c == L'\r') c = L'\n';
                GlobalUnlock(h);
                CloseClipboard();
                edInsertText(app, s);
                return;
            }
        }
        CloseClipboard();
        return;
    }
    if (hasPng && pastePngStream(app)) return;
    if (hasDib && pasteDib(app)) return;
}

void edUndo(App& app) {
    if (!app.hasTab()) return;
    if (app.tab().ed.undoStep()) {
        app.tab().dirty = true;
        edEnsureCaretVisible(app);
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edRedo(App& app) {
    if (!app.hasTab()) return;
    if (app.tab().ed.redoStep()) {
        app.tab().dirty = true;
        edEnsureCaretVisible(app);
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edMouseDown(App& app, float x, float y, bool extend) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;
    if (x < srcGutterW(app)) {
        TCHAR unused = 0;
        (void)unused;
        return;
    }
    Pos p;
    srcHitTestPoint(app, x, y, &p);
    app.tab().ed.moveTo(p, extend);
    app.selecting = true;
    SetCapture(app.hwnd);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edMouseDrag(App& app, float x, float y) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;
    Pos p;
    srcHitTestPoint(app, x, y, &p);
    app.tab().ed.caret = p;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edKey(App& app, WPARAM vk, bool ctrl, bool shift) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;
    Tab& t = app.tab();
    Editor& ed = t.ed;
    bool changed = false;

    switch (vk) {
    case VK_LEFT:
        if (ctrl) {
            int col = ed.caret.col;
            const std::wstring& l = ed.line(ed.caret.line);
            while (col > 0 && iswspace(l[(size_t)col - 1])) --col;
            while (col > 0 && !iswspace(l[(size_t)col - 1])) --col;
            ed.caret.col = col;
            if (!shift) ed.anchor = ed.caret;
        } else {
            ed.moveBy(0, -1, shift);
        }
        break;
    case VK_RIGHT: {
        if (ctrl) {
            int col = ed.caret.col;
            const std::wstring& l = ed.line(ed.caret.line);
            while (col < (int)l.size() && !iswspace(l[(size_t)col])) ++col;
            while (col < (int)l.size() && iswspace(l[(size_t)col])) ++col;
            ed.caret.col = col;
            if (!shift) ed.anchor = ed.caret;
        } else {
            ed.moveBy(0, 1, shift);
        }
        break;
    }
    case VK_UP: ed.moveBy(-1, 0, shift); break;
    case VK_DOWN: ed.moveBy(1, 0, shift); break;
    case VK_HOME: ed.moveLineEdge(false, shift); break;
    case VK_END: ed.moveLineEdge(true, shift); break;
    case VK_PRIOR: {
        int step = (int)std::floor(app.viewH() / srcLineHeight()) - 1;
        ed.moveBy(-step, 0, shift);
        break;
    }
    case VK_NEXT: {
        int step = (int)std::floor(app.viewH() / srcLineHeight()) - 1;
        ed.moveBy(step, 0, shift);
        break;
    }
    case VK_BACK:
        if (ctrl) {
            Pos a = ed.caret;
            int col = a.col;
            const std::wstring& l = ed.line(a.line);
            while (col > 0 && iswspace(l[(size_t)col - 1])) --col;
            while (col > 0 && !iswspace(l[(size_t)col - 1])) --col;
            ed.anchor = Pos{a.line, col};
            ed.caret = a;
            ed.snapshot(GetTickCount64());
            ed.deleteSelection();
        } else {
            ed.backspace();
        }
        changed = true;
        break;
    case VK_DELETE:
        ed.deleteForward();
        changed = true;
        break;
    case VK_RETURN:
        ed.insertNewline(true, true);
        changed = true;
        break;
    case VK_TAB:
        ed.snapshot(GetTickCount64());
        ed.insert(L"    ");
        changed = true;
        break;
    default:
        break;
    }

    if (changed) {
        t.dirty = true;
        edEnsureCaretVisible(app);
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void applyFormat(App& app, int action) {
    if (!app.hasTab()) return;
    if (app.tab().mode != Mode::Source) switchMode(app, Mode::Source);
    Editor& ed = app.tab().ed;
    switch ((ChromeAction)action) {
    case ChromeAction::Bold: ed.wrapSelection(L"**", L"**"); break;
    case ChromeAction::Italic: ed.wrapSelection(L"*", L"*"); break;
    case ChromeAction::Strike: ed.wrapSelection(L"~~", L"~~"); break;
    case ChromeAction::Code: ed.wrapSelection(L"`", L"`"); break;
    case ChromeAction::H1: ed.prefixLines(L"# "); break;
    case ChromeAction::H2: ed.prefixLines(L"## "); break;
    case ChromeAction::H3: ed.prefixLines(L"### "); break;
    case ChromeAction::List: ed.prefixLines(L"- "); break;
    case ChromeAction::Quote: ed.prefixLines(L"> "); break;
    case ChromeAction::Link: ed.wrapSelection(L"[", L"](https://)"); break;
    case ChromeAction::Image: ed.wrapSelection(L"![", L"](image.png)"); break;
    default: return;
    }
    app.tab().dirty = true;
    edEnsureCaretVisible(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ find

namespace {

std::wstring lowerCopy(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower((wint_t)c);
    return r;
}

int countIn(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return 0;
    std::wstring h = lowerCopy(hay), n = lowerCopy(needle);
    int c = 0;
    for (size_t p = h.find(n); p != std::wstring::npos; p = h.find(n, p + n.size())) ++c;
    return c;
}

}  // namespace

void findRefresh(App& app) {
    app.findCount = 0;
    app.findIndex = -1;
    if (!app.hasTab() || app.findQuery.empty()) return;
    if (app.tab().mode == Mode::Source) {
        app.findCount = countIn(app.tab().ed.text(), app.findQuery);
    } else {
        int n = 0;
        for (const auto& b : app.tab().doc.blocks) {
            std::wstring text;
            for (const auto& r : b.runs) text += r.text;
            if (b.kind == BKind::Code) text = b.code;
            n += countIn(text, app.findQuery);
        }
        app.findCount = n;
    }
    if (app.findCount > 0 && app.findIndex < 0) app.findIndex = 0;
}

void findOpenBar(App& app) {
    app.findOpen = true;
    app.findQuery.clear();
    app.findCount = 0;
    app.findIndex = -1;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void findClose(App& app) {
    app.findOpen = false;
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void findType(App& app, wchar_t ch) {
    app.findQuery += ch;
    findRefresh(app);
    findJump(app);
}

void findBackspace(App& app) {
    if (app.findQuery.empty()) return;
    app.findQuery.pop_back();
    findRefresh(app);
    findJump(app);
}

void findJump(App& app) {
    if (!app.hasTab() || app.findQuery.empty()) return;
    Tab& t = app.tab();
    if (t.mode == Mode::Source) {
        std::wstring text = t.ed.text();
        std::wstring h = lowerCopy(text), n = lowerCopy(app.findQuery);
        if (n.empty()) return;
        size_t pos = h.find(n);
        if (pos == std::wstring::npos) return;
        Pos p = t.ed.indexToPos(pos);
        t.ed.moveTo(p, false);
        t.ed.firstLine = std::max(0, p.line - 3);
        edEnsureCaretVisible(app);
    } else {
        for (const auto& b : t.doc.blocks) {
            std::wstring text;
            for (const auto& r : b.runs) text += r.text;
            if (b.kind == BKind::Code) text = b.code;
            if (!countIn(text, app.findQuery)) continue;
            int idx = (int)(&b - &t.doc.blocks[0]);
            scrollToBlock(app, idx);
            break;
        }
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void findNext(App& app, int dir) {
    if (!app.hasTab() || app.findQuery.empty()) return;
    findRefresh(app);
    if (app.findCount <= 0) return;
    app.findIndex = (app.findIndex + dir + app.findCount) % app.findCount;
    findJump(app);
}

// ------------------------------------------------------------------ settings

void loadState(App& app) {
    std::ifstream f(statePath().c_str());
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("win=", 0) == 0) {
            int l, t, w, h;
            if (sscanf_s(line.c_str() + 4, "%d,%d,%d,%d", &l, &t, &w, &h) == 4 && w > 200 && h > 200)
                SetWindowPos(app.hwnd, nullptr, l, t, w, h, SWP_NOZORDER);
        } else if (line.rfind("outline=", 0) == 0) {
            app.showOutline = line[8] == '1';
        }
    }
}

void saveState(App& app) {
    RECT rc = {};
    GetWindowRect(app.hwnd, &rc);
    std::ofstream f(statePath().c_str(), std::ios::trunc);
    if (!f) return;
    f << "win=" << rc.left << "," << rc.top << "," << (rc.right - rc.left) << ","
      << (rc.bottom - rc.top) << "\n";
    f << "outline=" << (app.showOutline ? 1 : 0) << "\n";
}

// ------------------------------------------------------------------ shell

void registerShell(bool install) {
    const wchar_t* kProgId = L"MDReader.md";
    std::wstring exe = exePath();
    std::wstring quoted = L"\"" + exe + L"\" \"%1\"";
    if (install) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\MDReader.md", 0, nullptr, 0,
                            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            const wchar_t* desc = L"Markdown Document";
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)desc,
                           (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\MDReader.md\\DefaultIcon", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            std::wstring icon = exe + L",0";
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)icon.c_str(),
                           (DWORD)((icon.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\MDReader.md\\shell\\open\\command", 0, nullptr, 0,
                            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                           (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.md\\OpenWithProgids", 0, nullptr,
                            0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, kProgId, 0, REG_NONE, nullptr, 0);
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\Applications\\mdreader.exe\\shell\\open\\command", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                           (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
    } else {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\MDReader.md");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\mdreader.exe");
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.md\\OpenWithProgids", 0,
                          KEY_WRITE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, kProgId);
            RegCloseKey(k);
        }
    }
}

void openHelp(App& app) {
    std::wstring msg =
        L"快捷键\n\n"
        L"Ctrl+N  新建标签页\nCtrl+O  打开文件\nCtrl+S  保存\nCtrl+W  关闭标签页\n"
        L"Ctrl+Tab 下一个标签页\nCtrl+E  源码/预览切换\nCtrl+J  大纲\nCtrl+F  查找\n"
        L"Ctrl+B  加粗\nCtrl+I  斜体\nCtrl+K  链接\nCtrl+Z / Ctrl+Y  撤销/重做\n"
        L"Ctrl+A  全选\n\n"
        L"预览模式：滚轮、PageUp/PageDown、Home/End、空格\n"
        L"源码模式：方向键、Home/End、Ctrl+左右（按词移动）\n\n"
        L"启动参数：--dark / --light 强制主题，--register / --unregister 文件关联\n";
    MessageBoxW(app.hwnd, msg.c_str(), L"MD Reader", MB_OK);
}


void toggleTheme(App& app) {
    app.themeDark = !app.themeDark;
    app.forceTheme = app.themeDark ? 1 : 2;
    applyTheme(app);
    applyDarkTitleBar(app.hwnd, app.themeDark);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

// ------------------------------------------------------------------ window

namespace {

float dipX(App& app, int px) { return (float)px * 96.f / app.dpi; }
float dipY(App& app, int px) { return (float)px * 96.f / app.dpi; }

void positionIme(App& app) {
    if (!app.hasTab() || app.tab().mode != Mode::Source) return;
    HIMC imc = ImmGetContext(app.hwnd);
    if (!imc) return;
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    cf.ptCurrentPos.x = (LONG)(srcCaretX(app) * app.dpi / 96.f);
    cf.ptCurrentPos.y = (LONG)((srcCaretY(app) + srcLineHeight()) * app.dpi / 96.f);
    ImmSetCompositionWindow(imc, &cf);
    ImmReleaseContext(app.hwnd, imc);
}

void handleChrome(App& app, ChromeAction a, int index) {
    switch (a) {
    case ChromeAction::NewTab: newTab(app, L"", L""); break;
    case ChromeAction::CloseTab: closeTab(app, index); break;
    case ChromeAction::SelectTab: selectTab(app, index); break;
    case ChromeAction::Outline: toggleOutline(app); break;
    case ChromeAction::ToggleMode: toggleMode(app); break;
    case ChromeAction::Find: findOpenBar(app); break;
    case ChromeAction::FindNext: findNext(app, 1); break;
    case ChromeAction::FindPrev: findNext(app, -1); break;
    case ChromeAction::CloseFind: findClose(app); break;
    case ChromeAction::Open: {
        wchar_t buf[1024] = L"";
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = app.hwnd;
        ofn.lpstrFilter = L"Markdown\0*.md;*.markdown;*.mdown;*.mkd\0\u6240\u6709\u6587\u4ef6\0*.*\0";
        ofn.lpstrFile = buf;
        ofn.nMaxFile = 1024;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&ofn)) openPath(app, buf);
        break;
    }
    case ChromeAction::ToggleTheme: toggleTheme(app); break;
    case ChromeAction::Save: saveTab(app); break;
    case ChromeAction::Undo: edUndo(app); break;
    case ChromeAction::Redo: edRedo(app); break;
    default: applyFormat(app, (int)a); break;
    }
}

void onKeyDown(App& app, WPARAM vk, bool ctrl, bool shift, bool alt) {
    (void)alt;
    if (app.findOpen) {
        if (vk == VK_ESCAPE) { findClose(app); return; }
        if (vk == VK_RETURN) { findNext(app, shift ? -1 : 1); return; }
        if (vk == VK_BACK) { findBackspace(app); return; }
        if (vk == VK_F3) { findNext(app, shift ? -1 : 1); return; }
        return;
    }

    if (ctrl) {
        switch (vk) {
        case 'N': newTab(app, L"", L""); return;
        case 'O': handleChrome(app, ChromeAction::Open, -1); return;
        case 'S': saveTab(app); return;
        case 'W': if (app.hasTab()) closeTab(app, app.active); return;
        case 'E': toggleMode(app); return;
        case 'J': toggleOutline(app); return;
        case 'F': findOpenBar(app); return;
        case 'B': applyFormat(app, (int)ChromeAction::Bold); return;
        case 'I': applyFormat(app, (int)ChromeAction::Italic); return;
        case 'K': applyFormat(app, (int)ChromeAction::Link); return;
        case 'Z': edUndo(app); return;
        case 'Y': edRedo(app); return;
        case 'A':
            if (app.hasTab() && app.tab().mode == Mode::Source) { edSelectAll(app); return; }
            break;
        case 'C': edCopy(app, false); return;
        case 'X': edCopy(app, true); return;
        case 'V': edPaste(app); return;
        case VK_TAB:
            if (!app.tabs.empty()) selectTab(app, (app.active + 1) % (int)app.tabs.size());
            return;
        default: break;
        }
    }

    if (app.hasTab() && app.tab().mode == Mode::Source) {
        edKey(app, vk, ctrl, shift);
        positionIme(app);
        return;
    }

    if (!app.hasTab()) return;
    switch (vk) {
    case VK_NEXT: scrollTo(app, app.tab().scrollY + app.viewH() * 0.92f); break;
    case VK_PRIOR: scrollTo(app, app.tab().scrollY - app.viewH() * 0.92f); break;
    case VK_HOME: scrollTo(app, 0.f); break;
    case VK_END: scrollTo(app, app.tab().lay.contentH); break;
    case VK_DOWN: scrollTo(app, app.tab().scrollY + 48.f); break;
    case VK_UP: scrollTo(app, app.tab().scrollY - 48.f); break;
    case VK_SPACE: scrollTo(app, app.tab().scrollY + app.viewH() * 0.92f); break;
    default: break;
    }
}

}  // namespace

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
    case WM_NCCALCSIZE:
        if (!wp) {
            // Pre-layout pass: report the whole window as client area.
            RECT wr = {};
            GetWindowRect(h, &wr);
            *(RECT*)lp = wr;
            return 0;
        }
        if (wp) {
            auto* p = (NCCALCSIZE_PARAMS*)lp;
            if (IsZoomed(h)) {
                // A maximised window is inflated by the resize border; keep the
                // content inside the work area.
                int bx = GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                int by = GetSystemMetrics(SM_CYSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
                p->rgrc[0].left += bx;
                p->rgrc[0].right -= bx;
                p->rgrc[0].top += by;
                p->rgrc[0].bottom -= by;
            }
            return 0;  // client area covers the whole window
        }
        break;
    case WM_NCHITTEST: {
        if (!app) break;
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(h, &pt);
        RECT rc = {};
        GetClientRect(h, &rc);
        int b = (int)(6.f * app->dpi / 96.f);
        if (!IsZoomed(h)) {
            bool left = pt.x < b, right = pt.x >= rc.right - b;
            bool top = pt.y < b, bottom = pt.y >= rc.bottom - b;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        float x = (float)pt.x * 96.f / app->dpi;
        float y = (float)pt.y * 96.f / app->dpi;
        auto inside = [&](D2D1_RECT_F r) {
            return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
        };
        // The three caption buttons are handled as client clicks: with a custom
        // frame the system's HTMAXBUTTON path swallows the button-down message.
        if (inside(captionMinRect(*app)) || inside(captionMaxRect(*app)) ||
            inside(captionCloseRect(*app)))
            return HTCLIENT;
        if (captionIsEmpty(*app, x, y)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_NCACTIVATE:
        if (app) InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_SIZE:
        if (app) {
            app->maximized = IsZoomed(h) != 0;
            app->dpi = (float)dpiOf(h);
            updateViewport(*app);
            if (app->rt) {
                RECT rc = {};
                GetClientRect(h, &rc);
                app->rt->SetDpi(app->dpi, app->dpi);
                D2D1_SIZE_U sz = {(UINT)rc.right, (UINT)rc.bottom};
                app->rt->Resize(sz);
                if (app->hasTab() && app->tab().mode == Mode::Preview) reflowAnchor(*app);
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
                MessageBoxW(h, L"Direct2D \u521d\u59cb\u5316\u5931\u8d25", L"MD Reader", MB_ICONERROR);
                return 0;
            }
            app->dpi = (float)dpiOf(h);
            updateViewport(*app);
            createTarget(*app);
            DragAcceptFiles(h, TRUE);
            if (!app->pendingFile.empty())
                openPath(*app, app->pendingFile);
            else
                newTab(*app, L"", L"");
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        if (app) {
            if (app->hasTab() && app->tab().mode == Mode::Preview) ensureLayoutForView(*app);
            paint(*app);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER:
        if (!app) return 0;
        if (wp == 1) {
            if (app->hasTab() && app->tab().mode == Mode::Preview) layoutStep(*app, 1e9f, 8);
            if (!app->hasTab() || app->tab().lay.done) KillTimer(h, 1);
            InvalidateRect(h, nullptr, FALSE);
        } else if (wp == 2) {
            app->caretOn = !app->caretOn;
            if ((app->hasTab() && app->tab().mode == Mode::Source) || app->findOpen)
                InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_APP_IMAGE:
        if (app) {
            imagePoll(*app);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (app) {
            float x = dipX(*app, GET_X_LPARAM(lp));
            float y = dipY(*app, GET_Y_LPARAM(lp));
            auto in = [&](D2D1_RECT_F r) {
                return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
            };
            if (in(captionCloseRect(*app))) { PostMessageW(h, WM_CLOSE, 0, 0); return 0; }
            if (in(captionMaxRect(*app))) {
                ShowWindow(h, IsZoomed(h) ? SW_RESTORE : SW_MAXIMIZE);
                return 0;
            }
            if (in(captionMinRect(*app))) { ShowWindow(h, SW_MINIMIZE); return 0; }
            ChromeHit hit = chromeHitTest(*app, x, y);
            if (hit.action != ChromeAction::None) {
                handleChrome(*app, hit.action, hit.index);
                return 0;
            }
            if (app->findOpen) return 0;
            if (y < app->contentTop()) return 0;
            if (app->hasTab()) {
                if (app->tab().mode == Mode::Source) {
                    edMouseDown(*app, x, y, (GetKeyState(VK_SHIFT) & 0x8000) != 0);
                    positionIme(*app);
                } else {
                    int oi = outlineHit(*app, x, y);
                    if (oi >= 0) {
                        scrollToBlock(*app, app->tab().toc[(size_t)oi].blockIndex);
                        return 0;
                    }
                    ScrollGeom g = scrollGeom(*app);
                    if (g.visible && x >= g.trackX - 6.f && x <= g.trackX + g.trackW + 6.f) {
                        if (y >= g.thumbY && y <= g.thumbY + g.thumbH) {
                            app->dragGrab = y - g.thumbY;
                        } else {
                            app->dragGrab = g.thumbH * 0.5f;
                            scrollToThumb(*app, y - app->dragGrab);
                        }
                        app->dragThumb = true;
                        SetCapture(h);
                    }
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (app) {
            if (app->dragThumb) { app->dragThumb = false; ReleaseCapture(); }
            if (app->selecting) { app->selecting = false; ReleaseCapture(); }
        }
        return 0;
    case WM_MOUSEMOVE:
        if (app) {
            float x = dipX(*app, GET_X_LPARAM(lp));
            float y = dipY(*app, GET_Y_LPARAM(lp));
            if (app->dragThumb) {
                scrollToThumb(*app, y - app->dragGrab);
                return 0;
            }
            if (app->selecting) {
                edMouseDrag(*app, x, y);
                return 0;
            }
            int nc = 0;
            auto inBtn = [&](D2D1_RECT_F r) {
                return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
            };
            if (inBtn(captionMinRect(*app))) nc = 1;
            else if (inBtn(captionMaxRect(*app))) nc = 2;
            else if (inBtn(captionCloseRect(*app))) nc = 3;
            if (nc != app->ncHover) {
                app->ncHover = nc;
                InvalidateRect(h, nullptr, FALSE);
            }
            int hover = toolButtonIndexAt(*app, x, y);
            int closeTab = -1;
            if (y < app->tabStripH) {
                for (int i = 0; i < (int)app->tabs.size(); ++i) {
                    D2D1_RECT_F c = closeTabRect(*app, i);
                    if (x >= c.left && x <= c.right && y >= c.top && y <= c.bottom) closeTab = i;
                }
            }
            if (hover != app->hoverBtn || closeTab != app->hoverCloseTab) {
                app->hoverBtn = hover;
                app->hoverCloseTab = closeTab;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (app && app->hasTab()) {
            POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(h, &pt);
            float x = dipX(*app, pt.x);
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (app->showOutline && x >= app->contentRight()) {
                scrollOutline(*app, -delta / 120.f * 78.f);
                return 0;
            }
            if (app->tab().mode == Mode::Source) {
                if (GetKeyState(VK_SHIFT) & 0x8000) {
                    app->tab().ed.scrollX =
                        std::max(0.f, app->tab().ed.scrollX - delta / 120.f * 60.f);
                    InvalidateRect(h, nullptr, FALSE);
                } else {
                    edMoveCaretLine(*app, -(delta / 120) * 3);
                }
                return 0;
            }
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == 0) lines = 3;
            scrollTo(*app, app->tab().scrollY - (float)delta / 120.f * lines * 22.f);
        }
        return 0;
    case WM_CHAR:
        if (app) {
            wchar_t c = (wchar_t)wp;
            if (app->findOpen) {
                if (c >= 32 && c != 127) findType(*app, c);
                return 0;
            }
            if (app->hasTab() && app->tab().mode == Mode::Source) {
                // Enter and Tab arrive through WM_KEYDOWN; ignore the WM_CHAR copies.
                if (c == L'\r' || c == L'\t') return 0;
                if (c >= 32 && c != 127) {
                    wchar_t buf[2] = {c, 0};
                    edInsertText(*app, buf);
                }
                return 0;
            }
        }
        return 0;
    case WM_IME_STARTCOMPOSITION:
        if (app) positionIme(*app);
        break;
    case WM_IME_COMPOSITION:
        if (app && (lp & GCS_RESULTSTR)) {
            HIMC imc = ImmGetContext(h);
            if (imc) {
                LONG bytes = ImmGetCompositionStringW(imc, GCS_RESULTSTR, nullptr, 0);
                if (bytes > 0) {
                    std::wstring s((size_t)(bytes / sizeof(wchar_t)), L'\0');
                    ImmGetCompositionStringW(imc, GCS_RESULTSTR, &s[0], bytes);
                    if (app->findOpen) {
                        for (wchar_t c : s) findType(*app, c);
                    } else {
                        edInsertText(*app, s);
                    }
                }
                ImmReleaseContext(h, imc);
            }
        }
        positionIme(*app);
        return 0;
    case WM_KEYDOWN:
        if (app) {
            bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
            onKeyDown(*app, wp, ctrl, shift, alt);
        }
        return 0;
    case WM_DROPFILES: {
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileW(hd, 0xFFFFFFFF, nullptr, 0);
        if (app) {
            for (UINT i = 0; i < n; ++i) {
                wchar_t path[1024] = L"";
                DragQueryFileW(hd, i, path, 1024);
                openPath(*app, path);
            }
        }
        DragFinish(hd);
        return 0;
    }
    case WM_COPYDATA: {
        auto* cds = (COPYDATASTRUCT*)lp;
        if (app && cds && cds->dwData == 1 && cds->lpData)
            openPath(*app, (const wchar_t*)cds->lpData);
        return 1;
    }
    case WM_SETTINGCHANGE:
        if (app && !app->forceTheme && lp && wcscmp((const wchar_t*)lp, L"ImmersiveColorSet") == 0) {
            bool dark = systemDark();
            if (dark != app->themeDark) {
                app->themeDark = dark;
                applyTheme(*app);
                applyDarkTitleBar(h, dark);
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
    typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
    if (auto fn = (SetDpiCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                            "SetProcessDpiAwarenessContext"))
        fn((HANDLE)-4);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    App app;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring openArg;
    bool hasDark = false, hasLight = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring a = argv[i];
            if (a == L"--dark") hasDark = true;
            else if (a == L"--light") hasLight = true;
            else if (a == L"--register") { registerShell(true); LocalFree(argv); return 0; }
            else if (a == L"--unregister") { registerShell(false); LocalFree(argv); return 0; }
            else if (!a.empty() && a[0] != L'-') openArg = a;
        }
        LocalFree(argv);
    }
    app.themeDark = hasDark ? true : (hasLight ? false : systemDark());

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MDReader.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"MDReaderWindow", nullptr);
        if (prev) {
            if (!openArg.empty()) {
                COPYDATASTRUCT cds = {};
                cds.dwData = 1;
                cds.cbData = (DWORD)((openArg.size() + 1) * sizeof(wchar_t));
                cds.lpData = (void*)openArg.c_str();
                SendMessageW(prev, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"MDReaderWindow";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"MDReaderWindow", L"MD Reader", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 1100, 900, nullptr, nullptr, hInst,
                                &app);
    if (!hwnd) return 1;

    app.pendingFile = openArg;
    applyWindowShape(hwnd);
    applyDarkTitleBar(hwnd, app.themeDark);
    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);
    loadState(app);
    PostMessageW(hwnd, WM_APP_INIT, 0, 0);
    SetTimer(hwnd, 2, 530, nullptr);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    saveState(app);
    if (mutex) CloseHandle(mutex);
    return 0;
}
