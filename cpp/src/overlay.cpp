#include "overlay.hpp"

#include "capture.hpp"
#include "utf.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace {

std::wstring format_number(std::optional<double> value) {
    if (!value) {
        return utf8_to_wide("없음");
    }
    std::ostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << *value;
    return utf8_to_wide(stream.str());
}

void draw_circle(HDC dc, cv::Point point, COLORREF color) {
    const int radius = 8;
    HPEN pen = CreatePen(PS_SOLID, 3, color);
    HGDIOBJ old = SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, point.x - radius, point.y - radius, point.x + radius, point.y + radius);
    SelectObject(dc, old);
    DeleteObject(pen);
}

}  // namespace

bool set_excluded_from_capture(HWND hwnd, bool excluded) {
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    const DWORD affinity = excluded ? WDA_EXCLUDEFROMCAPTURE : 0;
    return SetWindowDisplayAffinity(hwnd, affinity) != FALSE;
}

bool exclude_from_capture(HWND hwnd) {
    return set_excluded_from_capture(hwnd, true);
}

bool Overlay::create_window(int x, int y, int width, int height) {
    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"PubgMortarOverlay",
        L"PUBG Mortar Overlay",
        WS_POPUP,
        x,
        y,
        width,
        height,
        nullptr,
        nullptr,
        instance_,
        nullptr
    );
    return hwnd_ != nullptr;
}

bool Overlay::replace_window() {
    RECT rect{0, 0, 100, 100};
    const bool was_visible = visible_;
    if (hwnd_ != nullptr) {
        GetWindowRect(hwnd_, &rect);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width < 1) {
        width = 100;
    }
    if (height < 1) {
        height = 100;
    }
    if (!create_window(rect.left, rect.top, width, height)) {
        visible_ = false;
        return false;
    }
    if (was_visible) {
        SetWindowPos(hwnd_, HWND_TOPMOST, rect.left, rect.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        redraw();
    }
    return true;
}

bool Overlay::create(HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"PubgMortarOverlay";
    RegisterClassW(&window_class);

    if (!create_window(0, 0, 100, 100)) {
        return false;
    }
    screen_dc_ = GetDC(nullptr);
    memory_dc_ = CreateCompatibleDC(screen_dc_);
    return true;
}

void Overlay::ensure_surface(int width, int height) {
    if (bitmap_ != nullptr && width_ == width && height_ == height) {
        return;
    }
    destroy_surface();
    width_ = width;
    height_ = height;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(memory_dc_, &info, DIB_RGB_COLORS, &bits_, nullptr, 0);
    SelectObject(memory_dc_, bitmap_);
}

void Overlay::destroy_surface() {
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
        bits_ = nullptr;
    }
}

bool Overlay::set_hidden_from_capture(bool hidden) {
    if (hwnd_ == nullptr) {
        return false;
    }
    // Once a window is excluded, clearing the flag does not put it back into capture.
    // A new window that was never excluded is included.
    if (excluded_ && !hidden) {
        if (!replace_window()) {
            return false;
        }
        excluded_ = false;
        set_excluded_from_capture(hwnd_, false);
        return true;
    }
    if (!set_excluded_from_capture(hwnd_, hidden)) {
        return false;
    }
    excluded_ = hidden;
    return true;
}

void Overlay::set_view(const OverlayView& view) {
    view_ = view;
    dirty_ = true;
    if (!view_.enabled && hwnd_ != nullptr && visible_) {
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
}

void Overlay::set_notice(const std::wstring& notice) {
    if (notice_ == notice) {
        return;
    }
    notice_ = notice;
    dirty_ = true;
    if (visible_) {
        redraw();
    }
}

OverlayAnchor Overlay::follow(const std::wstring& title_part) {
    OverlayAnchor anchor;
    if (!view_.enabled || hwnd_ == nullptr) {
        if (visible_) {
            ShowWindow(hwnd_, SW_HIDE);
            visible_ = false;
        }
        return anchor;
    }
    HWND target = find_window_by_title(title_part);
    if (target == nullptr) {
        if (visible_) {
            ShowWindow(hwnd_, SW_HIDE);
            visible_ = false;
        }
        return anchor;
    }
    RECT bounds{};
    if (!client_bounds_on_screen(target, bounds)) {
        return anchor;
    }
    const int width = bounds.right - bounds.left;
    const int height = bounds.bottom - bounds.top;
    if (width <= 0 || height <= 0) {
        return anchor;
    }
    const bool moved = !visible_ || anchor_x_ != bounds.left || anchor_y_ != bounds.top || width_ != width || height_ != height;
    if (!moved && !dirty_) {
        anchor.shown = true;
        anchor.x = bounds.left;
        anchor.y = bounds.top;
        anchor.width = width;
        anchor.height = height;
        return anchor;
    }
    ensure_surface(width, height);
    SetWindowPos(hwnd_, HWND_TOPMOST, bounds.left, bounds.top, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    anchor_x_ = bounds.left;
    anchor_y_ = bounds.top;
    visible_ = true;
    redraw();
    anchor.shown = true;
    anchor.x = bounds.left;
    anchor.y = bounds.top;
    anchor.width = width;
    anchor.height = height;
    return anchor;
}

void Overlay::show_results(const MapResult& map, const ElevationResult& elevation, bool have_elevation) {
    map_ = map;
    elevation_ = elevation;
    have_elevation_ = have_elevation;
    dirty_ = true;
    if (visible_) {
        redraw();
    }
}

void Overlay::redraw() {
    if (bitmap_ == nullptr || bits_ == nullptr || width_ <= 0 || height_ <= 0) {
        return;
    }
    std::fill_n(static_cast<uint32_t*>(bits_), static_cast<size_t>(width_) * height_, 0u);
    SetMapMode(memory_dc_, MM_TEXT);
    SetWindowOrgEx(memory_dc_, 0, 0, nullptr);
    SetViewportOrgEx(memory_dc_, 0, 0, nullptr);
    SetBkMode(memory_dc_, TRANSPARENT);
    if (view_.borders) {
        HPEN border = CreatePen(PS_SOLID, 30, RGB(255, 255, 0));
        HGDIOBJ old_pen = SelectObject(memory_dc_, border);
        SelectObject(memory_dc_, GetStockObject(NULL_BRUSH));
        Rectangle(memory_dc_, 0, 0, width_, height_);
        SelectObject(memory_dc_, old_pen);
        DeleteObject(border);
    }

    const double scale = view_.scale <= 0 ? 1.0 : view_.scale / 100.0;
    auto scaled = [&](int value) { return static_cast<int>(value / scale); };

    int origin_x = 0;
    int origin_y = 0;
    if (map_.minimap_box) {
        origin_x = map_.minimap_box->x;
        origin_y = map_.minimap_box->y;
    }
    if (view_.minimap_box && map_.minimap_box) {
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(160, 0, 160));
        HGDIOBJ old_pen = SelectObject(memory_dc_, pen);
        SelectObject(memory_dc_, GetStockObject(NULL_BRUSH));
        Rectangle(
            memory_dc_,
            scaled(map_.minimap_box->x),
            scaled(map_.minimap_box->y),
            scaled(map_.minimap_box->x + map_.minimap_box->width),
            scaled(map_.minimap_box->y + map_.minimap_box->height)
        );
        SelectObject(memory_dc_, old_pen);
        DeleteObject(pen);
    }

    HFONT font = CreateFontW(
        20,
        0,
        0,
        0,
        FW_BOLD,
        FALSE,
        FALSE,
        FALSE,
        HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        L"Malgun Gothic"
    );
    HGDIOBJ old_font = SelectObject(memory_dc_, font);
    SetTextColor(memory_dc_, RGB(255, 80, 80));

    std::wstring lines[5];
    int line_count = 0;
    if (!notice_.empty()) {
        lines[line_count++] = notice_;
    }
    lines[line_count++] = utf8_to_wide("거리: ") + format_number(map_.distance);
    lines[line_count++] = utf8_to_wide("박격포 거리: ") +
        (have_elevation_ ? utf8_to_wide(elevation_.mortar_label) : utf8_to_wide("없음"));
    lines[line_count++] = utf8_to_wide("고도: ") + (have_elevation_ ? format_number(elevation_.elevation) : utf8_to_wide("없음"));
    lines[line_count++] = utf8_to_wide("격자 간격: ") + format_number(map_.grid_gap);

    const int panel_w = 280;
    const int panel_h = 16 + line_count * 28;
    HBRUSH plate = CreateSolidBrush(RGB(20, 20, 20));
    RECT plate_rect{12, 12, 12 + panel_w, 12 + panel_h};
    FillRect(memory_dc_, &plate_rect, plate);
    DeleteObject(plate);
    for (int i = 0; i < line_count; ++i) {
        TextOutW(memory_dc_, 20, 18 + i * 28, lines[i].c_str(), static_cast<int>(lines[i].size()));
    }

    if (view_.map_marks) {
        std::optional<cv::Point> player;
        std::optional<cv::Point> mark;
        if (map_.player) {
            player = cv::Point(scaled(map_.player->x + origin_x), scaled(map_.player->y + origin_y));
            draw_circle(memory_dc_, *player, RGB(0, 0, 255));
        }
        if (map_.mark) {
            mark = cv::Point(scaled(map_.mark->x + origin_x), scaled(map_.mark->y + origin_y));
            draw_circle(memory_dc_, *mark, RGB(255, 0, 0));
        }
        if (player && mark) {
            HPEN pen = CreatePen(PS_SOLID, 3, RGB(0, 220, 0));
            HGDIOBJ old_pen = SelectObject(memory_dc_, pen);
            MoveToEx(memory_dc_, player->x, player->y, nullptr);
            LineTo(memory_dc_, mark->x, mark->y);
            SelectObject(memory_dc_, old_pen);
            DeleteObject(pen);
        }
    }
    if (view_.elevation_marks && have_elevation_ && elevation_.mark && elevation_.center) {
        const cv::Point mark(scaled(elevation_.mark->x + elevation_.x_start), scaled(elevation_.mark->y));
        const cv::Point center(scaled(elevation_.center->x), scaled(elevation_.center->y));
        draw_circle(memory_dc_, mark, RGB(255, 255, 0));
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 220, 0));
        HGDIOBJ old_pen = SelectObject(memory_dc_, pen);
        MoveToEx(memory_dc_, mark.x, mark.y, nullptr);
        LineTo(memory_dc_, center.x, center.y);
        SelectObject(memory_dc_, old_pen);
        DeleteObject(pen);
    }

    auto* pixels = static_cast<uint32_t*>(bits_);
    const size_t count = static_cast<size_t>(width_) * height_;
    for (size_t i = 0; i < count; ++i) {
        const uint32_t pixel = pixels[i];
        const uint32_t color = pixel & 0x00FFFFFFu;
        pixels[i] = color == 0 ? 0 : (color | 0xFF000000u);
    }

    POINT origin{0, 0};
    SIZE size{width_, height_};
    RECT window_rect{};
    GetWindowRect(hwnd_, &window_rect);
    POINT window_origin{window_rect.left, window_rect.top};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(hwnd_, screen_dc_, &window_origin, &size, memory_dc_, &origin, 0, &blend, ULW_ALPHA);
    dirty_ = false;

    SelectObject(memory_dc_, old_font);
    DeleteObject(font);
}

bool Overlay::excluded_from_capture() const {
    return excluded_;
}

HWND Overlay::hwnd() const {
    return hwnd_;
}
