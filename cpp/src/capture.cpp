#include "capture.hpp"

#include <opencv2/imgproc.hpp>

namespace {

struct Search {
    std::wstring title;
    DWORD self_pid = 0;
    HWND found = nullptr;
};

BOOL CALLBACK enum_windows(HWND hwnd, LPARAM param) {
    auto* search = reinterpret_cast<Search*>(param);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == search->self_pid) {
        return TRUE;
    }
    wchar_t title[512] = {};
    GetWindowTextW(hwnd, title, 512);
    if (title[0] == L'\0') {
        return TRUE;
    }
    if (wcsstr(title, search->title.c_str()) != nullptr) {
        search->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

HWND find_window_by_title(const std::wstring& title_part) {
    Search search;
    search.title = title_part;
    search.self_pid = GetCurrentProcessId();
    EnumWindows(enum_windows, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

bool client_bounds_on_screen(HWND window, RECT& bounds) {
    if (window == nullptr) {
        return false;
    }
    RECT client{};
    if (!GetClientRect(window, &client)) {
        return false;
    }
    POINT origin{0, 0};
    if (!ClientToScreen(window, &origin)) {
        return false;
    }
    bounds.left = origin.x;
    bounds.top = origin.y;
    bounds.right = origin.x + (client.right - client.left);
    bounds.bottom = origin.y + (client.bottom - client.top);
    return bounds.right > bounds.left && bounds.bottom > bounds.top;
}

CapturedFrame capture_window(HWND window) {
    CapturedFrame frame;
    if (window == nullptr) {
        return frame;
    }
    RECT bounds{};
    if (!client_bounds_on_screen(window, bounds)) {
        return frame;
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return frame;
    }

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    HGDIOBJ previous = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, bounds.left, bounds.top, SRCCOPY | CAPTUREBLT);

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    cv::Mat bgra(height, width, CV_8UC4);
    GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), bgra.data, &info, DIB_RGB_COLORS);
    cv::cvtColor(bgra, frame.bgr, cv::COLOR_BGRA2BGR);
    frame.bounds = bounds;

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return frame;
}
