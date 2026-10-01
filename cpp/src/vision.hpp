#pragma once

#include <opencv2/core.hpp>

#include <optional>
#include <string>
#include <vector>

struct Settings {
    std::string color = "green";
    int fov = 103;
    int min_radius = 5;
    int max_radius = 30;
    int canny1 = 20;
    int canny2 = 40;
    double line_threshold = 0.55;
    double line_gap = 0.10;
    int line_merge = 10;
    std::wstring window_title = L"PUBG:";

    bool debug_mode = false;
    std::string hotkey_map = "alt+1";
    std::string hotkey_elevation = "alt+2";
    std::string hotkey_both = "alt+3";

    bool voice_enabled = true;
    int voice_volume = 50;
    int voice_rate = 150;

    bool elevation_draw_points = true;
    bool elevation_draw_processed = false;

    bool minimap_enabled = false;
    int minimap_offset = 170;
    int minimap_small = 673;
    int minimap_large = 715;

    bool grid_show_processed = false;
    bool grid_draw_lines = true;

    bool mark_draw = true;
    bool mark_show_processed = false;
    bool mark_zoom = true;
    bool mark_yolo = false;

    bool overlay_enabled = false;
    bool overlay_capture_hidden = true;
    bool overlay_borders = false;
    bool overlay_map_marks = false;
    bool overlay_elevation_marks = false;
    bool overlay_minimap_box = false;
    int overlay_scale = 100;
};

struct OverlayView {
    bool enabled = false;
    bool borders = false;
    bool map_marks = false;
    bool elevation_marks = false;
    bool minimap_box = false;
    int scale = 100;
};

struct MapResult {
    std::optional<double> distance;
    std::optional<double> grid_gap;
    std::optional<cv::Point> player;
    std::optional<cv::Point> mark;
    int minimap_type = 0;
    std::optional<cv::Rect> minimap_box;
    cv::Mat preview;
};

struct ElevationResult {
    std::optional<double> elevation;
    std::optional<double> elevated_distance;
    std::string mortar_label = "없음";
    std::optional<cv::Point> mark;
    std::optional<cv::Point> center;
    int x_start = 0;
    cv::Mat preview;
};

class OnnxModels;

MapResult analyze_map(const cv::Mat& bgr, const Settings& settings, OnnxModels* models);
ElevationResult analyze_elevation(
    const cv::Mat& bgr,
    const Settings& settings,
    std::optional<double> planar_distance,
    const std::vector<int>& mortar_distances
);
std::vector<int> load_mortar_distances(const std::wstring& path);
cv::Mat read_image_file(const std::wstring& path);
bool write_image_file(const std::wstring& path, const cv::Mat& image);
