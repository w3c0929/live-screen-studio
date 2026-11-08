#include "ocr_service.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <sstream>
#include <utility>

namespace {

std::wstring quote(const std::wstring& path) {
    return L"\"" + path + L"\"";
}

}  // namespace

OcrService::OcrService(HWND target, std::wstring helperPath)
    : target_(target), helperPath_(std::move(helperPath)) {
    worker_ = std::thread(&OcrService::workerLoop, this);
}

OcrService::~OcrService() {
    shutdown();
}

bool OcrService::available() const {
    if (helperPath_.empty()) return false;
    return GetFileAttributesW(helperPath_.c_str()) != INVALID_FILE_ATTRIBUTES;
}

void OcrService::recognize(const RECT& captureRect, Callback callback) {
    if (!available() || !callback) return;
    {
        std::lock_guard lock(mutex_);
        if (stop_ || busy_ || pending_) return;
        pending_ = Request{captureRect, std::move(callback)};
    }
    cv_.notify_one();
}

void OcrService::shutdown() {
    {
        std::lock_guard lock(mutex_);
        if (stop_) return;
        stop_ = true;
        pending_.reset();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void OcrService::workerLoop() {
    for (;;) {
        Request request;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || pending_.has_value(); });
            if (stop_) return;
            request = std::move(*pending_);
            pending_.reset();
        }

        busy_ = true;
        std::vector<OcrLine> lines = runHelper(helperPath_, request.rect);
        busy_ = false;

        auto* result = new (std::nothrow) OcrResultMessage{std::move(lines),
                                                            std::move(request.callback)};
        if (!result) continue;
        if (!PostMessageW(target_, kLiveZoomOcrResult, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }
}

std::vector<OcrLine> OcrService::runHelper(const std::wstring& helperPath,
                                           const RECT& captureRect) {
    std::vector<OcrLine> lines;
    if (helperPath.empty()) return lines;

    std::wstring command = quote(helperPath) + L" --quiet --rect " +
                           std::to_wstring(captureRect.left) + L" " +
                           std::to_wstring(captureRect.top) + L" " +
                           std::to_wstring(captureRect.right - captureRect.left) + L" " +
                           std::to_wstring(captureRect.bottom - captureRect.top);

    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return lines;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = nulIn;

    PROCESS_INFORMATION process{};
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');
    const BOOL started = CreateProcessW(
        nullptr, commandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
        nullptr, &startup, &process);
    CloseHandle(writePipe);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
    if (!started) {
        CloseHandle(readPipe);
        std::printf("[livezoom] OCR helper failed to start: %lu\n", GetLastError());
        return lines;
    }

    std::string output;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
    }
    WaitForSingleObject(process.hProcess, 10000);
    CloseHandle(readPipe);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        double x = 0, y = 0, width = 0, height = 0;
        if (std::sscanf(line.c_str(), "%lf,%lf,%lf,%lf", &x, &y, &width, &height) != 4) {
            continue;
        }
        if (width <= 0 || height <= 0) continue;
        OcrLine parsed;
        parsed.box = {x, y, width, height};
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) parsed.text = line.substr(tab + 1);
        lines.push_back(std::move(parsed));
    }
    return lines;
}
