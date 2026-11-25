#include "files.h"

namespace {

std::optional<FileStamp> stampFromHandle(HANDLE file) {
    BY_HANDLE_FILE_INFORMATION info = {};
    if (!GetFileInformationByHandle(file, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return std::nullopt;
    auto combine = [](DWORD high, DWORD low) { return (uint64_t(high) << 32) | low; };
    return FileStamp{combine(info.nFileIndexHigh, info.nFileIndexLow),
                     combine(info.nFileSizeHigh, info.nFileSizeLow),
                     combine(info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime),
                     info.dwVolumeSerialNumber, true};
}

}  // namespace

std::optional<FileStamp> fileStamp(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return FileStamp{};
        return std::nullopt;
    }
    const auto stamp = stampFromHandle(file);
    CloseHandle(file);
    return stamp;
}

bool readDocument(const std::wstring& path, std::wstring& text, bool& bom, FileStamp& stamp) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const auto current = stampFromHandle(file);
    if (!current || current->size > INT_MAX) { CloseHandle(file); return false; }
    std::string s((size_t)current->size, '\0');
    DWORD count = 0;
    const bool read = s.empty() || (ReadFile(file, s.data(), (DWORD)s.size(), &count, nullptr) && count == s.size());
    CloseHandle(file);
    if (!read) return false;
    bom = s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB &&
        (unsigned char)s[2] == 0xBF;
    if (bom) s.erase(0, 3);
    stamp = *current;
    if (s.empty()) { text.clear(); return true; }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                           (int)s.size(), nullptr, 0);
    if (!length) return false;
    text.resize(length);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(),
                               text.data(), length) == length;
}

void setEncoding(Tab& tab, const std::wstring& text, bool bom) {
    tab.utf8Bom = bom;
    const size_t pos = text.find_first_of(L"\r\n");
    tab.lineEnding = LineEnding::LF;
    if (pos != std::wstring::npos && text[pos] == L'\r')
        tab.lineEnding = pos + 1 < text.size() && text[pos + 1] == L'\n'
                        ? LineEnding::CRLF : LineEnding::CR;
}

std::wstring documentText(const Tab& tab) {
    std::wstring text;
    if (tab.utf8Bom) text += L'\uFEFF';
    const wchar_t* newline = L"\n";
    if (tab.lineEnding == LineEnding::CRLF) newline = L"\r\n";
    if (tab.lineEnding == LineEnding::CR) newline = L"\r";
    for (int i = 0; i < tab.ed.lineCount(); ++i) {
        if (i) text += newline;
        text += tab.ed.line(i);
    }
    return text;
}

bool writeAllText(const std::wstring& path, const std::wstring& text,
                  const FileStamp* expected, FileStamp* saved) {
    if (text.size() > INT_MAX) return false;
    int need = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), (int)text.size(),
                                   nullptr, 0, nullptr, nullptr);
    if (!text.empty() && need <= 0) return false;
    std::string utf8((size_t)need, '\0');
    if (need && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.c_str(), (int)text.size(),
                                   utf8.data(), need, nullptr, nullptr) != need) return false;
    static unsigned sequence = 0;
    const std::wstring parent = path.substr(0, path.find_last_of(L"\\/") + 1);
    const std::wstring temp = parent + L".mdreader-" + std::to_wstring(GetCurrentProcessId()) +
                              L"-" + std::to_wstring(GetTickCount64()) + L"-" +
                              std::to_wstring(++sequence) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const DWORD size = static_cast<DWORD>(utf8.size());
    const bool flushed = WriteFile(file, utf8.data(), size, &written, nullptr) &&
                         written == size && FlushFileBuffers(file);
    const bool closed = CloseHandle(file) != 0;
    const auto result = saved ? fileStamp(temp) : std::optional<FileStamp>{};
    if (flushed && closed && (!saved || (result && result->exists)) &&
        (!expected || fileStamp(path) == *expected) &&
        MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (saved) *saved = *result;
        return true;
    }
    DeleteFileW(temp.c_str());
    return false;
}
