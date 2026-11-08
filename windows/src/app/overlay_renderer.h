#pragma once

#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <optional>
#include <vector>

#include "annotation.h"
#include "cursor_style.h"
#include "geometry.h"
#include "style.h"

using lz::HighlightKind;

/// Transparent topmost DirectComposition window that carries ink, highlights and click effects.
class OverlayRenderer {
public:
    OverlayRenderer() = default;
    ~OverlayRenderer();
    OverlayRenderer(const OverlayRenderer&) = delete;
    OverlayRenderer& operator=(const OverlayRenderer&) = delete;

    bool showForMonitor(HMONITOR monitor);
    void hide();
    void shutdown();

    [[nodiscard]] HWND hwnd() const { return hwnd_; }
    [[nodiscard]] bool visible() const { return visible_; }

    /// Display-local viewport origin, zoom and display size for the current frame.
    void setMapping(lz::Point viewportOrigin, double zoom, lz::Size display);

    void beginStroke(lz::Point content);
    void extendStroke(lz::Point content, bool straight);
    void endStroke();
    void cancelStroke();
    void undo();
    void clear();
    [[nodiscard]] bool hasContent() const { return !annotations_.empty(); }

    void addHighlight(HighlightKind kind, lz::Rect line);
    void addClick(lz::Point screenLocal, double now);
    /// Screen-local position of the drawn cursor, scaled with the zoom.
    void setCursor(lz::Point position, double scale, double opacity);
    void setCursorStyle(lz::CursorStyle style) { cursorStyle_ = style; }
    void setInk(const lz::InkLook& ink) { ink_ = ink; }
    void setClickEffect(lz::ClickEffect effect, lz::Rgba color);

    void render(double now);

private:
    struct ClickMark {
        lz::Point position;
        double start = 0;
        lz::ClickEffect effect = lz::ClickEffect::Ripple;
        lz::Rgba color;
    };

    static LRESULT CALLBACK overlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    bool createWindow(HMONITOR monitor);
    void destroyWindow();
    bool createDevice();
    void destroyDevice();

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> buildStrokePath(
        const std::vector<lz::Point>& points) const;
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> roundedRect(
        const lz::Rect& rect, double corner) const;
    void drawAnnotation(const lz::Annotation& annotation);
    void drawStroke(const lz::Annotation& annotation);
    void drawHighlight(const lz::Annotation& annotation);
    void drawStyledGeometry(ID2D1Geometry* geometry, const lz::InkLook& look, double width,
                            lz::InkStyle style);
    void drawClicks(double now);
    void drawCursor();
    bool buildCursorPath();
    void setBrush(const lz::Rgba& color, float alphaScale = 1.0f);

    HWND hwnd_ = nullptr;
    HMONITOR monitor_ = nullptr;
    RECT monitorRect_{};
    bool visible_ = false;
    bool classRegistered_ = false;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStyle_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> buttStyle_;
    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dashStyle_;
    Microsoft::WRL::ComPtr<ID2D1PathGeometry> cursorPath_;
    lz::Point cursorPosition_{};
    double cursorScale_ = 1;
    double cursorOpacity_ = 1;
    bool cursorVisible_ = false;
    lz::CursorStyle cursorStyle_ = lz::CursorStyle::Classic;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;

    std::vector<lz::Annotation> annotations_;
    std::vector<ClickMark> clicks_;
    bool strokeInProgress_ = false;

    lz::Point mappingOrigin_{};
    double mappingZoom_ = 1;
    lz::Size display_{};

    lz::InkLook ink_;
    lz::ClickEffect clickEffect_ = lz::ClickEffect::Ripple;
    lz::Rgba clickColor_{0.20f, 0.55f, 1.0f, 1.0f};
};
