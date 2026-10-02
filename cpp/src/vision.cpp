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

bool hsv_bounds(const std::string& color, cv::Scalar& lower, cv::Scalar& upper) {
    if (color == "orange") {
        lower = cv::Scalar(4, 90, 110);
        upper = cv::Scalar(18, 255, 255);
    } else if (color == "yellow") {
        lower = cv::Scalar(18, 90, 120);
        upper = cv::Scalar(42, 255, 255);
    } else if (color == "blue") {
        lower = cv::Scalar(95, 70, 90);
        upper = cv::Scalar(125, 255, 255);
    } else if (color == "green") {
        lower = cv::Scalar(40, 70, 90);
        upper = cv::Scalar(85, 255, 255);
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
    const cv::Mat kernel_shape = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel_shape);
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

struct RadiusRange {
    float min_radius = 2;
    float max_radius = 40;
};

RadiusRange radius_for_image(const cv::Mat& image, int min_radius, int max_radius) {
    const double scale = std::max(image.cols, image.rows) / 1080.0;
    RadiusRange range;
    range.min_radius = std::max(2.0f, static_cast<float>(min_radius) * static_cast<float>(std::clamp(scale, 0.55, 2.5) * 0.7));
    range.max_radius = std::max(range.min_radius + 8.0f, static_cast<float>(max_radius) * static_cast<float>(std::clamp(scale, 1.0, 2.5) * 1.8));
    return range;
}

bool far_enough(cv::Point a, cv::Point b, float radius) {
    const double dx = static_cast<double>(a.x - b.x);
    const double dy = static_cast<double>(a.y - b.y);
    return std::hypot(dx, dy) >= std::max(16.0, static_cast<double>(radius) * 1.3);
}

bool marker_shape(const cv::Mat& mask, const std::vector<cv::Point>& contour, cv::Point2f center, float radius, double& circularity) {
    const double area = std::abs(cv::contourArea(contour));
    const double peri = cv::arcLength(contour, true);
    if (peri < 1.0 || area < 8.0 || radius < 1.0f) {
        return false;
    }
    circularity = 4.0 * CV_PI * area / (peri * peri);
    const cv::Rect box = cv::boundingRect(contour);
    const double aspect = static_cast<double>(box.width) / std::max(box.height, 1);
    if (circularity < 0.72 || aspect < 0.62 || aspect > 1.62) {
        return false;
    }

    int filled[8] = {};
    int total[8] = {};
    const int reach = std::max(2, static_cast<int>(std::ceil(radius)));
    const int cx = cvRound(center.x);
    const int cy = cvRound(center.y);
    const int reach_sq = reach * reach;
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (dx * dx + dy * dy > reach_sq) {
                continue;
            }
            const int x = cx + dx;
            const int y = cy + dy;
            if (x < 0 || y < 0 || x >= mask.cols || y >= mask.rows) {
                continue;
            }
            int sector = static_cast<int>((std::atan2(static_cast<double>(dy), static_cast<double>(dx)) + CV_PI) / (2.0 * CV_PI) * 8.0);
            sector = std::clamp(sector, 0, 7);
            ++total[sector];
            if (mask.at<uchar>(y, x) > 0) {
                ++filled[sector];
            }
        }
    }
    int covered = 0;
    int empty = 0;
    for (int i = 0; i < 8; ++i) {
        if (total[i] == 0) {
            ++empty;
            continue;
        }
        const double ratio = static_cast<double>(filled[i]) / total[i];
        if (ratio >= 0.18) {
            ++covered;
        } else if (ratio < 0.08) {
            ++empty;
        }
    }
    return covered >= 6 && empty <= 2;
}

void blackout_hud(cv::Mat& image) {
    const int width = image.cols;
    const int height = image.rows;
    fill_black(image, {0, static_cast<int>(height * 0.80)}, {static_cast<int>(width * 0.22), height});
    fill_black(image, {static_cast<int>(width * 0.75), static_cast<int>(height * 0.78)}, {width, height});
    fill_black(image, {static_cast<int>(width * 0.78), 0}, {width, static_cast<int>(height * 0.22)});
    fill_black(image, {static_cast<int>(width * 0.28), static_cast<int>(height * 0.88)}, {static_cast<int>(width * 0.72), height});
}

std::pair<std::optional<cv::Point>, std::optional<cv::Point>> find_player_and_mark(
    const cv::Mat& mask,
    float min_radius,
    float max_radius
) {
    struct Hit {
        cv::Point center;
        float radius = 0;
        double area = 0;
    };
    std::vector<Hit> hits;
    for (const auto& contour : contours_by_area(mask)) {
        cv::Point2f center;
        float radius = 0;
        cv::minEnclosingCircle(contour, center, radius);
        if (radius < 3.0f || radius >= max_radius) {
            continue;
        }
        double circularity = 0;
        if (!marker_shape(mask, contour, center, radius, circularity)) {
            continue;
        }
        const cv::Point point(cvRound(center.x), cvRound(center.y));
        bool duplicate = false;
        for (const Hit& hit : hits) {
            if (!far_enough(hit.center, point, std::max(hit.radius, radius))) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            hits.push_back({point, radius, std::abs(cv::contourArea(contour))});
        }
    }
    if (hits.size() >= 2) {
        std::vector<float> radii;
        radii.reserve(hits.size());
        for (const Hit& hit : hits) {
            radii.push_back(hit.radius);
        }
        std::nth_element(radii.begin(), radii.begin() + static_cast<std::ptrdiff_t>(radii.size() / 2), radii.end());
        const float typical = radii[radii.size() / 2];
        hits.erase(
            std::remove_if(hits.begin(), hits.end(), [&](const Hit& hit) {
                return hit.radius > typical * 1.8f && hit.radius > min_radius;
            }),
            hits.end()
        );
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.area > b.area; });
    if (hits.size() > 2) {
        hits.resize(2);
    }
    std::optional<cv::Point> player;
    std::optional<cv::Point> mark;
    if (!hits.empty()) {
        player = hits[0].center;
    }
    if (hits.size() >= 2) {
        mark = cv::Point(hits[1].center.x, cvRound(hits[1].center.y + hits[1].radius));
    }
    return {player, mark};
}

std::optional<cv::Point> find_largest_mark(const cv::Mat& mask, int min_radius, int max_radius) {
    std::optional<cv::Point> best;
    double best_roundness = -1;
    for (const auto& contour : contours_by_area(mask)) {
        cv::Point2f center;
        float radius = 0;
        cv::minEnclosingCircle(contour, center, radius);
        if (!(min_radius < radius && radius < max_radius)) {
            continue;
        }
        double circularity = 0;
        if (!marker_shape(mask, contour, center, radius, circularity)) {
            continue;
        }
        if (circularity > best_roundness) {
            best_roundness = circularity;
            best = cv::Point(cvRound(center.x), cvRound(center.y));
        }
    }
    return best;
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
    std::optional<double> pitch;
};

struct PitchEstimate {
    double value = 0;
    int support = 0;
};

std::vector<double> line_positions(const std::vector<Segment>& lines, bool horizontal) {
    std::vector<double> positions;
    positions.reserve(lines.size());
    for (const Segment& line : lines) {
        positions.push_back(horizontal ? static_cast<double>(line.y0) : static_cast<double>(line.x0));
    }
    std::sort(positions.begin(), positions.end());
    return positions;
}

void append_gaps(const std::vector<double>& positions, std::vector<double>& gaps) {
    for (size_t i = 1; i < positions.size(); ++i) {
        const double gap = positions[i] - positions[i - 1];
        if (gap > 1.0) {
            gaps.push_back(gap);
        }
    }
}

PitchEstimate majority_pitch(const std::vector<double>& gaps) {
    PitchEstimate result;
    if (gaps.empty()) {
        return result;
    }
    std::vector<double> sorted = gaps;
    std::sort(sorted.begin(), sorted.end());
    int best_count = 0;
    double best_sum = 0;
    for (double center : sorted) {
        const double tolerance = std::max(3.0, center * 0.06);
        double sum = 0;
        int count = 0;
        for (double gap : sorted) {
            if (std::abs(gap - center) <= tolerance) {
                sum += gap;
                ++count;
            }
        }
        if (count > best_count) {
            best_count = count;
            best_sum = sum;
        }
    }
    result.support = best_count;
    result.value = best_sum / static_cast<double>(best_count);
    const bool repeated = best_count >= 2 || best_count * 2 > static_cast<int>(sorted.size());
    if (!repeated) {
        result.value = sorted.size() % 2 == 0
                           ? (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0
                           : sorted[sorted.size() / 2];
    }
    return result;
}

struct AxisFit {
    double anchor = 0;
    double origin = 0;
};

std::optional<AxisFit> fit_axis(const std::vector<double>& positions, double pitch) {
    if (positions.size() < 2 || pitch < 4.0) {
        return std::nullopt;
    }
    const double tolerance = std::max(6.0, pitch * 0.18);
    int best_count = 0;
    double anchor = positions.front();
    for (double candidate : positions) {
        int count = 0;
        for (double pos : positions) {
            const double steps = std::round((pos - candidate) / pitch);
            if (std::abs((pos - candidate) - steps * pitch) <= tolerance) {
                ++count;
            }
        }
        if (count > best_count) {
            best_count = count;
            anchor = candidate;
        }
    }
    if (best_count < 2 || (best_count < 3 && best_count * 2 < static_cast<int>(positions.size()))) {
        return std::nullopt;
    }
    std::vector<double> origins;
    origins.reserve(static_cast<size_t>(best_count));
    for (double pos : positions) {
        const double steps = std::round((pos - anchor) / pitch);
        if (std::abs((pos - anchor) - steps * pitch) <= tolerance) {
            origins.push_back(pos - steps * pitch);
        }
    }
    if (origins.empty()) {
        return std::nullopt;
    }
    std::sort(origins.begin(), origins.end());
    const size_t mid = origins.size() / 2;
    const double origin = origins.size() % 2 == 0 ? (origins[mid - 1] + origins[mid]) / 2.0 : origins[mid];
    return AxisFit{anchor, origin};
}

std::vector<Segment> synthesize_lines(double origin, double pitch, int extent, int span, bool horizontal) {
    std::vector<Segment> lines;
    if (pitch < 4.0 || extent <= 1 || span <= 1) {
        return lines;
    }
    const int k_min = static_cast<int>(std::ceil((-0.5 - origin) / pitch));
    const int k_max = static_cast<int>(std::floor((static_cast<double>(extent) - 0.5 - origin) / pitch));
    if (k_max < k_min || k_max - k_min > extent) {
        return lines;
    }
    for (int k = k_min; k <= k_max; ++k) {
        const int pos = static_cast<int>(std::lround(origin + static_cast<double>(k) * pitch));
        if (pos < 0 || pos >= extent) {
            continue;
        }
        if (!lines.empty()) {
            const int previous = horizontal ? lines.back().y0 : lines.back().x0;
            if (previous == pos) {
                continue;
            }
        }
        if (horizontal) {
            lines.push_back({0, pos, span - 1, pos});
        } else {
            lines.push_back({pos, 0, pos, span - 1});
        }
    }
    return lines;
}

void complete_grid(GridDetect& grid, int cols, int rows) {
    const std::vector<double> horizontal = line_positions(grid.horizontal, true);
    const std::vector<double> vertical = line_positions(grid.vertical, false);
    std::vector<double> gaps;
    append_gaps(horizontal, gaps);
    append_gaps(vertical, gaps);
    const PitchEstimate estimate = majority_pitch(gaps);
    if (estimate.value <= 1.0) {
        return;
    }
    double pitch = estimate.value;
    if (estimate.support >= 2) {
        const double tolerance = std::max(6.0, pitch * 0.18);
        double numerator = 0;
        double denominator = 0;
        auto accumulate = [&](const std::optional<AxisFit>& fit, const std::vector<double>& positions) {
            if (!fit) {
                return;
            }
            for (double pos : positions) {
                const double steps = std::round((pos - fit->anchor) / pitch);
                if (std::abs((pos - fit->anchor) - steps * pitch) > tolerance) {
                    continue;
                }
                numerator += steps * (pos - fit->anchor);
                denominator += steps * steps;
            }
        };
        const std::optional<AxisFit> horizontal_fit = fit_axis(horizontal, pitch);
        const std::optional<AxisFit> vertical_fit = fit_axis(vertical, pitch);
        accumulate(horizontal_fit, horizontal);
        accumulate(vertical_fit, vertical);
        if (denominator >= 1.0) {
            const double refined = numerator / denominator;
            if (refined > pitch * 0.85 && refined < pitch * 1.15) {
                pitch = refined;
            }
        }
    }
    grid.pitch = pitch;
    if (estimate.support < 2 || cols <= 1 || rows <= 1) {
        return;
    }
    if (const std::optional<AxisFit> fit = fit_axis(horizontal, pitch)) {
        const std::vector<Segment> predicted = synthesize_lines(fit->origin, pitch, rows, cols, true);
        if (!predicted.empty()) {
            grid.horizontal = predicted;
        }
    }
    if (const std::optional<AxisFit> fit = fit_axis(vertical, pitch)) {
        const std::vector<Segment> predicted = synthesize_lines(fit->origin, pitch, cols, rows, false);
        if (!predicted.empty()) {
            grid.vertical = predicted;
        }
    }
}

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
    std::vector<double> gaps;
    append_gaps(line_positions(horizontal, true), gaps);
    append_gaps(line_positions(vertical, false), gaps);
    const PitchEstimate estimate = majority_pitch(gaps);
    if (estimate.value <= 1.0) {
        return std::nullopt;
    }
    return estimate.value;
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

std::vector<YoloBox> detect_tiles(OnnxModels& models, const cv::Mat& image) {
    if (image.empty()) {
        return {};
    }
    auto pieces = [](int length) {
        if (length <= 800) {
            return 1;
        }
        if (length <= 1400) {
            return 2;
        }
        return 3;
    };
    const int across = pieces(image.cols);
    const int down = pieces(image.rows);
    if (across == 1 && down == 1) {
        return models.detect(image);
    }
    auto window_for = [](int length, int count) {
        if (count <= 1) {
            return length;
        }
        const double factor = (count - 1) * 0.8 + 1.0;
        return std::min(length, std::max(320, static_cast<int>(std::ceil(length / factor))));
    };
    const int tile_w = window_for(image.cols, across);
    const int tile_h = window_for(image.rows, down);
    std::vector<YoloBox> found;
    auto keep = [&](YoloBox box) {
        const int cx = (box.x0 + box.x1) / 2;
        const int cy = (box.y0 + box.y1) / 2;
        for (YoloBox& kept : found) {
            if (kept.class_id != box.class_id) {
                continue;
            }
            const int kx = (kept.x0 + kept.x1) / 2;
            const int ky = (kept.y0 + kept.y1) / 2;
            if (std::abs(kx - cx) + std::abs(ky - cy) < 28) {
                if (box.confidence > kept.confidence) {
                    kept = box;
                }
                return;
            }
        }
        found.push_back(std::move(box));
    };
    for (int row = 0; row < down; ++row) {
        const int top = down == 1 ? 0 : (image.rows - tile_h) * row / (down - 1);
        for (int col = 0; col < across; ++col) {
            const int left = across == 1 ? 0 : (image.cols - tile_w) * col / (across - 1);
            const cv::Rect tile(left, top, tile_w, tile_h);
            for (YoloBox box : models.detect(image(tile))) {
                box.x0 += left;
                box.y0 += top;
                box.x1 += left;
                box.y1 += top;
                keep(std::move(box));
            }
        }
    }
    return found;
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

    GridDetect grid = detect_grid_lines(image, settings);
    complete_grid(grid, image.cols, image.rows);
    result.grid_gap = grid.pitch ? grid.pitch : calculate_grid_gap(grid.horizontal, grid.vertical);

    cv::Mat hsv_mask;
    std::vector<cv::Rect> samples;
    const bool model_ready = models != nullptr && models->has_mark();
    const bool use_yolo = settings.mark_yolo && model_ready;
    const RadiusRange radii = radius_for_image(image, settings.min_radius, settings.max_radius);
    cv::Mat mark_image = image.clone();
    blackout_hud(mark_image);
    hsv_mask = color_mask(mark_image, settings.color, use_yolo ? 5 : 3, use_yolo ? 8 : 18);
    const auto [player, mark] = find_player_and_mark(hsv_mask, radii.min_radius, radii.max_radius);
    result.player = player;
    result.mark = mark;

    auto take_yolo = [&](const std::vector<YoloBox>& boxes) {
        for (const YoloBox& box : boxes) {
            if (box.name.find(settings.color) == std::string::npos) {
                continue;
            }
            const bool is_player = box.name.find("player") != std::string::npos;
            const cv::Point center((box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2);
            if (is_player && !result.player) {
                result.player = center;
            } else if (!is_player && !result.mark) {
                result.mark = cv::Point(center.x, box.y1);
            }
        }
    };

    if (use_yolo || ((!result.player || !result.mark) && model_ready)) {
        std::vector<YoloBox> unique;
        auto absorb = [&](YoloBox box) {
            for (const YoloBox& kept : unique) {
                if (kept.class_id == box.class_id && std::abs(kept.x0 - box.x0) + std::abs(kept.y0 - box.y0) < 10) {
                    return;
                }
            }
            unique.push_back(box);
        };
        if (use_yolo) {
            for (const CircleMark& circle : all_circles(hsv_mask)) {
                if (!(radii.min_radius < circle.radius && circle.radius < radii.max_radius * 1.4f)) {
                    continue;
                }
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
                    absorb(box);
                }
            }
        }
        if (!result.player || !result.mark || unique.empty()) {
            for (const YoloBox& box : detect_tiles(*models, image)) {
                absorb(box);
            }
        }
        take_yolo(unique);
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
    const cv::Mat mask = color_mask(strip, settings.color, 5, 8);
    const RadiusRange radii = radius_for_image(bgr, settings.min_radius, settings.max_radius);
    const std::optional<cv::Point> local = find_largest_mark(
        mask,
        std::max(1, static_cast<int>(radii.min_radius)),
        std::max(4, static_cast<int>(radii.max_radius))
    );
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
