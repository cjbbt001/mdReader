#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string>
#include <cstdio>
#include <vector>

// Minimal COM smart pointer: move-only, no exceptions.
template <class T>
struct ComPtr {
    T* p = nullptr;
    ComPtr() = default;
    ComPtr(T* q) : p(q) {}
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p(o.p) { o.p = nullptr; }
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) { if (p) p->Release(); p = o.p; o.p = nullptr; }
        return *this;
    }
    ~ComPtr() { if (p) p->Release(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T** put() { reset(); return &p; }
    IUnknown** putUnk() { reset(); return (IUnknown**)&p; }
    T* get() const { return p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

inline std::wstring widen(const char* s, size_t n) {
    if (!n) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, (int)n, &w[0], len);
    return w;
}

inline D2D1_COLOR_F rgb(unsigned hex, float a = 1.f) {
    D2D1_COLOR_F c;
    c.r = ((hex >> 16) & 0xFF) / 255.f;
    c.g = ((hex >> 8) & 0xFF) / 255.f;
    c.b = (hex & 0xFF) / 255.f;
    c.a = a;
    return c;
}

inline D2D1_RECT_F rect(float l, float t, float r, float b) {
    D2D1_RECT_F rc; rc.left = l; rc.top = t; rc.right = r; rc.bottom = b; return rc;
}
