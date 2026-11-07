// Minimal proof of concept for the transparent annotation/cursor layer.
//
// A topmost, click-through, WS_EX_NOREDIRECTIONBITMAP window is composed by DirectComposition.
// Direct2D draws a demo stroke, a cursor ring and an animated click ripple into a premultiplied
// swap chain. This is the layer that will carry LiveZoom's ink, custom cursor and click effects.
//
// Controls: Esc quits. The demo always runs for --seconds N when supplied.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <d3d11.h>
#include <d2d1_3.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kOverlayClass[] = L"LiveZoomOverlayPoCHost";

void logLine(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::printf("\n");
    std::fflush(stdout);
    va_end(args);
}

struct Renderer {
    HWND hwnd = nullptr;
    RECT monitor{};
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> d2dContext;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1SolidColorBrush> inkBrush;
    ComPtr<ID2D1SolidColorBrush> accentBrush;
    ComPtr<ID2D1SolidColorBrush> whiteBrush;
    ComPtr<ID2D1PathGeometry> demoPath;
    ComPtr<IDCompositionDevice> dcompDevice;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;
    LARGE_INTEGER frequency{};
    LARGE_INTEGER start{};
    uint64_t lastFpsTick = 0;
    int frames = 0;
};

Renderer g_renderer;
bool g_cleanedUp = false;

double nowSeconds() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart - g_renderer.start.QuadPart) /
           static_cast<double>(g_renderer.frequency.QuadPart);
}

bool createDemoPath() {
    HRESULT hr = g_renderer.d2dFactory->CreatePathGeometry(&g_renderer.demoPath);
    if (FAILED(hr)) return false;
    ComPtr<ID2D1GeometrySink> sink;
    hr = g_renderer.demoPath->Open(&sink);
    if (FAILED(hr)) return false;
    sink->BeginFigure(D2D1::Point2F(120, 180), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i <= 160; ++i) {
        const float x = 120.0f + i * 5.0f;
        const float y = 180.0f + std::sin(i * 0.08f) * 70.0f;
        sink->AddLine(D2D1::Point2F(x, y));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    return true;
}

bool createRenderer(HWND hwnd, RECT monitor) {
    g_renderer.hwnd = hwnd;
    g_renderer.monitor = monitor;
    QueryPerformanceFrequency(&g_renderer.frequency);
    QueryPerformanceCounter(&g_renderer.start);

    const int width = monitor.right - monitor.left;
    const int height = monitor.bottom - monitor.top;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &g_renderer.d3d, nullptr,
                                   &g_renderer.context);
    if (FAILED(hr)) {
        logLine("[overlay_poc] hardware device failed (0x%08lx), trying WARP", hr);
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &g_renderer.d3d, nullptr,
                               &g_renderer.context);
    }
    if (FAILED(hr)) {
        logLine("[overlay_poc] D3D11CreateDevice failed: 0x%08lx", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = g_renderer.d3d.As(&dxgiDevice);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = factory->CreateSwapChainForComposition(g_renderer.d3d.Get(), &description, nullptr,
                                                &g_renderer.swapChain);
    if (FAILED(hr)) {
        logLine("[overlay_poc] CreateSwapChainForComposition failed: 0x%08lx", hr);
        return false;
    }

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                           g_renderer.d2dFactory.GetAddressOf());
    if (FAILED(hr)) {
        logLine("[overlay_poc] D2D1CreateFactory failed: 0x%08lx", hr);
        return false;
    }
    hr = g_renderer.d2dFactory->CreateDevice(dxgiDevice.Get(), &g_renderer.d2dDevice);
    if (FAILED(hr)) return false;
    hr = g_renderer.d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                   &g_renderer.d2dContext);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface> surface;
    hr = g_renderer.swapChain->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;
    D2D1_BITMAP_PROPERTIES1 properties{};
    properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                               D2D1_ALPHA_MODE_PREMULTIPLIED);
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    properties.dpiX = 96;
    properties.dpiY = 96;
    hr = g_renderer.d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &properties,
                                                            &g_renderer.target);
    if (FAILED(hr)) {
        logLine("[overlay_poc] CreateBitmapFromDxgiSurface failed: 0x%08lx", hr);
        return false;
    }
    g_renderer.d2dContext->SetTarget(g_renderer.target.Get());
    g_renderer.d2dContext->SetDpi(96, 96);
    g_renderer.d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    g_renderer.d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 0.23f, 0.19f, 0.95f), &g_renderer.inkBrush);
    g_renderer.d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(0.20f, 0.55f, 1.0f, 0.85f), &g_renderer.accentBrush);
    g_renderer.d2dContext->CreateSolidColorBrush(
        D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &g_renderer.whiteBrush);
    if (!createDemoPath()) {
        logLine("[overlay_poc] could not create demo geometry");
        return false;
    }

    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&g_renderer.dcompDevice));
    if (FAILED(hr)) {
        logLine("[overlay_poc] DCompositionCreateDevice failed: 0x%08lx", hr);
        return false;
    }
    hr = g_renderer.dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &g_renderer.dcompTarget);
    if (FAILED(hr)) return false;
    hr = g_renderer.dcompDevice->CreateVisual(&g_renderer.dcompVisual);
    if (FAILED(hr)) return false;
    hr = g_renderer.dcompVisual->SetContent(g_renderer.swapChain.Get());
    if (FAILED(hr)) return false;
    hr = g_renderer.dcompTarget->SetRoot(g_renderer.dcompVisual.Get());
    if (FAILED(hr)) return false;
    hr = g_renderer.dcompDevice->Commit();
    if (FAILED(hr)) return false;

    logLine("[overlay_poc] renderer ready (%dx%d)", width, height);
    return true;
}

void renderFrame() {
    if (!g_renderer.d2dContext || !g_renderer.swapChain) return;
    const double t = nowSeconds();
    POINT cursor{};
    GetCursorPos(&cursor);
    const float cx = static_cast<float>(cursor.x - g_renderer.monitor.left);
    const float cy = static_cast<float>(cursor.y - g_renderer.monitor.top);

    auto* context = g_renderer.d2dContext.Get();
    context->BeginDraw();
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    // A marker-style demo stroke.
    context->DrawGeometry(g_renderer.demoPath.Get(), g_renderer.inkBrush.Get(), 5.0f);

    // A soft neon layer behind it.
    D2D1_COLOR_F neon = D2D1::ColorF(1.0f, 0.23f, 0.19f, 0.16f);
    g_renderer.inkBrush->SetColor(neon);
    context->DrawGeometry(g_renderer.demoPath.Get(), g_renderer.inkBrush.Get(), 18.0f);
    g_renderer.inkBrush->SetColor(D2D1::ColorF(1.0f, 0.23f, 0.19f, 0.95f));

    // Cursor ring + centre dot.
    context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 13, 13),
                         g_renderer.whiteBrush.Get(), 2.5f);
    context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 4, 4),
                         g_renderer.inkBrush.Get());

    // Animated ripple, 0.6 s per cycle.
    const double phase = std::fmod(t, 0.6) / 0.6;
    const float eased = static_cast<float>(1 - std::pow(1 - phase, 3));
    const float radius = 8 + 34 * eased;
    g_renderer.accentBrush->SetColor(
        D2D1::ColorF(0.20f, 0.55f, 1.0f, static_cast<float>(0.85 * (1 - phase))));
    context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius),
                         g_renderer.accentBrush.Get(), 3.0f);

    const HRESULT hr = context->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        logLine("[overlay_poc] D2D target lost; restart the PoC to recreate it");
        return;
    }
    g_renderer.swapChain->Present(1, 0);
    DwmFlush();

    const uint64_t now = GetTickCount64();
    ++g_renderer.frames;
    if (now - g_renderer.lastFpsTick >= 1000) {
        if (g_renderer.lastFpsTick) {
            logLine("[overlay_poc] %d fps", g_renderer.frames);
        }
        g_renderer.lastFpsTick = now;
        g_renderer.frames = 0;
    }
}

LRESULT CALLBACK overlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

void cleanup() {
    if (g_cleanedUp) return;
    g_cleanedUp = true;
    g_renderer.dcompVisual.Reset();
    g_renderer.dcompTarget.Reset();
    g_renderer.dcompDevice.Reset();
    g_renderer.target.Reset();
    g_renderer.inkBrush.Reset();
    g_renderer.accentBrush.Reset();
    g_renderer.whiteBrush.Reset();
    g_renderer.demoPath.Reset();
    g_renderer.d2dContext.Reset();
    g_renderer.d2dDevice.Reset();
    g_renderer.d2dFactory.Reset();
    g_renderer.swapChain.Reset();
    g_renderer.context.Reset();
    g_renderer.d3d.Reset();
    if (g_renderer.hwnd) {
        DestroyWindow(g_renderer.hwnd);
        g_renderer.hwnd = nullptr;
    }
}

BOOL WINAPI consoleCtrlHandler(DWORD) {
    cleanup();
    return FALSE;
}

}  // namespace

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    double seconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::stod(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("usage: overlay_poc [--seconds N]\n");
            return 0;
        }
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        logLine("[overlay_poc] GetMonitorInfo failed: %lu", GetLastError());
        return 1;
    }

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = overlayProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = kOverlayClass;
    RegisterClassExW(&windowClass);

    const int width = info.rcMonitor.right - info.rcMonitor.left;
    const int height = info.rcMonitor.bottom - info.rcMonitor.top;
    const HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
            WS_EX_NOREDIRECTIONBITMAP,
        kOverlayClass, L"LiveZoom Overlay PoC", WS_POPUP,
        info.rcMonitor.left, info.rcMonitor.top, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        logLine("[overlay_poc] CreateWindowEx failed: %lu", GetLastError());
        return 1;
    }

    if (!createRenderer(hwnd, info.rcMonitor)) {
        cleanup();
        return 1;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, info.rcMonitor.left, info.rcMonitor.top, width, height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    const uint64_t quitAt = seconds > 0
                                ? GetTickCount64() + static_cast<uint64_t>(seconds * 1000)
                                : 0;
    bool running = true;
    while (running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) break;
        if (quitAt && GetTickCount64() >= quitAt) break;
        renderFrame();
    }

    cleanup();
    logLine("[overlay_poc] done");
    return 0;
}
