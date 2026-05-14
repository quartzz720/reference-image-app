#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <string>
#include <algorithm>
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#endif

using Gdiplus::Graphics;
using Gdiplus::Image;
using Gdiplus::InterpolationModeHighQualityBicubic;
using Gdiplus::Rect;
using Gdiplus::Status;

namespace {
const wchar_t kWindowClassName[] = L"ReferenceImageWindow";
const UINT kOpenImageMessage = WM_APP + 1;

ULONG_PTR g_gdiplusToken = 0;
Image* g_loadedImage = nullptr;
int g_imagePixelWidth = 0;
int g_imagePixelHeight = 0;
double g_imageAspect = 1.0;
bool g_hasOpenedDialog = false;

UINT GetDpiForWindowSafe(HWND hwnd) {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto getDpiForWindow = reinterpret_cast<UINT(WINAPI*)(HWND)>(
        GetProcAddress(user32, "GetDpiForWindow"));
    if (getDpiForWindow) {
        return getDpiForWindow(hwnd);
    }
    return 96;
}

void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto setDpiAwarenessContext = reinterpret_cast<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpiAwarenessContext) {
        setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        return;
    }

    auto setProcessDpiAware = reinterpret_cast<BOOL(WINAPI*)()>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setProcessDpiAware) {
        setProcessDpiAware();
    }
}

void GetWindowExtraSize(HWND hwnd, int* extraWidth, int* extraHeight) {
    RECT rect = {0, 0, 100, 100};
    DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    DWORD exStyle = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    UINT dpi = GetDpiForWindowSafe(hwnd);

    if ((style & WS_CAPTION) == 0) {
        *extraWidth = 0;
        *extraHeight = 0;
        return;
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    auto adjustForDpi = reinterpret_cast<BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT)>(
        GetProcAddress(user32, "AdjustWindowRectExForDpi"));
    BOOL ok = FALSE;
    if (adjustForDpi) {
        ok = adjustForDpi(&rect, style, FALSE, exStyle, dpi);
    } else {
        ok = AdjustWindowRectEx(&rect, style, FALSE, exStyle);
    }

    if (!ok) {
        *extraWidth = 0;
        *extraHeight = 0;
        return;
    }

    *extraWidth = (rect.right - rect.left) - 100;
    *extraHeight = (rect.bottom - rect.top) - 100;
}

bool ShowOpenFileDialog(HWND hwnd, std::wstring* outPath) {
    wchar_t filePath[MAX_PATH] = {0};
    const wchar_t* filter =
        L"Images (*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0"
        L"All Files\0"
        L"*.*\0\0";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }

    *outPath = filePath;
    return true;
}

bool LoadImageFromFile(const std::wstring& path) {
    delete g_loadedImage;
    g_loadedImage = nullptr;

    Image* image = Image::FromFile(path.c_str());
    if (!image || image->GetLastStatus() != Status::Ok) {
        delete image;
        return false;
    }

    g_loadedImage = image;
    g_imagePixelWidth = static_cast<int>(g_loadedImage->GetWidth());
    g_imagePixelHeight = static_cast<int>(g_loadedImage->GetHeight());
    g_imageAspect = g_imagePixelHeight > 0 ?
        static_cast<double>(g_imagePixelWidth) / static_cast<double>(g_imagePixelHeight) :
        1.0;
    return true;
}

void FitWindowToImage(HWND hwnd) {
    if (!g_loadedImage) {
        return;
    }

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    int maxWidth = static_cast<int>((workArea.right - workArea.left) * 0.9);
    int maxHeight = static_cast<int>((workArea.bottom - workArea.top) * 0.9);

    int extraWidth = 0;
    int extraHeight = 0;
    GetWindowExtraSize(hwnd, &extraWidth, &extraHeight);

    int maxClientWidth = std::max(1, maxWidth - extraWidth);
    int maxClientHeight = std::max(1, maxHeight - extraHeight);

    double scale = 1.0;
    if (g_imagePixelWidth > 0 && g_imagePixelHeight > 0) {
        double scaleW = static_cast<double>(maxClientWidth) / static_cast<double>(g_imagePixelWidth);
        double scaleH = static_cast<double>(maxClientHeight) / static_cast<double>(g_imagePixelHeight);
        scale = std::min(1.0, std::min(scaleW, scaleH));
    }

    int clientWidth = std::max(1, static_cast<int>(g_imagePixelWidth * scale));
    int clientHeight = std::max(1, static_cast<int>(g_imagePixelHeight * scale));

    int windowWidth = clientWidth + extraWidth;
    int windowHeight = clientHeight + extraHeight;
    int x = workArea.left + (workArea.right - workArea.left - windowWidth) / 2;
    int y = workArea.top + (workArea.bottom - workArea.top - windowHeight) / 2;

    SetWindowPos(hwnd, HWND_TOPMOST, x, y, windowWidth, windowHeight, SWP_SHOWWINDOW);
}

void AdjustRectForAspect(HWND hwnd, UINT edge, RECT* rect) {
    if (!g_loadedImage || g_imageAspect <= 0.0) {
        return;
    }

    int extraWidth = 0;
    int extraHeight = 0;
    GetWindowExtraSize(hwnd, &extraWidth, &extraHeight);

    int windowWidth = rect->right - rect->left;
    int windowHeight = rect->bottom - rect->top;
    int clientWidth = std::max(1, windowWidth - extraWidth);
    int clientHeight = std::max(1, windowHeight - extraHeight);

    int newWindowWidth = windowWidth;
    int newWindowHeight = windowHeight;

    auto applyWidth = [&](int width) {
        int newClientWidth = std::max(1, width - extraWidth);
        int newImageHeight = static_cast<int>(newClientWidth / g_imageAspect + 0.5);
        int newClientHeight = newImageHeight;
        newWindowWidth = newClientWidth + extraWidth;
        newWindowHeight = newClientHeight + extraHeight;
    };

    auto applyHeight = [&](int height) {
        int newClientHeight = std::max(1, height - extraHeight);
        int newClientWidth = static_cast<int>(newClientHeight * g_imageAspect + 0.5);
        newWindowWidth = newClientWidth + extraWidth;
        newWindowHeight = newClientHeight + extraHeight;
    };

    switch (edge) {
        case WMSZ_LEFT:
        case WMSZ_RIGHT:
            applyWidth(windowWidth);
            rect->bottom = rect->top + newWindowHeight;
            break;
        case WMSZ_TOP:
        case WMSZ_BOTTOM:
            applyHeight(windowHeight);
            rect->right = rect->left + newWindowWidth;
            break;
        case WMSZ_TOPLEFT:
        case WMSZ_TOPRIGHT:
        case WMSZ_BOTTOMLEFT:
        case WMSZ_BOTTOMRIGHT: {
            int heightFromWidth = static_cast<int>(clientWidth / g_imageAspect + 0.5);
            int widthFromHeight = static_cast<int>(clientHeight * g_imageAspect + 0.5);
            int diffByWidth = std::abs(heightFromWidth - clientHeight);
            int diffByHeight = std::abs(widthFromHeight - clientWidth);

            if (diffByWidth <= diffByHeight) {
                applyWidth(windowWidth);
            } else {
                applyHeight(windowHeight);
            }

            if (edge == WMSZ_TOPLEFT) {
                rect->left = rect->right - newWindowWidth;
                rect->top = rect->bottom - newWindowHeight;
            } else if (edge == WMSZ_TOPRIGHT) {
                rect->right = rect->left + newWindowWidth;
                rect->top = rect->bottom - newWindowHeight;
            } else if (edge == WMSZ_BOTTOMLEFT) {
                rect->left = rect->right - newWindowWidth;
                rect->bottom = rect->top + newWindowHeight;
            } else {
                rect->right = rect->left + newWindowWidth;
                rect->bottom = rect->top + newWindowHeight;
            }
            break;
        }
        default:
            break;
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            PostMessageW(hwnd, kOpenImageMessage, 0, 0);
            return 0;
        case kOpenImageMessage: {
            if (g_hasOpenedDialog) {
                return 0;
            }
            g_hasOpenedDialog = true;

            std::wstring path;
            if (!ShowOpenFileDialog(hwnd, &path)) {
                DestroyWindow(hwnd);
                return 0;
            }
            if (!LoadImageFromFile(path)) {
                MessageBoxW(hwnd, L"Failed to load image.", L"Error", MB_OK | MB_ICONERROR);
                DestroyWindow(hwnd);
                return 0;
            }

            FitWindowToImage(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_SIZING:
            if (g_loadedImage) {
                AdjustRectForAspect(hwnd, static_cast<UINT>(wParam), reinterpret_cast<RECT*>(lParam));
                InvalidateRect(hwnd, nullptr, TRUE);
                return TRUE;
            }
            return FALSE;
        case WM_SIZE:
            if (g_loadedImage) {
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wParam) != WA_INACTIVE) {
                SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            return 0;
        case WM_DPICHANGED: {
            const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(hwnd, nullptr,
                suggested->left,
                suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT client = {};
            GetClientRect(hwnd, &client);
            int width = client.right - client.left;
            int height = client.bottom - client.top;

            if (width <= 0 || height <= 0) {
                EndPaint(hwnd, &ps);
                return 0;
            }

            HDC memDc = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
            HGDIOBJ oldBitmap = SelectObject(memDc, memBitmap);

            RECT fillRect = {0, 0, width, height};
            FillRect(memDc, &fillRect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

            Graphics graphics(memDc);
            graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            if (g_loadedImage) {
                graphics.DrawImage(g_loadedImage, Rect(0, 0, width, height));
            }

            BitBlt(hdc, 0, 0, width, height, memDc, 0, 0, SRCCOPY);

            SelectObject(memDc, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    EnableDpiAwareness();

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr) != Status::Ok) {
        return 1;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    if (!RegisterClassExW(&wc)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME;
    DWORD exStyle = WS_EX_TOPMOST;

    HWND hwnd = CreateWindowExW(
        exStyle,
        kWindowClassName,
        L"Reference Image",
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        800,
        600,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    HICON appIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (appIcon) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
    }

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    delete g_loadedImage;
    g_loadedImage = nullptr;

    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
