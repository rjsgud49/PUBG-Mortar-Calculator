#pragma once

#include <windows.h>

#include <string>

#include "vision.hpp"

struct OverlayAnchor {
    bool shown = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class Overlay {
public:
    bool create(HINSTANCE instance);
    bool set_hidden_from_capture(bool hidden);
    void set_view(const OverlayView& view);
    void set_notice(const std::wstring& notice);
    OverlayAnchor follow(const std::wstring& title_part);
    void show_results(const MapResult& map, const ElevationResult& elevation, bool have_elevation);
    bool excluded_from_capture() const;
    HWND hwnd() const;

private:
    void redraw();
    void ensure_surface(int width, int height);
    void destroy_surface();
    bool create_window(int x, int y, int width, int height);
    bool replace_window();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC screen_dc_ = nullptr;
    HDC memory_dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    void* bits_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool excluded_ = false;
    bool visible_ = false;
    bool dirty_ = true;
    int anchor_x_ = 0;
    int anchor_y_ = 0;
    std::wstring notice_;
    MapResult map_{};
    ElevationResult elevation_{};
    OverlayView view_{};
    bool have_elevation_ = false;
};

bool exclude_from_capture(HWND hwnd);
