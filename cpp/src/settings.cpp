#include "settings.hpp"

#include "utf.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

std::string read_file(const std::filesystem::path& file) {
    std::ifstream stream{file, std::ios::binary};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

size_t find_value(const std::string& json, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const size_t key_pos = json.find(pattern);
    if (key_pos == std::string::npos) {
        return std::string::npos;
    }
    const size_t colon = json.find(':', key_pos + pattern.size());
    if (colon == std::string::npos) {
        return std::string::npos;
    }
    size_t pos = colon + 1;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    return pos;
}

void read_bool(const std::string& json, const std::string& key, bool& out) {
    const size_t pos = find_value(json, key);
    if (pos == std::string::npos) {
        return;
    }
    if (json.compare(pos, 4, "true") == 0) {
        out = true;
    } else if (json.compare(pos, 5, "false") == 0) {
        out = false;
    }
}

void read_int(const std::string& json, const std::string& key, int& out) {
    const size_t pos = find_value(json, key);
    if (pos == std::string::npos || pos >= json.size()) {
        return;
    }
    if (!(json[pos] == '-' || std::isdigit(static_cast<unsigned char>(json[pos])))) {
        return;
    }
    try {
        out = std::stoi(json.substr(pos));
    } catch (const std::exception&) {
    }
}

void read_string(const std::string& json, const std::string& key, std::string& out) {
    const size_t pos = find_value(json, key);
    if (pos == std::string::npos || pos >= json.size() || json[pos] != '"') {
        return;
    }
    std::string value;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            ++i;
            value.push_back(json[i]);
            continue;
        }
        if (json[i] == '"') {
            out = value;
            return;
        }
        value.push_back(json[i]);
    }
}

std::string escape_json(const std::string& text) {
    std::string out;
    for (char ch : text) {
        if (ch == '\\' || ch == '"') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    return out;
}

void write_bool(std::ostream& out, const char* key, bool value, bool comma = true) {
    out << "    \"" << key << "\": " << (value ? "true" : "false") << (comma ? ",\n" : "\n");
}

void write_int(std::ostream& out, const char* key, int value, bool comma = true) {
    out << "    \"" << key << "\": " << value << (comma ? ",\n" : "\n");
}

void write_string(std::ostream& out, const char* key, const std::string& value, bool comma = true) {
    out << "    \"" << key << "\": \"" << escape_json(value) << "\"" << (comma ? ",\n" : "\n");
}

}  // namespace

std::filesystem::path find_project_root(const std::filesystem::path& start) {
    std::filesystem::path dir = start;
    for (int i = 0; i < 6; ++i) {
        if (std::filesystem::exists(dir / "assets" / "mortar_distances.txt")) {
            return dir;
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return start;
}

void load_settings(const std::filesystem::path& file, Settings& settings) {
    const std::string json = read_file(file);
    if (json.empty()) {
        return;
    }
    read_bool(json, "general_settings_debug_mode_checkbox", settings.debug_mode);
    read_string(json, "general_settings_calculation_hotkey_entry", settings.hotkey_map);
    read_string(json, "elevation_hotkey_entry", settings.hotkey_elevation);
    read_string(json, "all_in_one_hotkey_entry", settings.hotkey_both);
    std::string title = wide_to_utf8(settings.window_title);
    read_string(json, "general_settings_title_entry", title);
    settings.window_title = utf8_to_wide(title);
    read_bool(json, "general_settings_dictor_checkbox", settings.voice_enabled);
    read_int(json, "dictor_settings_volume_slider", settings.voice_volume);
    read_int(json, "dictor_settings_rate_slider", settings.voice_rate);
    read_bool(json, "elevation_draw_points_checkbox", settings.elevation_draw_points);
    read_bool(json, "elevation_draw_processed_checkbox", settings.elevation_draw_processed);
    read_int(json, "elevation_fov_slider", settings.fov);
    read_bool(json, "minimap_detector_enabled_checkbox", settings.minimap_enabled);
    read_int(json, "minimap_detector_offset_slider", settings.minimap_offset);
    read_int(json, "minimap_detector_small_minimap_size_slider", settings.minimap_small);
    read_int(json, "minimap_detector_large_minimap_size_slider", settings.minimap_large);
    read_bool(json, "grid_detection_show_processed_image_checkbox", settings.grid_show_processed);
    read_bool(json, "grid_detection_draw_grid_lines_checkbox", settings.grid_draw_lines);
    read_int(json, "grid_detection_canny1_threshold_slider", settings.canny1);
    read_int(json, "grid_detection_canny2_threshold_slider", settings.canny2);
    int line_threshold = static_cast<int>(std::lround(settings.line_threshold * 100));
    int line_gap = static_cast<int>(std::lround(settings.line_gap * 100));
    read_int(json, "grid_detection_line_threshold_slider", line_threshold);
    read_int(json, "grid_detection_line_gap_slider", line_gap);
    settings.line_threshold = line_threshold / 100.0;
    settings.line_gap = line_gap / 100.0;
    read_int(json, "grid_detection_merge_threshold_slider", settings.line_merge);
    read_bool(json, "mark_detection_draw_checkbox", settings.mark_draw);
    read_bool(json, "mark_detection_show_processed_image_checkbox", settings.mark_show_processed);
    read_bool(json, "mark_detection_zoom_to_points_checkbox", settings.mark_zoom);
    read_bool(json, "mark_detection_yolo_checkbox", settings.mark_yolo);
    read_string(json, "mark_detection_color_combobox", settings.color);
    read_int(json, "mark_detection_min_radius_slider", settings.min_radius);
    read_int(json, "mark_detection_max_radius_slider", settings.max_radius);
    read_bool(json, "overlay_settings_enabled_checkbox", settings.overlay_enabled);
    read_bool(json, "overlay_settings_hide_from_capture_checkbox", settings.overlay_capture_hidden);
    read_bool(json, "overlay_settings_draw_borders_checkbox", settings.overlay_borders);
    read_bool(json, "overlay_settings_draw_map_marks_checkbox", settings.overlay_map_marks);
    read_bool(json, "overlay_settings_draw_elevation_marks_checkbox", settings.overlay_elevation_marks);
    read_bool(json, "overlay_settings_draw_minimap_box_checkbox", settings.overlay_minimap_box);
    read_int(json, "overlay_settings_scale_slider", settings.overlay_scale);
}

void save_settings(const std::filesystem::path& file, const Settings& settings) {
    std::ofstream out{file, std::ios::binary};
    out << "{\n";
    write_bool(out, "general_settings_debug_mode_checkbox", settings.debug_mode);
    write_string(out, "general_settings_calculation_hotkey_entry", settings.hotkey_map);
    write_string(out, "elevation_hotkey_entry", settings.hotkey_elevation);
    write_string(out, "all_in_one_hotkey_entry", settings.hotkey_both);
    write_string(out, "general_settings_title_entry", wide_to_utf8(settings.window_title));
    write_bool(out, "general_settings_dictor_checkbox", settings.voice_enabled);
    write_int(out, "dictor_settings_volume_slider", settings.voice_volume);
    write_int(out, "dictor_settings_rate_slider", settings.voice_rate);
    write_bool(out, "elevation_draw_points_checkbox", settings.elevation_draw_points);
    write_bool(out, "elevation_draw_processed_checkbox", settings.elevation_draw_processed);
    write_int(out, "elevation_fov_slider", settings.fov);
    write_bool(out, "minimap_detector_enabled_checkbox", settings.minimap_enabled);
    write_int(out, "minimap_detector_offset_slider", settings.minimap_offset);
    write_int(out, "minimap_detector_small_minimap_size_slider", settings.minimap_small);
    write_int(out, "minimap_detector_large_minimap_size_slider", settings.minimap_large);
    write_bool(out, "grid_detection_show_processed_image_checkbox", settings.grid_show_processed);
    write_bool(out, "grid_detection_draw_grid_lines_checkbox", settings.grid_draw_lines);
    write_int(out, "grid_detection_canny1_threshold_slider", settings.canny1);
    write_int(out, "grid_detection_canny2_threshold_slider", settings.canny2);
    write_int(out, "grid_detection_line_threshold_slider", static_cast<int>(std::lround(settings.line_threshold * 100)));
    write_int(out, "grid_detection_line_gap_slider", static_cast<int>(std::lround(settings.line_gap * 100)));
    write_int(out, "grid_detection_merge_threshold_slider", settings.line_merge);
    write_bool(out, "mark_detection_draw_checkbox", settings.mark_draw);
    write_bool(out, "mark_detection_show_processed_image_checkbox", settings.mark_show_processed);
    write_bool(out, "mark_detection_zoom_to_points_checkbox", settings.mark_zoom);
    write_bool(out, "mark_detection_yolo_checkbox", settings.mark_yolo);
    write_string(out, "mark_detection_color_combobox", settings.color);
    write_int(out, "mark_detection_min_radius_slider", settings.min_radius);
    write_int(out, "mark_detection_max_radius_slider", settings.max_radius);
    write_bool(out, "overlay_settings_enabled_checkbox", settings.overlay_enabled);
    write_bool(out, "overlay_settings_hide_from_capture_checkbox", settings.overlay_capture_hidden);
    write_bool(out, "overlay_settings_draw_borders_checkbox", settings.overlay_borders);
    write_bool(out, "overlay_settings_draw_map_marks_checkbox", settings.overlay_map_marks);
    write_bool(out, "overlay_settings_draw_elevation_marks_checkbox", settings.overlay_elevation_marks);
    write_bool(out, "overlay_settings_draw_minimap_box_checkbox", settings.overlay_minimap_box);
    write_int(out, "overlay_settings_scale_slider", settings.overlay_scale, false);
    out << "}\n";
}
