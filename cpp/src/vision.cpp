#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "vision.hpp"

#include "onnx_models.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace {

constexpr double kGridMeters = 100.0;

struct Segment {
    int x0;
    int y0;
    int x1;
    int y1;
};

void fill_black(cv::Mat& image, cv::Point a, cv::Point b) {
    const int x0 = std::clamp(std::min(a.x, b.x), 0, image.cols);
    const int y0 = std::clamp(std::min(a.y, b.y), 0, image.rows);
    const int x1 = std::clamp(std::max(a.x, b.x), 0, image.cols);
    const int y1 = std::clamp(std::max(a.y, b.y), 0, image.rows);
    if (x1 > x0 && y1 > y0) {
        image(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(cv::Scalar(0, 0, 0));
    }
}

void remove_danger_zones(cv::Mat& image) {
    const int width = image.cols;
    const int height = image.rows;
    fill_black(image, {0, static_cast<int>(height * 0.83)}, {static_cast<int>(width * 0.13), height});
    fill_black(image, {static_cast<int>(width * 0.75), static_cast<int>(height * 0.8)}, {width, height});
    fill_black(image, {static_cast<int>(width * 0.8), 0}, {width, static_cast<int>(height * 0.25)});
    fill_black(
        image,
        {static_cast<int>(width * 0.3), static_cast<int>(height * 0.9)},
        {static_cast<int>(width * 0.7), height}
    );
}

bool hsv_bounds(const std::string& color, cv::Scalar& lower, cv::Scalar& upper) {
    if (color == "orange") {
        lower = cv::Scalar(10, 106, 123);
        upper = cv::Scalar(13, 238, 231);
    } else if (color == "yellow") {
        lower = cv::Scalar(23, 137, 163);
        upper = cv::Scalar(36, 255, 240);
    } else if (color == "blue") {
        lower = cv::Scalar(73, 65, 156);
        upper = cv::Scalar(117, 203, 224);
    } else if (color == "green") {
        lower = cv::Scalar(49, 101, 111);
        upper = cv::Scalar(80, 195, 219);
    } else {
        return false;
    }
    return true;
}

cv::Mat color_mask(const cv::Mat& bgr, const std::string& color, int blur, int threshold_value) {
    cv::Scalar lower;
    cv::Scalar upper;
    if (!hsv_bounds(color, lower, upper)) {
        return cv::Mat(bgr.rows, bgr.cols, CV_8UC1, cv::Scalar(0));
    }
    cv::Mat hsv;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::Mat mask;
    cv::inRange(hsv, lower, upper, mask);
    const int kernel = blur % 2 == 0 ? blur + 1 : blur;
    cv::GaussianBlur(mask, mask, cv::Size(kernel, kernel), 7);
    cv::threshold(mask, mask, threshold_value, 255, cv::THRESH_BINARY);
    return mask;
}

std::vector<std::vector<cv::Point>> contours_by_area(const cv::Mat& mask) {
    std::vector<std::vector<cv::Point>> contours;
    cv::Mat clone = mask.clone();
    cv::findContours(clone, contours, cv::RETR_CCOMP, cv::CHAIN_APPROX_SIMPLE);
    std::sort(contours.begin(), contours.end(), [](const auto& a, const auto& b) {
        return cv::contourArea(a) > cv::contourArea(b);
    });
    return contours;
}

std::pair<std::optional<cv::Point>, std::optional<cv::Point>> find_player_and_mark(
    const cv::Mat& mask,
    int min_radius,
    int max_radius
) {
    std::optional<cv::Point> player;
    std::optional<cv::Point> mark;
    for (const auto& contour : contours_by_area(mask)) {
        cv::Point2f center;
        float radius = 0;
        cv::minEnclosingCircle(contour, center, radius);
        if (!(min_radius < radius && radius < max_radius)) {
            continue;
        }
        if (!player) {
            player = cv::Point(cvRound(center.x), cvRound(center.y));
        } else if (!mark) {
            mark = cv::Point(cvRound(center.x), cvRound(center.y + radius));
            break;
        }
    }
    return {player, mark};
}

std::optional<cv::Point> find_largest_mark(const cv::Mat& mask, int min_radius, int max_radius) {
    for (const auto& contour : contours_by_area(mask)) {
        cv::Point2f center;
        float radius = 0;
        cv::minEnclosingCircle(contour, center, radius);
        if (min_radius < radius && radius < max_radius) {
            return cv::Point(cvRound(center.x), cvRound(center.y));
        }
    }
    return std::nullopt;
}

Segment average_cluster(const std::vector<Segment>& cluster, int index) {
    int sum = 0;
    for (const Segment& line : cluster) {
        sum += index == 1 ? line.y0 : line.x0;
    }
    const int average = static_cast<int>(sum / static_cast<int>(cluster.size()));
    if (index == 1) {
        int min_x = cluster[0].x0;
        int max_x = cluster[0].x0;
        for (const Segment& line : cluster) {
            min_x = std::min(min_x, std::min(line.x0, line.x1));
            max_x = std::max(max_x, std::max(line.x0, line.x1));
        }
        return {min_x, average, max_x, average};
    }
    int min_y = cluster[0].y0;
    int max_y = cluster[0].y0;
    for (const Segment& line : cluster) {
        min_y = std::min(min_y, std::min(line.y0, line.y1));
        max_y = std::max(max_y, std::max(line.y0, line.y1));
    }
    return {average, min_y, average, max_y};
}

std::vector<Segment> merge_lines(std::vector<Segment> lines, int index, int threshold) {
    if (lines.empty()) {
        return {};
    }
    std::sort(lines.begin(), lines.end(), [index](const Segment& a, const Segment& b) {
        return (index == 1 ? a.y0 : a.x0) < (index == 1 ? b.y0 : b.x0);
    });
    std::vector<Segment> merged;
    std::vector<Segment> cluster{lines.front()};
    for (size_t i = 1; i < lines.size(); ++i) {
        const int current = index == 1 ? lines[i].y0 : lines[i].x0;
        const int previous = index == 1 ? cluster.back().y0 : cluster.back().x0;
        if (std::abs(current - previous) < threshold) {
            cluster.push_back(lines[i]);
        } else {
            merged.push_back(average_cluster(cluster, index));
            cluster = {lines[i]};
        }
    }
    merged.push_back(average_cluster(cluster, index));
    return merged;
}

struct GridDetect {
    std::vector<Segment> horizontal;
    std::vector<Segment> vertical;
    cv::Mat edges;
};

struct CircleMark {
    int x;
    int y;
    float radius;
};

GridDetect detect_grid_lines(const cv::Mat& bgr, const Settings& settings) {
    GridDetect result;
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Canny(gray, result.edges, settings.canny1, settings.canny2, 3);

    const int max_resolution = std::max(result.edges.rows, result.edges.cols);
    if (max_resolution <= 0) {
        return result;
    }
    const double scale_x = static_cast<double>(result.edges.cols) / max_resolution;
    const double scale_y = static_cast<double>(result.edges.rows) / max_resolution;
    cv::Mat square;
    cv::resize(result.edges, square, cv::Size(max_resolution, max_resolution), 0, 0, cv::INTER_NEAREST);

    const int line_threshold = std::max(1, static_cast<int>(settings.line_threshold * square.rows));
    const int line_gap = std::max(0, static_cast<int>(settings.line_gap * square.rows));
    std::vector<cv::Vec4i> raw_lines;
    cv::HoughLinesP(square, raw_lines, 1, CV_PI / 2.0, line_threshold, 0, line_gap);

    std::vector<Segment> horizontal;
    std::vector<Segment> vertical;
    for (const cv::Vec4i& raw : raw_lines) {
        Segment line{
            static_cast<int>(std::lround(raw[0] * scale_x)),
            static_cast<int>(std::lround(raw[1] * scale_y)),
            static_cast<int>(std::lround(raw[2] * scale_x)),
            static_cast<int>(std::lround(raw[3] * scale_y)),
        };
        if (std::abs(line.x1 - line.x0) < std::abs(line.y1 - line.y0)) {
            vertical.push_back(line);
        } else {
            horizontal.push_back(line);
        }
    }
    result.horizontal = merge_lines(std::move(horizontal), 1, settings.line_merge);
    result.vertical = merge_lines(std::move(vertical), 0, settings.line_merge);
    return result;
}

std::vector<CircleMark> all_circles(const cv::Mat& mask) {
    std::vector<CircleMark> marks;
    for (const auto& contour : contours_by_area(mask)) {
        cv::Point2f center;
        float radius = 0;
        cv::minEnclosingCircle(contour, center, radius);
        marks.push_back({static_cast<int>(center.x), static_cast<int>(center.y), radius});
    }
    return marks;
}

void draw_grid(cv::Mat& image, const std::vector<Segment>& horizontal, const std::vector<Segment>& vertical) {
    if (image.empty()) {
        return;
    }
    const int thickness = std::max(1, static_cast<int>(((image.cols * 0.002) + (image.rows * 0.002)) / 2.0));
    for (const Segment& line : horizontal) {
        cv::line(image, {line.x0, line.y0}, {line.x1, line.y1}, cv::Scalar(255, 0, 0), thickness);
    }
    for (const Segment& line : vertical) {
        cv::line(image, {line.x0, line.y0}, {line.x1, line.y1}, cv::Scalar(0, 0, 255), thickness);
    }
}

void paint_label(cv::Mat& image, const std::wstring& text, int x, int y, int font_px) {
    if (image.empty() || text.empty()) {
        return;
    }
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HFONT font = CreateFontW(
        font_px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic"
    );
    HGDIOBJ old_font = SelectObject(memory, font);
    SIZE size{};
    GetTextExtentPoint32W(memory, text.c_str(), static_cast<int>(text.size()), &size);
    if (size.cx <= 0 || size.cy <= 0) {
        SelectObject(memory, old_font);
        DeleteObject(font);
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size.cx;
    info.bmiHeader.biHeight = -size.cy;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old_bitmap = SelectObject(memory, dib);
    RECT rect{0, 0, size.cx, size.cy};
    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(memory, &rect, black);
    DeleteObject(black);
    SetBkMode(memory, TRANSPARENT);
    SetTextColor(memory, RGB(255, 255, 255));
    TextOutW(memory, 0, 0, text.c_str(), static_cast<int>(text.size()));
    auto* pixels = static_cast<uint32_t*>(bits);
    for (int row = 0; row < size.cy; ++row) {
        const int dest_y = y + row;
        if (dest_y < 0 || dest_y >= image.rows) {
            continue;
        }
        for (int col = 0; col < size.cx; ++col) {
            const int dest_x = x + col;
            if (dest_x < 0 || dest_x >= image.cols) {
                continue;
            }
            const uint32_t pixel = pixels[row * size.cx + col];
            if (((pixel >> 16) & 255) < 40) {
                continue;
            }
            image.at<cv::Vec3b>(dest_y, dest_x) = cv::Vec3b(255, 255, 255);
        }
    }
    SelectObject(memory, old_bitmap);
    SelectObject(memory, old_font);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
}

void draw_point(cv::Mat& image, cv::Point position, const std::wstring& title, const cv::Scalar& bgr) {
    const int base = std::min(image.rows, image.cols);
    const int radius = std::max(1, static_cast<int>(0.02 * base));
    const int thickness = std::max(1, static_cast<int>(0.005 * base));
    cv::circle(image, position, radius, bgr, thickness);

    const int font_px = std::max(12, static_cast<int>(0.002 * base * 22));
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    HFONT font = CreateFontW(
        font_px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic"
    );
    SelectObject(memory, font);
    SIZE size{};
    GetTextExtentPoint32W(memory, title.c_str(), static_cast<int>(title.size()), &size);
    DeleteObject(font);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);

    int bg_x = position.x - size.cx / 2;
    int bg_y = position.y - size.cy - 10;
    const int x_max = std::max(0, image.cols - static_cast<int>(size.cx));
    const int y_min = static_cast<int>(size.cy) + 10;
    const int y_max = std::max(y_min, image.rows - 10);
    bg_x = std::clamp(bg_x, 0, x_max);
    bg_y = std::clamp(bg_y, y_min, y_max);
    cv::rectangle(image, {bg_x - 5, bg_y - size.cy - 5}, {bg_x + size.cx + 5, bg_y + 5}, cv::Scalar(0, 0, 0), cv::FILLED);
    paint_label(image, title, bg_x, bg_y - size.cy, font_px);
}

cv::Mat crop_points(const cv::Mat& image, cv::Point a, cv::Point b, double margin) {
    int left = std::min(a.x, b.x);
    int right = std::max(a.x, b.x);
    int top = std::min(a.y, b.y);
    int bottom = std::max(a.y, b.y);
    const int margin_x = static_cast<int>(std::lround(image.cols * margin));
    const int margin_y = static_cast<int>(std::lround(image.rows * margin));
    left = std::clamp(left - margin_x, 0, image.cols);
    right = std::clamp(right + margin_x, 0, image.cols);
    top = std::clamp(top - margin_y, 0, image.rows);
    bottom = std::clamp(bottom + margin_y, 0, image.rows);
    if (right - left < 2 || bottom - top < 2) {
        return image.clone();
    }
    return image(cv::Rect(left, top, right - left, bottom - top)).clone();
}

std::optional<double> calculate_grid_gap(
    const std::vector<Segment>& horizontal,
    const std::vector<Segment>& vertical
) {
    std::vector<Segment> sorted_h = horizontal;
    std::vector<Segment> sorted_v = vertical;
    std::sort(sorted_h.begin(), sorted_h.end(), [](const Segment& a, const Segment& b) {
        return (a.y0 + a.y1) < (b.y0 + b.y1);
    });
    std::sort(sorted_v.begin(), sorted_v.end(), [](const Segment& a, const Segment& b) {
        return (a.x0 + a.x1) < (b.x0 + b.x1);
    });

    std::vector<double> gaps;
    for (size_t i = 1; i < sorted_h.size(); ++i) {
        gaps.push_back(std::abs(static_cast<double>(sorted_h[i].y0 - sorted_h[i - 1].y0)));
    }
    for (size_t i = 1; i < sorted_v.size(); ++i) {
        gaps.push_back(std::abs(static_cast<double>(sorted_v[i].x0 - sorted_v[i - 1].x0)));
    }
    if (gaps.empty()) {
        return std::nullopt;
    }
    std::sort(gaps.begin(), gaps.end());
    const double median = gaps.size() % 2 == 0
                              ? (gaps[gaps.size() / 2 - 1] + gaps[gaps.size() / 2]) / 2.0
                              : gaps[gaps.size() / 2];
    if (median <= 0) {
        return std::nullopt;
    }
    double sum = 0;
    int count = 0;
    for (double gap : gaps) {
        if (std::abs(gap - median) <= 0.01 * median) {
            sum += gap;
            ++count;
        }
    }
    if (count > 0) {
        return sum / count;
    }
    return median;
}

std::optional<double> planar_distance(cv::Point player, cv::Point mark, double grid_gap) {
    if (grid_gap == 0) {
        return std::nullopt;
    }
    const double delta_x = std::abs(player.x - mark.x) / grid_gap * kGridMeters;
    const double delta_y = std::abs(player.y - mark.y) / grid_gap * kGridMeters;
    return std::sqrt(delta_x * delta_x + delta_y * delta_y);
}

double elevation_meters(int center_y, int mark_y, double fov_deg, double distance) {
    if (center_y == 0) {
        return 0;
    }
    constexpr double aspect = 16.0 / 9.0;
    const double vertical_fov =
        std::atan(std::tan((fov_deg * CV_PI / 180.0) / 2.0) / aspect) * 2.0;
    const double pixels = static_cast<double>(mark_y - center_y);
    const double angle = std::atan(std::tan(vertical_fov / 2.0) / center_y * pixels);
    return -std::tan(angle) * distance;
}

double elevated_distance(double distance, double elevation) {
    constexpr double max_mortar_distance = 700.0;
    if (elevation == 0) {
        return distance;
    }
    const double tan_beta = elevation / distance;
    const double discriminant = max_mortar_distance * max_mortar_distance -
                                2 * distance * max_mortar_distance * tan_beta - distance * distance;
    if (discriminant < 0) {
        return 1000;
    }
    const double sqrt_term = std::sqrt(discriminant);
    return (distance + tan_beta * (max_mortar_distance - sqrt_term)) / (tan_beta * tan_beta + 1);
}

}  // namespace

MapResult analyze_map(const cv::Mat& bgr, const Settings& settings, OnnxModels* models) {
    MapResult result;
    if (bgr.empty()) {
        return result;
    }
    cv::Mat image = bgr.clone();
    if (settings.minimap_enabled && models != nullptr && models->has_minimap()) {
        result.minimap_type = models->classify_minimap(image);
        if (result.minimap_type != 0) {
            const int width = image.cols;
            const int height = image.rows;
            const int offset = static_cast<int>(width * (settings.minimap_offset / 10000.0));
            const double size_scale = result.minimap_type == 1 ? settings.minimap_small / 5000.0 : settings.minimap_large / 3000.0;
            const int x1 = width - offset;
            const int y1 = height - offset;
            const int x0 = x1 - static_cast<int>(width * size_scale);
            const int y0 = y1 - static_cast<int>(width * size_scale);
            const int left = std::clamp(std::min(x0, x1), 0, width);
            const int right = std::clamp(std::max(x0, x1), 0, width);
            const int top = std::clamp(std::min(y0, y1), 0, height);
            const int bottom = std::clamp(std::max(y0, y1), 0, height);
            if (right - left > 1 && bottom - top > 1) {
                result.minimap_box = cv::Rect(left, top, right - left, bottom - top);
                image = image(*result.minimap_box).clone();
            }
        }
    }

    const GridDetect grid = detect_grid_lines(image, settings);
    result.grid_gap = calculate_grid_gap(grid.horizontal, grid.vertical);
    if (result.minimap_type == 0) {
        remove_danger_zones(image);
    }

    cv::Mat hsv_mask;
    std::vector<cv::Rect> samples;
    const bool use_yolo = settings.mark_yolo && models != nullptr && models->has_mark();
    if (!use_yolo) {
        hsv_mask = color_mask(image, settings.color, 3, 30);
        const auto [player, mark] = find_player_and_mark(hsv_mask, settings.min_radius, settings.max_radius);
        result.player = player;
        result.mark = mark;
    } else {
        hsv_mask = color_mask(image, settings.color, 19, 1);
        std::vector<YoloBox> unique;
        for (const CircleMark& circle : all_circles(hsv_mask)) {
            const int margin = std::max(static_cast<int>(std::lround(circle.radius * 3.0)), 50);
            const int x_min = std::max(0, circle.x - margin);
            const int y_min = std::max(0, circle.y - margin);
            const int x_max = std::min(image.cols, circle.x + margin);
            const int y_max = std::min(image.rows, circle.y + margin);
            if (x_max <= x_min || y_max <= y_min) {
                continue;
            }
            const cv::Rect sample(x_min, y_min, x_max - x_min, y_max - y_min);
            samples.push_back(sample);
            for (YoloBox box : models->detect(image(sample))) {
                box.x0 += x_min;
                box.y0 += y_min;
                box.x1 += x_min;
                box.y1 += y_min;
                bool exists = false;
                for (const YoloBox& kept : unique) {
                    if (kept.class_id != box.class_id) {
                        continue;
                    }
                    if (std::abs(kept.x0 - box.x0) + std::abs(kept.y0 - box.y0) < 10) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    unique.push_back(box);
                }
            }
        }
        for (const YoloBox& box : unique) {
            if (box.name.find(settings.color) == std::string::npos) {
                continue;
            }
            const bool is_player = box.name.find("player") != std::string::npos;
            const cv::Point center((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2);
            if (is_player && !result.player) {
                result.player = center;
            } else if (!result.mark) {
                result.mark = cv::Point(center.x, box.y1);
            }
        }
    }

    if (result.player && result.mark && result.grid_gap) {
        result.distance = planar_distance(*result.player, *result.mark, *result.grid_gap);
    }

    cv::Mat preview = image;
    if (settings.grid_show_processed && !grid.edges.empty()) {
        cv::cvtColor(grid.edges, preview, cv::COLOR_GRAY2BGR);
    } else if (settings.mark_show_processed && !hsv_mask.empty()) {
        cv::cvtColor(hsv_mask, preview, cv::COLOR_GRAY2BGR);
        if (use_yolo) {
            for (const cv::Rect& sample : samples) {
                cv::rectangle(preview, sample, cv::Scalar(0, 0, 255), 3);
            }
        }
    }
    if (settings.grid_draw_lines) {
        draw_grid(preview, grid.horizontal, grid.vertical);
    }
    if (settings.mark_draw) {
        if (result.player) {
            draw_point(preview, *result.player, L"플레이어", cv::Scalar(255, 0, 0));
        }
        if (result.mark) {
            draw_point(preview, *result.mark, L"마커", cv::Scalar(0, 0, 255));
        }
    }
    if (result.player && result.mark && settings.mark_zoom) {
        preview = crop_points(preview, *result.player, *result.mark, 0.05);
    }
    result.preview = preview;
    return result;
}

ElevationResult analyze_elevation(
    const cv::Mat& bgr,
    const Settings& settings,
    std::optional<double> planar,
    const std::vector<int>& mortar_distances
) {
    ElevationResult result;
    if (bgr.empty()) {
        return result;
    }
    cv::Mat image = bgr.clone();
    const int cut_y = static_cast<int>(image.rows * 0.1);
    fill_black(image, {0, 0}, {image.cols, cut_y});

    const int center_x = image.cols / 2;
    const int center_y = image.rows / 2;
    result.center = cv::Point(center_x, center_y);
    const double gap = image.cols * 0.02;
    int x0 = static_cast<int>(std::lround(center_x - gap));
    int x1 = static_cast<int>(std::lround(x0 + gap * 2));
    x0 = std::clamp(x0, 0, std::max(0, image.cols - 1));
    x1 = std::clamp(x1, x0 + 1, image.cols);
    result.x_start = x0;
    const cv::Mat strip = image(cv::Rect(x0, 0, x1 - x0, image.rows));
    const cv::Mat mask = color_mask(strip, settings.color, 19, 1);
    const std::optional<cv::Point> local = find_largest_mark(mask, settings.min_radius, settings.max_radius);
    result.mark = local;

    if (local && planar) {
        result.elevation = elevation_meters(center_y, local->y, settings.fov, *planar);
        result.elevated_distance = elevated_distance(*planar, *result.elevation);
        if (*result.elevated_distance < 120) {
            result.mortar_label = "너무 가까움";
        } else if (*result.elevated_distance > 705) {
            result.mortar_label = "너무 멈";
        } else if (!mortar_distances.empty()) {
            const int nearest = *std::min_element(
                mortar_distances.begin(),
                mortar_distances.end(),
                [&](int a, int b) {
                    return std::abs(a - *result.elevated_distance) < std::abs(b - *result.elevated_distance);
                }
            );
            result.mortar_label = std::to_string(nearest);
        }
    }

    cv::Mat view = strip.clone();
    if (settings.elevation_draw_processed) {
        cv::cvtColor(mask, view, cv::COLOR_GRAY2BGR);
    }
    if (settings.elevation_draw_points) {
        const cv::Point strip_center(view.cols / 2, view.rows / 2);
        cv::circle(view, strip_center, 2, cv::Scalar(0, 255, 0), 5);
        if (local) {
            cv::arrowedLine(view, strip_center, *local, cv::Scalar(0, 255, 0), 3);
            cv::circle(view, *local, 2, cv::Scalar(0, 255, 0), 5);
        }
    }
    if (cut_y < view.rows) {
        result.preview = view(cv::Rect(0, cut_y, view.cols, view.rows - cut_y)).clone();
    }
    return result;
}

std::vector<int> load_mortar_distances(const std::wstring& path) {
    std::ifstream file{std::filesystem::path(path)};
    std::vector<int> distances;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        try {
            distances.push_back(std::stoi(line));
        } catch (const std::exception&) {
        }
    }
    return distances;
}

cv::Mat read_image_file(const std::wstring& path) {
    std::ifstream file{std::filesystem::path(path), std::ios::binary};
    if (!file) {
        return {};
    }
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.empty()) {
        return {};
    }
    return cv::imdecode(bytes, cv::IMREAD_COLOR);
}

bool write_image_file(const std::wstring& path, const cv::Mat& image) {
    if (image.empty()) {
        return false;
    }
    std::vector<unsigned char> bytes;
    if (!cv::imencode(".png", image, bytes)) {
        return false;
    }
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path());
    }
    std::ofstream file{target, std::ios::binary};
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return file.good();
}
