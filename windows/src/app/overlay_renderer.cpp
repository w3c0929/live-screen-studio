#include "overlay_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <dwmapi.h>

#include "arrow_head.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kOverlayClass[] = L"LiveZoomOverlayHost";

D2D1_COLOR_F toColor(const lz::Rgba& color, float alphaScale = 1.0f) {
    return D2D1::ColorF(color.r, color.g, color.b, color.a * alphaScale);
}

lz::Rgba lighten(const lz::Rgba& color, float amount) {
    return {color.r + (1.0f - color.r) * amount,
            color.g + (1.0f - color.g) * amount,
            color.b + (1.0f - color.b) * amount,
            1.0f};
}

}  // namespace

OverlayRenderer::~OverlayRenderer() {
    shutdown();
}

LRESULT CALLBACK OverlayRenderer::overlayProc(HWND hwnd, UINT message, WPARAM wParam,
                                              LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_SETCURSOR: {
            // A topmost click-through window still wins the cursor. Ask the real window
            // below it for its cursor, so I-beam, hand and resize cursors keep working.
            POINT point{};
            GetCursorPos(&point);
            HWND below = WindowFromPoint(point);
            while (below) {
                wchar_t className[64]{};
                GetClassNameW(below, className, 64);
                if (std::wcscmp(className, L"LiveZoomOverlayHost") != 0 &&
                    std::wcscmp(className, L"LiveZoomMagnifierHost") != 0) {
                    break;
                }
                below = GetWindow(below, GW_HWNDNEXT);
            }
            if (below) {
                const LRESULT hit =
                    SendMessageW(below, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
                SendMessageW(below, WM_SETCURSOR, reinterpret_cast<WPARAM>(below),
                             MAKELPARAM(static_cast<WORD>(hit), WM_MOUSEMOVE));
            }
            return TRUE;
        }
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

bool OverlayRenderer::createWindow(HMONITOR monitor) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) return false;

    if (!classRegistered_) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = overlayProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kOverlayClass;
        classRegistered_ = RegisterClassExW(&windowClass) != 0 ||
                           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
    }

    monitor_ = monitor;
    monitorRect_ = info.rcMonitor;
    const int width = monitorRect_.right - monitorRect_.left;
    const int height = monitorRect_.bottom - monitorRect_.top;
    hwnd_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT |
            WS_EX_NOREDIRECTIONBITMAP,
        kOverlayClass, L"LiveZoom Overlay", WS_POPUP,
        monitorRect_.left, monitorRect_.top, width, height,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd_) {
        std::printf("[livezoom] overlay CreateWindowEx failed: %lu\n", GetLastError());
        return false;
    }
    return true;
}

void OverlayRenderer::destroyWindow() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    monitor_ = nullptr;
    monitorRect_ = {};
}

bool OverlayRenderer::createDevice() {
    const int width = monitorRect_.right - monitorRect_.left;
    const int height = monitorRect_.bottom - monitorRect_.top;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d_, nullptr, &context_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, nullptr, &context_);
    }
    if (FAILED(hr)) {
        std::printf("[livezoom] overlay D3D11CreateDevice failed: 0x%08lx\n", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3d_.As(&dxgiDevice);
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
    hr = factory->CreateSwapChainForComposition(d3d_.Get(), &description, nullptr, &swapChain_);
    if (FAILED(hr)) {
        std::printf("[livezoom] overlay CreateSwapChain failed: 0x%08lx\n", hr);
        return false;
    }

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.GetAddressOf());
    if (FAILED(hr)) return false;
    hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_);
    if (FAILED(hr)) return false;
    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_);
    if (FAILED(hr)) return false;

    ComPtr<IDXGISurface> surface;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;
    D2D1_BITMAP_PROPERTIES1 properties{};
    properties.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                               D2D1_ALPHA_MODE_PREMULTIPLIED);
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    properties.dpiX = 96;
    properties.dpiY = 96;
    hr = d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target_);
    if (FAILED(hr)) return false;
    d2dContext_->SetTarget(target_.Get());
    d2dContext_->SetDpi(96, 96);
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    hr = d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);
    if (FAILED(hr)) return false;

    D2D1_STROKE_STYLE_PROPERTIES roundProperties{};
    roundProperties.startCap = D2D1_CAP_STYLE_ROUND;
    roundProperties.endCap = D2D1_CAP_STYLE_ROUND;
    roundProperties.dashCap = D2D1_CAP_STYLE_ROUND;
    roundProperties.lineJoin = D2D1_LINE_JOIN_ROUND;
    roundProperties.miterLimit = 10;
    roundProperties.dashStyle = D2D1_DASH_STYLE_SOLID;
    hr = d2dFactory_->CreateStrokeStyle(roundProperties, nullptr, 0, &roundStyle_);
    if (FAILED(hr)) return false;

    D2D1_STROKE_STYLE_PROPERTIES buttProperties = roundProperties;
    buttProperties.startCap = D2D1_CAP_STYLE_FLAT;
    buttProperties.endCap = D2D1_CAP_STYLE_FLAT;
    buttProperties.dashCap = D2D1_CAP_STYLE_FLAT;
    buttProperties.lineJoin = D2D1_LINE_JOIN_MITER;
    hr = d2dFactory_->CreateStrokeStyle(buttProperties, nullptr, 0, &buttStyle_);
    if (FAILED(hr)) return false;

    D2D1_STROKE_STYLE_PROPERTIES dashProperties = roundProperties;
    dashProperties.dashStyle = D2D1_DASH_STYLE_CUSTOM;
    const float dashes[] = {2.4f, 2.2f};
    hr = d2dFactory_->CreateStrokeStyle(dashProperties, dashes, 2, &dashStyle_);
    if (FAILED(hr)) return false;

    const D2D1_GRADIENT_STOP spotlightStops[4] = {
        {0.0f, D2D1::ColorF(0, 0, 0, 0.0f)},
        {0.86f, D2D1::ColorF(0, 0, 0, 0.0f)},
        {0.94f, D2D1::ColorF(0, 0, 0, 0.52f)},
        {1.0f, D2D1::ColorF(0, 0, 0, 0.52f)},
    };
    ComPtr<ID2D1GradientStopCollection> spotlightStopsCollection;
    if (SUCCEEDED(d2dContext_->CreateGradientStopCollection(
            spotlightStops, 4, &spotlightStopsCollection))) {
        d2dContext_->CreateRadialGradientBrush(
            D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0),
                                                D2D1::Point2F(0, 0), 1, 1),
            spotlightStopsCollection.Get(), &spotlightBrush_);
    }

    hr = DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&dcompDevice_));
    if (FAILED(hr)) return false;
    hr = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
    if (FAILED(hr)) return false;
    hr = dcompDevice_->CreateVisual(&dcompVisual_);
    if (FAILED(hr)) return false;
    hr = dcompVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr)) return false;
    hr = dcompTarget_->SetRoot(dcompVisual_.Get());
    if (FAILED(hr)) return false;
    hr = dcompDevice_->Commit();
    return SUCCEEDED(hr);
}

void OverlayRenderer::destroyDevice() {
    dcompVisual_.Reset();
    dcompTarget_.Reset();
    dcompDevice_.Reset();
    target_.Reset();
    brush_.Reset();
    roundStyle_.Reset();
    buttStyle_.Reset();
    dashStyle_.Reset();
    spotlightBrush_.Reset();
    cursorPath_.Reset();
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    swapChain_.Reset();
    context_.Reset();
    d3d_.Reset();
}

bool OverlayRenderer::showForMonitor(HMONITOR monitor) {
    if (hwnd_ && monitor_ == monitor) {
        SetWindowPos(hwnd_, HWND_TOPMOST, monitorRect_.left, monitorRect_.top,
                     monitorRect_.right - monitorRect_.left,
                     monitorRect_.bottom - monitorRect_.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        visible_ = true;
        return true;
    }
    hide();
    destroyDevice();
    destroyWindow();
    annotations_.clear();
    clicks_.clear();
    strokeInProgress_ = false;
    if (!createWindow(monitor)) return false;
    if (!createDevice()) {
        destroyWindow();
        return false;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, monitorRect_.left, monitorRect_.top,
                 monitorRect_.right - monitorRect_.left,
                 monitorRect_.bottom - monitorRect_.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    visible_ = true;
    return true;
}

void OverlayRenderer::hide() {
    if (!visible_) return;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    visible_ = false;
}

void OverlayRenderer::shutdown() {
    hide();
    destroyDevice();
    destroyWindow();
    annotations_.clear();
    clicks_.clear();
}

void OverlayRenderer::setMapping(lz::Point viewportOrigin, double zoom, lz::Size display) {
    if (zoom == 0) zoom = 1;
    if (!(display == display_)) {
        display_ = display;
        annotations_.clear();
        clicks_.clear();
    }
    mappingOrigin_ = viewportOrigin;
    mappingZoom_ = zoom;
}

void OverlayRenderer::setClickEffect(lz::ClickEffect effect, lz::Rgba color) {
    clickEffect_ = effect;
    clickColor_ = color;
}

void OverlayRenderer::beginStroke(lz::Point content) {
    if (!visible_) return;
    if (strokeInProgress_ && !annotations_.empty()) {
        annotations_.pop_back();
    }
    annotations_.push_back(lz::Annotation::stroke(content, ink_, mappingZoom_));
    strokeInProgress_ = true;
}

void OverlayRenderer::extendStroke(lz::Point content, bool straight) {
    if (!strokeInProgress_ || annotations_.empty()) return;
    annotations_.back().extend(content, straight);
}

void OverlayRenderer::endStroke() {
    if (!annotations_.empty()) annotations_.back().complete = true;
    strokeInProgress_ = false;
}

void OverlayRenderer::cancelStroke() {
    if (!strokeInProgress_) return;
    if (!annotations_.empty()) annotations_.pop_back();
    strokeInProgress_ = false;
}

void OverlayRenderer::undo() {
    if (!annotations_.empty()) {
        annotations_.pop_back();
    } else if (spotlightActive_) {
        spotlightActive_ = false;
    }
    strokeInProgress_ = false;
}

void OverlayRenderer::clear() {
    annotations_.clear();
    clicks_.clear();
    spotlightActive_ = false;
    strokeInProgress_ = false;
}

void OverlayRenderer::addHighlight(HighlightKind kind, lz::Rect line) {
    addHighlight(kind, line, mappingZoom_);
}

void OverlayRenderer::addHighlight(HighlightKind kind, lz::Rect line, double zoom) {
    if (!visible_) return;
    if (kind == HighlightKind::Spotlight) {
        annotations_.erase(std::remove_if(annotations_.begin(), annotations_.end(),
                                         [](const lz::Annotation& a) { return a.spotlight; }),
                          annotations_.end());
    }
    annotations_.push_back(lz::Annotation::highlight(kind, line, ink_, zoom));
}

bool OverlayRenderer::removeSpotlight() {
    const auto it = std::find_if(annotations_.begin(), annotations_.end(),
                                 [](const lz::Annotation& a) { return a.spotlight; });
    if (it == annotations_.end()) return false;
    annotations_.erase(it);
    return true;
}

void OverlayRenderer::toggleSpotlight() {
    spotlightActive_ = !spotlightActive_;
}

void OverlayRenderer::setSpotlightCenter(lz::Point screenLocal, double radius) {
    spotlightCenter_ = screenLocal;
    spotlightRadius_ = radius;
}

void OverlayRenderer::addClick(lz::Point screenLocal, double now) {
    if (!visible_) return;
    clicks_.push_back({screenLocal, now, clickEffect_, clickColor_});
}

void OverlayRenderer::setCursor(lz::Point position, double scale, double opacity) {
    cursorPosition_ = position;
    cursorScale_ = scale;
    cursorOpacity_ = opacity;
    cursorVisible_ = opacity > 0.01;
}

bool OverlayRenderer::buildCursorPath() {
    if (cursorPath_) return true;
    if (!d2dFactory_) return false;
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(d2dFactory_->CreatePathGeometry(&path))) return false;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return false;
    const auto p = [](float x, float y) { return D2D1::Point2F(x, y); };
    sink->BeginFigure(p(0, 0), D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLine(p(0, 16.5f));
    sink->AddLine(p(3.9f, 12.8f));
    sink->AddLine(p(6.6f, 19.0f));
    sink->AddLine(p(9.3f, 17.8f));
    sink->AddLine(p(6.7f, 11.8f));
    sink->AddLine(p(11.8f, 11.8f));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    cursorPath_ = path;
    return true;
}

void OverlayRenderer::drawCursor() {
    if (!cursorVisible_ || !d2dContext_ || !buildCursorPath()) return;
    const float scale = static_cast<float>(cursorScale_);
    if (scale <= 0) return;
    const float opacity = static_cast<float>(cursorOpacity_);
    d2dContext_->SetTransform(D2D1::Matrix3x2F(
        scale, 0, 0, scale, static_cast<float>(cursorPosition_.x),
        static_cast<float>(cursorPosition_.y)));

    auto arrow = [&](lz::Rgba fill, lz::Rgba outline, float outlineWidth) {
        setBrush({0, 0, 0, 0.35f}, opacity);
        d2dContext_->DrawGeometry(cursorPath_.Get(), brush_.Get(), 3.2f, roundStyle_.Get());
        setBrush(fill, opacity);
        d2dContext_->FillGeometry(cursorPath_.Get(), brush_.Get());
        setBrush(outline, opacity);
        d2dContext_->DrawGeometry(cursorPath_.Get(), brush_.Get(), outlineWidth,
                                  roundStyle_.Get());
    };

    switch (cursorStyle_) {
        case lz::CursorStyle::Light:
            arrow({1, 1, 1, 1}, {0.1f, 0.1f, 0.12f, 1}, 1.2f);
            break;
        case lz::CursorStyle::Ocean:
            arrow({0.18f, 0.5f, 0.93f, 1}, {1, 1, 1, 1}, 1.6f);
            break;
        case lz::CursorStyle::Sunset:
            arrow({1.0f, 0.36f, 0.22f, 1}, {1, 1, 1, 1}, 1.6f);
            break;
        case lz::CursorStyle::Mint:
            arrow({0.14f, 0.78f, 0.58f, 1}, {1, 1, 1, 1}, 1.6f);
            break;
        case lz::CursorStyle::Dot: {
            setBrush({0.1f, 0.1f, 0.12f, 0.88f}, opacity);
            const D2D1_ELLIPSE dot{D2D1::Point2F(0, 0), 7.5f, 7.5f};
            d2dContext_->FillEllipse(dot, brush_.Get());
            setBrush({1, 1, 1, 1}, opacity);
            d2dContext_->DrawEllipse(dot, brush_.Get(), 2.0f);
            break;
        }
        case lz::CursorStyle::Spotlight:
            setBrush({1.0f, 0.84f, 0.04f, 0.42f}, opacity);
            d2dContext_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(0, 0), 17, 17), brush_.Get());
            arrow({0.07f, 0.07f, 0.08f, 1}, {1, 1, 1, 1}, 1.5f);
            break;
        case lz::CursorStyle::System:
        case lz::CursorStyle::Classic:
        default:
            arrow({0.07f, 0.07f, 0.08f, 1}, {1, 1, 1, 1}, 1.2f);
            break;
    }
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
}

void OverlayRenderer::setBrush(const lz::Rgba& color, float alphaScale) {
    if (brush_) brush_->SetColor(toColor(color, alphaScale));
}

ComPtr<ID2D1PathGeometry> OverlayRenderer::buildStrokePath(
    const std::vector<lz::Point>& points) const {
    if (!d2dFactory_ || points.empty()) return nullptr;
    ComPtr<ID2D1PathGeometry> path;
    if (FAILED(d2dFactory_->CreatePathGeometry(&path))) return nullptr;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(path->Open(&sink))) return nullptr;

    sink->BeginFigure(D2D1::Point2F(static_cast<float>(points[0].x),
                                    static_cast<float>(points[0].y)),
                      D2D1_FIGURE_BEGIN_HOLLOW);
    if (points.size() == 1) {
        sink->AddLine(D2D1::Point2F(static_cast<float>(points[0].x + 0.01),
                                    static_cast<float>(points[0].y)));
    } else if (points.size() == 2) {
        sink->AddLine(D2D1::Point2F(static_cast<float>(points[1].x),
                                    static_cast<float>(points[1].y)));
    } else {
        for (size_t i = 1; i + 1 < points.size(); ++i) {
            const lz::Point mid{(points[i].x + points[i + 1].x) / 2,
                                (points[i].y + points[i + 1].y) / 2};
            sink->AddQuadraticBezier(D2D1::QuadraticBezierSegment(
                D2D1::Point2F(static_cast<float>(points[i].x),
                              static_cast<float>(points[i].y)),
                D2D1::Point2F(static_cast<float>(mid.x), static_cast<float>(mid.y))));
        }
        sink->AddLine(D2D1::Point2F(static_cast<float>(points.back().x),
                                    static_cast<float>(points.back().y)));
    }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    return path;
}

ComPtr<ID2D1RoundedRectangleGeometry> OverlayRenderer::roundedRect(const lz::Rect& rect,
                                                                   double corner) const {
    if (!d2dFactory_) return nullptr;
    ComPtr<ID2D1RoundedRectangleGeometry> geometry;
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(
        D2D1::RectF(static_cast<float>(rect.minX()), static_cast<float>(rect.minY()),
                    static_cast<float>(rect.maxX()), static_cast<float>(rect.maxY())),
        static_cast<float>(corner), static_cast<float>(corner));
    if (FAILED(d2dFactory_->CreateRoundedRectangleGeometry(rounded, &geometry))) return nullptr;
    return geometry;
}

void OverlayRenderer::drawStyledGeometry(ID2D1Geometry* geometry, const lz::InkLook& look,
                                         double width, lz::InkStyle style) {
    if (!geometry || !d2dContext_ || !brush_) return;
    const float w = static_cast<float>(std::max(width, 0.01));
    switch (style) {
        case lz::InkStyle::Marker:
        case lz::InkStyle::Arrow:
            setBrush(look.color);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w, roundStyle_.Get());
            break;
        case lz::InkStyle::Neon:
            setBrush(look.color, 0.14f);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w * 3.6f, roundStyle_.Get());
            setBrush(look.color, 0.32f);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w * 2.0f, roundStyle_.Get());
            setBrush(look.color);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w, roundStyle_.Get());
            setBrush(lighten(look.color, 0.7f));
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w * 0.38f, roundStyle_.Get());
            break;
        case lz::InkStyle::Highlighter:
            setBrush(look.color, 0.38f);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w * 3.6f, buttStyle_.Get());
            break;
        case lz::InkStyle::Dashed:
            setBrush(look.color);
            d2dContext_->DrawGeometry(geometry, brush_.Get(), w, dashStyle_.Get());
            break;
    }
}

void OverlayRenderer::drawStroke(const lz::Annotation& annotation) {
    if (annotation.points.empty()) return;
    ComPtr<ID2D1PathGeometry> path = buildStrokePath(annotation.points);
    if (!path) return;
    const double width = annotation.look.width * annotation.unit;
    drawStyledGeometry(path.Get(), annotation.look, width, annotation.look.style);

    if (annotation.look.style == lz::InkStyle::Arrow) {
        const double length =
            std::max(annotation.look.width * 3.2, 14.0) * annotation.unit;
        if (const auto barbs = lz::ArrowHead::barbs(annotation.points, length)) {
            ComPtr<ID2D1PathGeometry> chevron;
            if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&chevron))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(chevron->Open(&sink))) {
                    const auto [a, b] = *barbs;
                    const lz::Point tip = annotation.points.back();
                    sink->BeginFigure(D2D1::Point2F(static_cast<float>(a.x),
                                                    static_cast<float>(a.y)),
                                      D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(D2D1::Point2F(static_cast<float>(tip.x),
                                                static_cast<float>(tip.y)));
                    sink->AddLine(D2D1::Point2F(static_cast<float>(b.x),
                                                static_cast<float>(b.y)));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    drawStyledGeometry(chevron.Get(), annotation.look, width,
                                       lz::InkStyle::Marker);
                }
            }
        }
    }
}

void OverlayRenderer::drawHighlight(const lz::Annotation& annotation) {
    const double h = annotation.line.height;
    const lz::Rect around = annotation.line.insetBy(-h * 0.35, -h * 0.12);
    const double corner = std::min(around.height * 0.25, 8.0 * annotation.unit);
    const lz::InkLook& look = annotation.look;
    const lz::InkStyle lineStyle =
        (look.style == lz::InkStyle::Neon || look.style == lz::InkStyle::Dashed)
            ? look.style
            : lz::InkStyle::Marker;
    const double width = std::max(look.width * 0.45, 2.0) * annotation.unit;

    switch (annotation.kind) {
        case HighlightKind::Box: {
            if (auto geometry = roundedRect(around, corner)) {
                drawStyledGeometry(geometry.Get(), look, width, lineStyle);
            }
            break;
        }
        case HighlightKind::Underline: {
            const double y = annotation.line.maxY() + h * 0.18;
            ComPtr<ID2D1PathGeometry> path;
            if (SUCCEEDED(d2dFactory_->CreatePathGeometry(&path))) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(path->Open(&sink))) {
                    sink->BeginFigure(
                        D2D1::Point2F(static_cast<float>(annotation.line.minX()),
                                      static_cast<float>(y)),
                        D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLine(
                        D2D1::Point2F(static_cast<float>(annotation.line.maxX()),
                                      static_cast<float>(y)));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->Close();
                    drawStyledGeometry(path.Get(), look, width, lineStyle);
                }
            }
            break;
        }
        case HighlightKind::Fill: {
            const double y = annotation.line.midY();
            setBrush(look.color, 0.35f);
            d2dContext_->DrawLine(
                D2D1::Point2F(static_cast<float>(annotation.line.minX() - h * 0.15),
                              static_cast<float>(y)),
                D2D1::Point2F(static_cast<float>(annotation.line.maxX() + h * 0.15),
                              static_cast<float>(y)),
                brush_.Get(), static_cast<float>(h * 1.25), buttStyle_.Get());
            break;
        }
        case HighlightKind::Spotlight: {
            const float left = static_cast<float>(std::max(0.0, around.minX()));
            const float top = static_cast<float>(std::max(0.0, around.minY()));
            const float right = static_cast<float>(std::min(display_.width, around.maxX()));
            const float bottom = static_cast<float>(std::min(display_.height, around.maxY()));
            const float w = static_cast<float>(display_.width);
            const float hgt = static_cast<float>(display_.height);
            setBrush({0, 0, 0, 0.55f});
            d2dContext_->FillRectangle(D2D1::RectF(0, 0, w, top), brush_.Get());
            d2dContext_->FillRectangle(D2D1::RectF(0, bottom, w, hgt), brush_.Get());
            d2dContext_->FillRectangle(D2D1::RectF(0, top, left, bottom), brush_.Get());
            d2dContext_->FillRectangle(D2D1::RectF(right, top, w, bottom), brush_.Get());
            if (auto geometry = roundedRect(around, corner)) {
                setBrush(look.color, 0.85f);
                d2dContext_->DrawGeometry(geometry.Get(), brush_.Get(),
                                          static_cast<float>(1.5 * annotation.unit),
                                          roundStyle_.Get());
            }
            break;
        }
    }
}

void OverlayRenderer::drawAnnotation(const lz::Annotation& annotation) {
    // A left press starts a stroke immediately; while it is still a single point it is either a
    // click that will be cancelled, or the start of a line. Drawing it would flash a dot.
    if (!annotation.spotlight && annotation.points.size() == 1) return;
    if (annotation.spotlight) {
        drawHighlight(annotation);
    } else if (!annotation.points.empty()) {
        drawStroke(annotation);
    } else {
        drawHighlight(annotation);
    }
}

void OverlayRenderer::drawSpotlight() {
    if (!spotlightActive_ || !spotlightBrush_ || !d2dContext_) return;
    if (spotlightRadius_ <= 0) return;
    const float width = static_cast<float>(
        display_.width > 0 ? display_.width : (monitorRect_.right - monitorRect_.left));
    const float height = static_cast<float>(
        display_.height > 0 ? display_.height : (monitorRect_.bottom - monitorRect_.top));
    spotlightBrush_->SetCenter(D2D1::Point2F(static_cast<float>(spotlightCenter_.x),
                                             static_cast<float>(spotlightCenter_.y)));
    spotlightBrush_->SetRadiusX(static_cast<float>(spotlightRadius_));
    spotlightBrush_->SetRadiusY(static_cast<float>(spotlightRadius_));
    d2dContext_->FillRectangle(D2D1::RectF(0, 0, width, height), spotlightBrush_.Get());
}

void OverlayRenderer::drawClicks(double now) {
    for (const ClickMark& mark : clicks_) {
        const auto look = lz::ClickEffectMath::appearance(mark.effect, now - mark.start);
        if (!look) continue;
        setBrush(mark.color, static_cast<float>(look->opacity));
        const D2D1_ELLIPSE ellipse{
            D2D1::Point2F(static_cast<float>(mark.position.x),
                          static_cast<float>(mark.position.y)),
            static_cast<float>(look->radius), static_cast<float>(look->radius)};
        if (look->filled) {
            d2dContext_->FillEllipse(ellipse, brush_.Get());
        } else {
            d2dContext_->DrawEllipse(ellipse, brush_.Get(),
                                     static_cast<float>(look->lineWidth));
        }
    }
}

void OverlayRenderer::render(double now) {
    if (!visible_ || !d2dContext_ || !swapChain_) return;

    clicks_.erase(std::remove_if(clicks_.begin(), clicks_.end(),
                                 [&](const ClickMark& mark) {
                                     return !lz::ClickEffectMath::appearance(
                                                 mark.effect, now - mark.start)
                                                 .has_value();
                                 }),
                  clicks_.end());

    d2dContext_->BeginDraw();
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->Clear(D2D1::ColorF(0, 0, 0, 0));

    const float scale = static_cast<float>(mappingZoom_);
    d2dContext_->SetTransform(D2D1::Matrix3x2F(
        scale, 0, 0, scale, static_cast<float>(-mappingOrigin_.x * mappingZoom_),
        static_cast<float>(-mappingOrigin_.y * mappingZoom_)));
    for (const lz::Annotation& annotation : annotations_) {
        drawAnnotation(annotation);
    }

    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    drawSpotlight();
    drawClicks(now);
    drawCursor();

    const HRESULT hr = d2dContext_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        std::printf("[livezoom] overlay target lost\n");
        return;
    }
    swapChain_->Present(1, 0);
}
