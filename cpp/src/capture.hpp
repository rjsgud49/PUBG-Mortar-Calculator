#pragma once

#include <opencv2/core.hpp>
#include <windows.h>

#include <string>

struct CapturedFrame {
    cv::Mat bgr;
    RECT bounds{};
};

HWND find_window_by_title(const std::wstring& title_part);
bool client_bounds_on_screen(HWND window, RECT& bounds);
CapturedFrame capture_window(HWND window);
