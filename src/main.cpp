#include "app.h"
#include "chrome.h"
#include "files.h"
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
#include <iomanip>

float srcGutterW(const App& app);
void srcHitTestPoint(App& app, float x, float y, Pos* p);
float srcCaretX(App& app);
float srcCaretY(App& app);
float srcLineHeight();

namespace {

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
    if (dir.empty()) return {};
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

std::wstring fileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void setTitle(App& app) {
    // The caption is custom-drawn (tabs replace the title text); the window text
    // only feeds the taskbar and Alt+Tab.
    (void)app;
    SetWindowTextW(app.hwnd, L"MdReader");
}

}  // namespace

// ------------------------------------------------------------------ tabs

void newTab(App& app, const std::wstring& path, const std::wstring& text) {
    Tab t;
    t.path = path;
    t.name = path.empty() ? std::wstring() : fileNameOf(path);
    if (app.hasTab()) finishPreviewEdit(app);
    t.ed.setText(text);
    parseText(text, t.doc);
    buildToc(t.doc, t.toc);
    app.tabs.push_back(std::move(t));
    app.active = (int)app.tabs.size() - 1;
    app.outlineScroll = 0.f;
    layoutReset(app);
    ensureLayoutForView(app);
    if (app.findOpen) { findRefresh(app); findJump(app); }
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

namespace {

void checkExternalFile(App& app, int index) {
    Tab& tab = app.tabs[(size_t)index];
    if (tab.path.empty() || (index == app.active && (app.composing || app.selecting || app.dragThumb))) return;
    const auto disk = fileStamp(tab.path);
    if (!disk) return;
    const bool changed = *disk != tab.fileStamp;
    if (changed != tab.diskChanged) {
        tab.diskChanged = changed;
        if (index == app.active) InvalidateRect(app.hwnd, nullptr, FALSE);
    }
    if (!changed || tab.dirty || !disk->exists) return;

    std::wstring text;
    bool bom = false;
    FileStamp loaded;
    if (!readDocument(tab.path, text, bom, loaded)) return;
    if (documentText(tab) != (bom ? L"\uFEFF" + text : text)) reloadTabFromDisk(app, index, text);
    setEncoding(tab, text, bom);
    tab.fileStamp = loaded;
    tab.diskChanged = false;
    if (index == app.active) InvalidateRect(app.hwnd, nullptr, FALSE);
}

bool openDocument(App& app, const std::wstring& pathIn, bool showError) {
    std::wstring path = pathIn;
    DWORD length = GetFullPathNameW(pathIn.c_str(), 0, nullptr, nullptr);
    if (length) {
        std::wstring full(length, L'\0');
        DWORD used = GetFullPathNameW(pathIn.c_str(), length, full.data(), nullptr);
        if (used && used < length) { full.resize(used); path = std::move(full); }
    }
    for (int i = 0; i < (int)app.tabs.size(); ++i) {
        if (_wcsicmp(app.tabs[(size_t)i].path.c_str(), path.c_str()) == 0) {
            selectTab(app, i);
            return true;
        }
    }
    std::wstring text;
    bool bom = false;
    FileStamp stamp;
    if (!readDocument(path, text, bom, stamp)) {
        if (showError)
            MessageBoxW(app.hwnd, L"无法读取文件，或文件不是 UTF-8 文本。", L"MdReader", MB_ICONWARNING);
        return false;
    }
    newTab(app, path, text);
    app.tab().fileStamp = stamp;
    setEncoding(app.tab(), text, bom);
    app.lastFile = path;
    if (!app.findOpen) restoreReadingPosition(app);
    return true;
}

}  // namespace

bool openPath(App& app, const std::wstring& path) { return openDocument(app, path, true); }

void selectTab(App& app, int index) {
    if (index < 0 || index >= (int)app.tabs.size()) return;
    if (app.hasTab() && app.active != index) finishPreviewEdit(app);
    app.active = index;
    if (!app.tab().path.empty()) app.lastFile = app.tab().path;
    checkExternalFile(app, index);
    app.pendingSurrogate = 0;
    if (!app.findOpen) app.matches.clear();
    if (app.findOpen) { findRefresh(app); findJump(app); }
    app.outlineScroll = 0.f;
    ensureLayoutForView(app);
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

static bool confirmClose(App& app, int index) {
    if (app.tabs[(size_t)index].dirty) {
        selectTab(app, index);
        std::wstring name = app.tab().name.empty() ? L"无标题" : app.tab().name;
        std::wstring msg = name + L" 尚未保存，要保存吗？";
        int r = MessageBoxW(app.hwnd, msg.c_str(), L"MdReader", MB_YESNOCANCEL | MB_ICONWARNING);
        if (r == IDYES) return saveTab(app);
        if (r != IDNO) return false;
    }
    return true;
}

void closeTab(App& app, int index) {
    if (index < 0 || index >= (int)app.tabs.size() || !confirmClose(app, index)) return;
    if (app.active == index) finishPreviewEdit(app);
    rememberReadingPosition(app, app.tabs[(size_t)index]);
    app.tabs.erase(app.tabs.begin() + index);
    if (app.tabs.empty()) {
        app.active = -1;
    } else if (app.active >= (int)app.tabs.size()) {
        app.active = (int)app.tabs.size() - 1;
    } else if (app.active > index) {
        app.active--;
    }
    if (app.hasTab() && !app.tab().path.empty()) app.lastFile = app.tab().path;
    if (app.findOpen) { findRefresh(app); findJump(app); }
    ensureLayoutForView(app);
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void switchMode(App& app, Mode mode) {
    if (!app.hasTab() || app.tab().mode == mode) return;
    Tab& t = app.tab();
    if (mode == Mode::Preview) {
        t.mode = mode;
        syncEditorToPreview(app);
    } else {
        finishPreviewEdit(app);
        const int block = t.pendingBlock >= 0 ? t.pendingBlock : blockAtScroll(app);
        if (!t.doc.blocks.empty())
            t.ed.firstLine = std::max(0, t.doc.blocks[(size_t)block].firstLine);
        if (t.ed.caret.line < t.ed.firstLine ||
            t.ed.caret.line >= t.ed.firstLine + (int)(app.viewH() / srcLineHeight()))
            t.ed.moveTo({t.ed.firstLine, 0}, false);
        t.mode = mode;
    }
    if (app.findOpen) { findRefresh(app); findJump(app); }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void toggleMode(App& app) {
    if (!app.hasTab()) return;
    switchMode(app, app.tab().mode == Mode::Preview ? Mode::Source : Mode::Preview);
}

bool saveTab(App& app) {
    if (!app.hasTab()) return true;
    Tab& t = app.tab();
    if (!t.dirty && !t.path.empty()) { checkExternalFile(app, app.active); return true; }
    if (t.path.empty()) return saveTabAs(app);
    const auto disk = fileStamp(t.path);
    if (!disk) {
        MessageBoxW(app.hwnd, L"无法确认文件当前状态，保存未完成。", L"MdReader", MB_ICONWARNING);
        return false;
    }
    if (*disk != t.fileStamp) {
        const int answer = MessageBoxW(app.hwnd,
            L"磁盘上的文件已被其他程序修改或移走。\n\n是：用当前内容覆盖原路径\n否：另存为\n取消：继续编辑",
            L"MdReader", MB_YESNOCANCEL | MB_DEFBUTTON3 | MB_ICONWARNING);
        if (answer == IDNO) return saveTabAs(app);
        if (answer != IDYES) return false;
    }
    if (!writeAllText(t.path, documentText(t), &*disk, &t.fileStamp)) {
        MessageBoxW(app.hwnd, L"保存失败", L"MdReader", MB_ICONWARNING);
        return false;
    }
    t.dirty = false;
    t.diskChanged = false;
    t.ed.modified = false;
    setTitle(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

bool saveTabAs(App& app) {
    if (!app.hasTab()) return false;
    wchar_t buf[1024] = L"";
    Tab& t = app.tab();
    wcsncpy_s(buf, t.name.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = app.hwnd;
    ofn.lpstrFilter = L"Markdown\0*.md;*.markdown\0所有文件\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 1024;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"md";
    if (!GetSaveFileNameW(&ofn)) return false;
    if (!writeAllText(buf, documentText(t), nullptr, &t.fileStamp)) {
        MessageBoxW(app.hwnd, L"保存失败", L"MdReader", MB_ICONWARNING);
        return false;
    }
    t.path = buf;
    t.name = fileNameOf(buf);
    app.lastFile = t.path;
    t.diskChanged = false;
    t.dirty = t.ed.modified = false;
    reflowAnchor(app);
    InvalidateRect(app.hwnd, nullptr, FALSE);
    return true;
}

// ------------------------------------------------------------------ editor input

static void prepareEditor(App& app) {
    Tab& tab = app.tab();
    if (tab.mode == Mode::Preview && tab.editFirst < 0) {
        if (tab.ed.hasSelection()) return;
        const int block = blockAtScroll(app);
        if (!tab.doc.blocks.empty())
            tab.ed.moveTo({tab.doc.blocks[(size_t)block].firstLine, 0}, false);
        startPreviewEdit(app, tab.ed.caret, false);
    }
}

void edInsertText(App& app, const std::wstring& s) {
    if (!app.hasTab()) return;
    if (s.empty()) return;
    prepareEditor(app);
    Tab& tab = app.tab();
    tab.ed.snapshot(GetTickCount64());
    tab.ed.insert(s);
    updatePreviewEditor(app, true);
}

void edSelectAll(App& app) {
    if (!app.hasTab()) return;
    prepareEditor(app);
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
    bool copied = false;
    if (OpenClipboard(app.hwnd)) {
        EmptyClipboard();
        size_t bytes = (sel.size() + 1) * sizeof(wchar_t);
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (h) {
            void* p = GlobalLock(h);
            if (p) {
                memcpy(p, sel.c_str(), bytes);
                GlobalUnlock(h);
                copied = SetClipboardData(CF_UNICODETEXT, h) != nullptr;
            }
            if (!copied) GlobalFree(h);
        }
        CloseClipboard();
    }
    if (cut && copied) {
        ed.snapshot(GetTickCount64());
        ed.deleteSelection();
        updatePreviewEditor(app, true);
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
                    L"MdReader", MB_ICONINFORMATION);
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
    prepareEditor(app);
    std::wstring line = L"![](" + relLink + L")";
    if (app.tab().ed.caret.col > 0) line.insert(0, 1, L'\n');
    edInsertText(app, line + L'\n');
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
    if (!app.hasTab()) return;

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
    Tab& tab = app.tab();
    if (!tab.ed.undoStep()) return;
    if (tab.mode == Mode::Preview) tab.editFirst = tab.editLast = -1;
    updatePreviewEditor(app, true);
}

void edRedo(App& app) {
    if (!app.hasTab()) return;
    Tab& tab = app.tab();
    if (!tab.ed.redoStep()) return;
    if (tab.mode == Mode::Preview) tab.editFirst = tab.editLast = -1;
    updatePreviewEditor(app, true);
}

void edMouseDown(App& app, float x, float y, bool extend) {
    if (!app.hasTab()) return;
    if (app.tab().mode == Mode::Preview) {
        if (!previewMouseDown(app, x, y, extend)) return;
    } else {
        if (x < srcGutterW(app)) return;
        Pos p;
        srcHitTestPoint(app, x, y, &p);
        app.tab().ed.moveTo(p, extend);
    }
    app.selecting = true;
    SetCapture(app.hwnd);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edMouseDrag(App& app, float x, float y) {
    if (!app.hasTab()) return;
    Pos p;
    if (app.tab().mode == Mode::Preview) {
        if (!previewHitEditor(app, x, y, p)) return;
    } else {
        srcHitTestPoint(app, x, y, &p);
    }
    app.tab().ed.moveTo(p, true);
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void edKey(App& app, WPARAM vk, bool ctrl, bool shift) {
    if (!app.hasTab()) return;
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
            ed.moveTo({ed.caret.line, col}, shift);
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
            ed.moveTo({ed.caret.line, col}, shift);
        } else {
            ed.moveBy(0, 1, shift);
        }
        break;
    }
    case VK_UP: ed.moveBy(-1, 0, shift); break;
    case VK_DOWN: ed.moveBy(1, 0, shift); break;
    case VK_HOME:
    case VK_END:
        if (ctrl) ed.moveDocEdge(vk == VK_END, shift);
        else ed.moveLineEdge(vk == VK_END, shift);
        break;
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
        if (!ed.hasSelection() && ed.caret == Pos{}) break;
        if (ctrl && !ed.hasSelection() && ed.caret.col > 0) {
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
        if (!ed.hasSelection() && ed.caret.line == ed.lineCount() - 1 &&
            ed.caret.col == (int)ed.lines.back().size()) break;
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

    updatePreviewEditor(app, changed);
}

void applyFormat(App& app, int action) {
    if (!app.hasTab()) return;
    prepareEditor(app);
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
    updatePreviewEditor(app, true);
}

// ------------------------------------------------------------------ find

namespace {

std::wstring lowerCopy(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower((wint_t)c);
    return r;
}

void collectMatches(App& app, const std::wstring& text, const std::wstring& needle, int block,
                    int cell = -1) {
    std::wstring folded = lowerCopy(text);
    for (size_t pos = folded.find(needle); pos != std::wstring::npos;
         pos = folded.find(needle, pos + needle.size()))
        app.matches.push_back({pos, block, cell});
}

}  // namespace

void findRefresh(App& app) {
    app.matches.clear();
    if (app.hasTab() && !app.findQuery.empty()) {
        const Tab& tab = app.tab();
        const std::wstring needle = lowerCopy(app.findQuery);
        if (tab.mode == Mode::Source) {
            collectMatches(app, tab.ed.text(), needle, -1);
        } else {
            for (int i = 0; i < (int)tab.doc.blocks.size(); ++i) {
                const Block& block = tab.doc.blocks[(size_t)i];
                if (block.kind == BKind::Table) {
                    int cols = 0;
                    for (const auto& row : block.cells) cols = std::max(cols, (int)row.size());
                    int rowIndex = 0;
                    for (const auto& row : block.cells) {
                        for (int col = 0; col < (int)row.size(); ++col) {
                            std::wstring text;
                            for (const auto& run : row[(size_t)col]) text += run.text;
                            collectMatches(app, text, needle, i, rowIndex * cols + col);
                        }
                        ++rowIndex;
                    }
                } else {
                    std::wstring text = block.code;
                    for (const auto& run : block.runs) text += run.text;
                    collectMatches(app, text, needle, i);
                }
            }
        }
    }
    app.findCount = (int)app.matches.size();
    app.findIndex = app.matches.empty() ? -1 : 0;
}

void findOpenBar(App& app) {
    if (app.hasTab()) finishPreviewEdit(app);
    app.findOpen = true;
    if (app.hasTab() && app.tab().mode == Mode::Source) {
        const auto selected = app.tab().ed.selectedText();
        if (!selected.empty() && selected.find(L'\n') == std::wstring::npos)
            app.findQuery = selected;
    }
    findRefresh(app);
    findJump(app);
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
    if (IS_LOW_SURROGATE(app.findQuery.back()) && app.findQuery.size() > 1 &&
        IS_HIGH_SURROGATE(app.findQuery[app.findQuery.size() - 2]))
        app.findQuery.pop_back();
    app.findQuery.pop_back();
    findRefresh(app);
    findJump(app);
}

void findJump(App& app) {
    if (!app.hasTab() || app.findIndex < 0 || app.findIndex >= app.findCount) return;
    Tab& tab = app.tab();
    const auto& match = app.matches[(size_t)app.findIndex];
    if (tab.mode == Mode::Source) {
        const Pos start = tab.ed.indexToPos(match.offset);
        tab.ed.moveTo(start, false);
        tab.ed.moveTo(tab.ed.indexToPos(match.offset + app.findQuery.size()), true);
        tab.ed.firstLine = std::max(0, start.line - 3);
        edEnsureCaretVisible(app);
    } else {
        scrollToBlock(app, match.block);
    }
    InvalidateRect(app.hwnd, nullptr, FALSE);
}

void findNext(App& app, int dir) {
    if (!app.hasTab() || app.findQuery.empty()) return;
    if (!app.findOpen) {
        finishPreviewEdit(app);
        if (app.matches.empty()) findRefresh(app);
        app.findOpen = true;
    }
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
        } else if (line.rfind("last=", 0) == 0) {
            std::istringstream record(line.substr(5));
            std::string path;
            if (record >> std::quoted(path)) app.lastFile = widen(path.data(), path.size());
        } else if (line.rfind("read=", 0) == 0 && app.readingPositions.size() < 100) {
            std::istringstream record(line.substr(5));
            std::string path;
            int row;
            float offset;
            if (record >> row >> offset >> std::quoted(path) &&
                row >= 0 && std::isfinite(offset) && !path.empty())
                app.readingPositions.push_back({widen(path.data(), path.size()), row, offset});
        }
    }
    if (app.lastFile.empty() && !app.readingPositions.empty()) app.lastFile = app.readingPositions.back().path;
}

void saveState(App& app) {
    RECT rc = {};
    GetWindowRect(app.hwnd, &rc);
    std::wostringstream f;
    f << "win=" << rc.left << "," << rc.top << "," << (rc.right - rc.left) << ","
      << (rc.bottom - rc.top) << "\n";
    f << "outline=" << (app.showOutline ? 1 : 0) << "\n";
    f << "last=" << std::quoted(app.lastFile) << '\n';
    for (const auto& pos : app.readingPositions)
        f << "read=" << pos.line << ' ' << pos.offset << ' ' << std::quoted(pos.path) << '\n';
    const std::wstring path = statePath();
    if (!path.empty()) writeAllText(path, f.str());
}

// ------------------------------------------------------------------ shell

void registerShell(bool install) {
    const wchar_t* kProgId = L"MdReader.md";
    std::wstring exe = exePath();
    std::wstring quoted = L"\"" + exe + L"\" \"%1\"";
    if (install) {
        // Drop keys written by the previous product name.
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\MDReader.md");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\mdreader.exe");

        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\MdReader.md", 0, nullptr, 0,
                            KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            const wchar_t* desc = L"Markdown Document";
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)desc,
                           (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\MdReader.md\\DefaultIcon", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            std::wstring icon = exe + L",0";
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)icon.c_str(),
                           (DWORD)((icon.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\MdReader.md\\shell\\open\\command", 0, nullptr, 0,
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
                            L"Software\\Classes\\Applications\\MdReader.exe\\shell\\open\\command", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            RegSetValueExW(k, nullptr, 0, REG_SZ, (const BYTE*)quoted.c_str(),
                           (DWORD)((quoted.size() + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        // Name shown in "Open with" and in Settings > Default apps.
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\Applications\\MdReader.exe\\Application", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            const wchar_t* friendly = L"MdReader";
            RegSetValueExW(k, L"FriendlyAppName", 0, REG_SZ, (const BYTE*)friendly,
                           (DWORD)((wcslen(friendly) + 1) * sizeof(wchar_t)));
            const wchar_t* appName = L"MdReader.exe";
            RegSetValueExW(k, L"ApplicationName", 0, REG_SZ, (const BYTE*)appName,
                           (DWORD)((wcslen(appName) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        // Also list it in the classic "Open with" menu for .md.
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.md\\OpenWithList", 0,
                            nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            const wchar_t* exeName = L"MdReader.exe";
            RegSetValueExW(k, L"a", 0, REG_SZ, (const BYTE*)exeName,
                           (DWORD)((wcslen(exeName) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
    } else {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\MdReader.md");
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications\\MdReader.exe");
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
    MessageBoxW(app.hwnd, msg.c_str(), L"MdReader", MB_OK);
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
    if (!app.hasTab()) return;
    HIMC imc = ImmGetContext(app.hwnd);
    if (!imc) return;
    COMPOSITIONFORM cf = {};
    cf.dwStyle = CFS_POINT;
    float x = 0.f, y = app.contentTop(), height = srcLineHeight();
    if (app.findOpen) {
        const auto bar = findBarRect(app);
        x = bar.left;
        y = bar.top;
        height = bar.bottom - bar.top;
    } else if (app.tab().mode == Mode::Source) {
        x = srcCaretX(app);
        y = srcCaretY(app);
    } else {
        previewCaretPoint(app, x, y, height);
    }
    cf.ptCurrentPos.x = (LONG)(x * app.dpi / 96.f);
    cf.ptCurrentPos.y = (LONG)((y + height) * app.dpi / 96.f);
    ImmSetCompositionWindow(imc, &cf);
    ImmReleaseContext(app.hwnd, imc);
}

void handleChrome(App& app, ChromeAction a, int index) {
    switch (a) {
    case ChromeAction::NewTab: newTab(app, L"", L""); break;
    case ChromeAction::TabMenu: {
        HMENU menu = CreatePopupMenu();
        if (!menu) break;
        for (size_t i = 0; i < app.tabs.size(); ++i) {
            const Tab& tab = app.tabs[i];
            std::wstring label = tab.name.empty() ? L"无标题" : tab.name;
            for (size_t at = label.find(L'&'); at != std::wstring::npos; at = label.find(L'&', at + 2))
                label.insert(at, 1, L'&');
            if (tab.dirty) label += L" *";
            AppendMenuW(menu, MF_STRING | ((int)i == app.active ? MF_CHECKED : 0),
                        (UINT_PTR)i + 1, label.c_str());
        }
        const auto bounds = tabOverflowRect(app);
        POINT point = {(LONG)(bounds.left * app.dpi / 96.f), (LONG)(bounds.bottom * app.dpi / 96.f)};
        ClientToScreen(app.hwnd, &point);
        const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                             point.x, point.y, 0, app.hwnd, nullptr);
        DestroyMenu(menu);
        if (selected) selectTab(app, (int)selected - 1);
        break;
    }
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
        if (ctrl && vk == 'S') { saveTab(app); return; }
        if (vk == VK_ESCAPE) { findClose(app); return; }
        if (vk == VK_RETURN) { findNext(app, shift ? -1 : 1); return; }
        if (vk == VK_BACK) { findBackspace(app); return; }
        if (vk == VK_F3) { findNext(app, shift ? -1 : 1); return; }
        return;
    }

    if (vk == VK_F3) { findNext(app, shift ? -1 : 1); return; }

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
        case 'A': if (app.hasTab()) edSelectAll(app); return;
        case 'C': edCopy(app, false); return;
        case 'X': edCopy(app, true); return;
        case 'V': edPaste(app); return;
        case VK_TAB:
            if (!app.tabs.empty()) selectTab(app, (app.active + 1) % (int)app.tabs.size());
            return;
        default: break;
        }
    }

    if (app.hasTab() && (app.tab().mode == Mode::Source || app.tab().editFirst >= 0 ||
                         (app.tab().doc.blocks.empty() && vk == VK_RETURN))) {
        if (vk == VK_ESCAPE && app.tab().mode == Mode::Preview) {
            finishPreviewEdit(app);
            return;
        }
        prepareEditor(app);
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
    case WM_GETMINMAXINFO: {
        auto* info = (MINMAXINFO*)lp;
        const float scale = (app ? app->dpi : (float)dpiOf(h)) / 96.f;
        info->ptMinTrackSize.x = (LONG)(400.f * scale);
        info->ptMinTrackSize.y = (LONG)(235.f * scale);
        return 0;
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
                MessageBoxW(h, L"Direct2D \u521d\u59cb\u5316\u5931\u8d25", L"MdReader", MB_ICONERROR);
                return 0;
            }
            app->dpi = (float)dpiOf(h);
            updateViewport(*app);
            createTarget(*app);
            DragAcceptFiles(h, TRUE);
            if (!app->pendingFile.empty())
                openPath(*app, app->pendingFile);
            else if (!app->lastFile.empty())
                openDocument(*app, app->lastFile, false);
            if (!app->hasTab()) newTab(*app, L"", L"");
            SetTimer(h, 3, 1000, nullptr);
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
            if (app->hasTab() && app->tab().mode == Mode::Preview) {
                layoutStep(*app, 1e9f, 8);
                if (app->tab().lay.edit.block < 0) layoutPreviewEditor(*app);
                resolvePendingScroll(*app);
                revealPreviewCaret(*app);
            }
            if (!app->hasTab() || app->tab().mode != Mode::Preview || app->tab().lay.done)
                KillTimer(h, 1);
            InvalidateRect(h, nullptr, FALSE);
        } else if (wp == 2) {
            app->caretOn = !app->caretOn;
            if ((app->hasTab() && (app->tab().mode == Mode::Source || app->tab().editFirst >= 0)) || app->findOpen)
                InvalidateRect(h, nullptr, FALSE);
        } else if (wp == 3 && !app->closing) {
            for (int i = 0; i < (int)app->tabs.size(); ++i) checkExternalFile(*app, i);
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
                    if (app->showOutline && x >= app->contentRight()) {
                        finishPreviewEdit(*app);
                        int oi = outlineHit(*app, x, y);
                        if (oi >= 0) scrollToBlock(*app, app->tab().toc[(size_t)oi].blockIndex);
                        return 0;
                    }
                    if (y >= app->vh - app->statusH) { finishPreviewEdit(*app); return 0; }
                    const bool extend = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                    const bool editLink = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                    const int link = previewLinkAt(*app, x, y);
                    if (link >= 0 && !extend && !editLink) {
                        openPreviewLink(*app, link);
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
                    } else {
                        edMouseDown(*app, x, y, extend);
                        positionIme(*app);
                    }
                }
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (app && LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(h, &pt);
            const float x = dipX(*app, pt.x), y = dipY(*app, pt.y);
            if (previewLinkAt(*app, x, y) >= 0) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
                return TRUE;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (app) app->dragThumb = app->selecting = false;
        return 0;
    case WM_KILLFOCUS:
        if (app && app->hasTab()) {
            app->composing = false;
            app->pendingSurrogate = 0;
            finishPreviewEdit(*app);
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
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
            const auto tabHit = chromeHitTest(*app, x, y);
            const int closeTab = tabHit.action == ChromeAction::CloseTab ? tabHit.index : -1;
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
            if (app->hasTab()) {
                // Enter and Tab arrive through WM_KEYDOWN; ignore the WM_CHAR copies.
                if (c == L'\r' || c == L'\t') return 0;
                if (c == L' ' && app->tab().mode == Mode::Preview && app->tab().editFirst < 0 &&
                    !app->tab().doc.blocks.empty()) return 0;
                if (IS_HIGH_SURROGATE(c)) {
                    app->pendingSurrogate = c;
                } else if (c >= 32 && c != 127) {
                    std::wstring text;
                    if (IS_LOW_SURROGATE(c)) {
                        if (!app->pendingSurrogate) return 0;
                        text += app->pendingSurrogate;
                    }
                    app->pendingSurrogate = 0;
                    text += c;
                    edInsertText(*app, text);
                }
                return 0;
            }
        }
        return 0;
    case WM_IME_STARTCOMPOSITION:
        if (app) { app->composing = true; positionIme(*app); }
        break;
    case WM_IME_ENDCOMPOSITION:
        if (app) app->composing = false;
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
                        app->findQuery += s;
                        findRefresh(*app);
                        findJump(*app);
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
    case WM_CLOSE:
        if (app) {
            if (app->closing) return 0;
            app->closing = true;
            const int previousActive = app->active;
            const std::wstring previousLast = app->lastFile;
            for (int i = 0; i < (int)app->tabs.size(); ++i)
                if (!confirmClose(*app, i)) { app->closing = false; return 0; }
            if (app->hasTab()) finishPreviewEdit(*app);
            for (const auto& tab : app->tabs) rememberReadingPosition(*app, tab);
            if (previousActive >= 0 && !app->tabs[(size_t)previousActive].path.empty())
                app->lastFile = app->tabs[(size_t)previousActive].path;
            else if (!previousLast.empty()) app->lastFile = previousLast;
            saveState(*app);
        }
        DestroyWindow(h);
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
    app.forceTheme = hasDark ? 1 : (hasLight ? 2 : 0);
    app.themeDark = hasDark || (!hasLight && systemDark());

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MDReader.SingleInstance");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND prev = FindWindowW(L"MdReaderWindow", nullptr);
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
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"MdReaderWindow";
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, L"MdReaderWindow", L"MdReader", WS_OVERLAPPEDWINDOW,
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
    if (mutex) CloseHandle(mutex);
    return 0;
}
