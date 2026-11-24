#include "app.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct ImgEntry {
    ComPtr<ID2D1Bitmap> bmp;
    UINT w = 0, h = 0;
    bool ready = false;
    bool failed = false;
    bool pending = false;
    uint64_t lastUse = 0;
};

struct Decoded {
    std::wstring path;
    UINT w = 0, h = 0;
    std::vector<BYTE> px;
    bool ok = false;
};

std::unordered_map<std::wstring, ImgEntry>& cache() {
    static std::unordered_map<std::wstring, ImgEntry> c;
    return c;
}

std::mutex& mu() {
    static std::mutex m;
    return m;
}

std::vector<Decoded*>& done() {
    static std::vector<Decoded*> d;
    return d;
}

HWND g_notify = nullptr;

void decodeThread(std::wstring path) {
    Decoded* out = new Decoded();
    out->path = path;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    IWICImagingFactory* factory = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&factory)))) {
        IWICBitmapDecoder* dec = nullptr;
        if (SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnLoad, &dec))) {
            IWICBitmapFrameDecode* frame = nullptr;
            if (SUCCEEDED(dec->GetFrame(0, &frame))) {
                IWICFormatConverter* conv = nullptr;
                if (SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
                    SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                               WICBitmapDitherTypeNone, nullptr, 0.0,
                                               WICBitmapPaletteTypeCustom))) {
                    UINT w = 0, h = 0;
                    if (SUCCEEDED(conv->GetSize(&w, &h)) && w && h) {
                        const UINT stride = w * 4;
                        out->px.resize((size_t)stride * h);
                        if (SUCCEEDED(conv->CopyPixels(nullptr, stride, (UINT)out->px.size(),
                                                       out->px.data()))) {
                            out->w = w;
                            out->h = h;
                            out->ok = true;
                        }
                    }
                }
                if (conv) conv->Release();
                frame->Release();
            }
            dec->Release();
        }
        factory->Release();
    }
    CoUninitialize();

    {
        std::lock_guard<std::mutex> lock(mu());
        done().push_back(out);
    }
    if (g_notify) PostMessageW(g_notify, WM_APP_IMAGE, 0, 0);
}

}  // namespace

std::wstring resolveImagePath(const Tab& t, const std::wstring& src) {
    if (src.empty()) return std::wstring();
    std::wstring s = src;
    if (s.rfind(L"file://", 0) == 0) {
        s = s.substr(7);
        std::wstring buf(s.size() + 1, L'\0');
        DWORD n = (DWORD)buf.size();
        if (SUCCEEDED(UrlUnescapeW(&buf[0], &s[0], &n, URL_UNESCAPE_INPLACE )))
            s = &buf[0];
    }
    if (s.size() > 2 && s[1] == L':') return s;  // absolute drive path
    size_t slash = t.path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return s;
    std::wstring dir = t.path.substr(0, slash + 1);
    std::wstring joined = dir + s;
    wchar_t full[1024];
    if (GetFullPathNameW(joined.c_str(), 1024, full, nullptr)) return full;
    return joined;
}

bool imageInfo(const std::wstring& path, UINT* w, UINT* h, int* state) {
    auto it = cache().find(path);
    if (it == cache().end()) {
        if (state) *state = 0;
        return false;
    }
    if (state) *state = it->second.ready ? 1 : (it->second.failed ? 2 : 0);
    *w = it->second.w;
    *h = it->second.h;
    return it->second.ready;
}

void requestImage(App& app, const std::wstring& path) {
    if (path.empty()) return;
    ImgEntry& e = cache()[path];
    e.lastUse = GetTickCount64();
    if (e.ready || e.failed || e.pending) return;
    e.pending = true;
    g_notify = app.hwnd;
    std::thread(decodeThread, path).detach();
}

ID2D1Bitmap* imageBitmap(const std::wstring& path) {
    auto it = cache().find(path);
    if (it == cache().end() || !it->second.ready) return nullptr;
    return it->second.bmp.get();
}

void imagePoll(App& app) {
    std::vector<Decoded*> items;
    {
        std::lock_guard<std::mutex> lock(mu());
        items.swap(done());
    }
    bool changed = false;
    for (Decoded* d : items) {
        ImgEntry& e = cache()[d->path];
        e.pending = false;
        e.w = d->w;
        e.h = d->h;
        if (!d->ok || !app.rt) {
            e.failed = true;
        } else {
            D2D1_BITMAP_PROPERTIES props = {};
            props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
            props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
            props.dpiX = 96.f;
            props.dpiY = 96.f;
            ComPtr<ID2D1Bitmap> bmp;
            if (SUCCEEDED(app.rt->CreateBitmap(D2D1_SIZE_U{d->w, d->h}, d->px.data(), d->w * 4,
                                               &props, bmp.put()))) {
                e.bmp = std::move(bmp);
                e.ready = true;
            } else {
                e.failed = true;
            }
        }
        changed = true;
        delete d;
    }
    if (changed && app.hasTab()) {
        // Image heights may have changed: lay the rest out again.
        relayoutFrom(app, 0);
    }
}

// Called from the painter for a block image.
void drawImageBlock(App& app, const Item& it, const std::wstring& path) {
    UINT w = 0, h = 0;
    int state = 0;
    imageInfo(path, &w, &h, &state);
    D2D1_RECT_F r = rect(it.x, it.y, it.x + it.w, it.y + it.h);
    if (state == 1) {
        if (ID2D1Bitmap* bmp = imageBitmap(path))
            app.rt->DrawBitmap(bmp, r, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        return;
    }
    // placeholder
    D2D1_ROUNDED_RECT rr = {r, 4.f, 4.f};
    app.rt->FillRoundedRectangle(rr, app.brCodeBg.get());
    const wchar_t* label = state == 2 ? L"\u56fe\u7247\u52a0\u8f7d\u5931\u8d25" : L"\u6b63\u5728\u52a0\u8f7d\u2026";
    app.rt->DrawText(label, (UINT32)wcslen(label), app.fmtBtn.get(),
                     rect(r.left, (r.top + r.bottom) * 0.5f - 10.f, r.right,
                          (r.top + r.bottom) * 0.5f + 14.f),
                     app.brMuted.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

// Pulls a CF_DIB off the clipboard and writes it as a PNG next to the
// document, so pasted screenshots become ordinary relative links.
bool saveClipboardImage(HWND owner, const std::wstring& file) {
    if (!OpenClipboard(owner)) return false;
    HANDLE h = GetClipboardData(CF_DIB);
    if (!h) {
        CloseClipboard();
        return false;
    }
    const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)GlobalLock(h);
    bool ok = false;
    if (bi && bi->biBitCount >= 24 && bi->biCompression == BI_RGB) {
        int w = (int)bi->biWidth;
        int hgt = (int)bi->biHeight;
        bool topDown = hgt < 0;
        if (topDown) hgt = -hgt;
        const BYTE* bits = (const BYTE*)bi + bi->biSize;
        const int srcBpp = bi->biBitCount / 8;
        const int srcStride = ((w * bi->biBitCount + 31) / 32) * 4;

        std::vector<BYTE> px((size_t)w * (size_t)hgt * 4, 0);
        bool anyAlpha = false;
        for (int y = 0; y < hgt; ++y) {
            const BYTE* row = bits + (size_t)(topDown ? y : hgt - 1 - y) * srcStride;
            BYTE* dst = px.data() + (size_t)y * w * 4;
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = row[x * srcBpp + 0];
                dst[x * 4 + 1] = row[x * srcBpp + 1];
                dst[x * 4 + 2] = row[x * srcBpp + 2];
                BYTE a = srcBpp == 4 ? row[x * 4 + 3] : 255;
                dst[x * 4 + 3] = a;
                if (a) anyAlpha = true;
            }
        }
        if (!anyAlpha)
            for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;

        IWICImagingFactory* f = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&f)))) {
            IWICBitmap* bmp = nullptr;
            if (SUCCEEDED(f->CreateBitmapFromMemory((UINT)w, (UINT)hgt,
                                                    GUID_WICPixelFormat32bppBGRA, (UINT)w * 4,
                                                    (UINT)px.size(), px.data(), &bmp))) {
                IWICStream* stream = nullptr;
                if (SUCCEEDED(f->CreateStream(&stream)) &&
                    SUCCEEDED(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE))) {
                    IWICBitmapEncoder* enc = nullptr;
                    if (SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
                        SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache))) {
                        IWICBitmapFrameEncode* frame = nullptr;
                        IPropertyBag2* props = nullptr;
                        if (SUCCEEDED(enc->CreateNewFrame(&frame, &props))) {
                            frame->Initialize(props);
                            frame->SetSize((UINT)w, (UINT)hgt);
                            WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
                            frame->SetPixelFormat(&fmt);
                            if (SUCCEEDED(frame->WriteSource(bmp, nullptr)) &&
                                SUCCEEDED(frame->Commit()))
                                ok = SUCCEEDED(enc->Commit());
                            frame->Release();
                            if (props) props->Release();
                        }
                        enc->Release();
                    }
                    stream->Release();
                }
                bmp->Release();
            }
            f->Release();
        }
    }
    GlobalUnlock(h);
    CloseClipboard();
    return ok;
}
