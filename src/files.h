#pragma once
#include "tab.h"
#include <optional>

std::optional<FileStamp> fileStamp(const std::wstring& path);
bool readDocument(const std::wstring& path, std::wstring& text, bool& bom, FileStamp& stamp);
void setEncoding(Tab& tab, const std::wstring& text, bool bom);
std::wstring documentText(const Tab& tab);
bool writeAllText(const std::wstring& path, const std::wstring& text,
                  const FileStamp* expected = nullptr, FileStamp* saved = nullptr);
