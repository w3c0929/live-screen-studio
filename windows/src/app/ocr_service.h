#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "geometry.h"

inline constexpr UINT kLiveZoomOcrResult = WM_APP + 12;

struct OcrLine {
    lz::Rect box;
    std::string text;
};

struct OcrResultMessage {
    std::vector<OcrLine> lines;
    std::function<void(std::vector<OcrLine>)> callback;
};

/// Runs the packaged OCR helper as a hidden child process and parses its line boxes.
/// Results are posted to the owner window on kLiveZoomOcrResult.
class OcrService {
public:
    using Callback = std::function<void(std::vector<OcrLine>)>;

    OcrService(HWND target, std::wstring helperPath);
    ~OcrService();
    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    [[nodiscard]] bool available() const;
    void recognize(const RECT& captureRect, Callback callback);
    void shutdown();

private:
    struct Request {
        RECT rect{};
        Callback callback;
    };

    void workerLoop();
    static std::vector<OcrLine> runHelper(const std::wstring& helperPath,
                                          const RECT& captureRect);

    HWND target_ = nullptr;
    std::wstring helperPath_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Request> pending_;
    bool stop_ = false;
    std::atomic<bool> busy_{false};
};
