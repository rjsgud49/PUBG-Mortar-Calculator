#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "capture.hpp"
#include "onnx_models.hpp"
#include "overlay.hpp"
#include "settings.hpp"
#include "speech.hpp"
#include "utf.hpp"
#include "vision.hpp"

#include <windows.h>
#include <uxtheme.h>
#include <objbase.h>
#include <commctrl.h>
#include <commdlg.h>

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

enum {
    IDC_TAB = 100,
    IDC_DEBUG = 201,
    IDC_HOTKEY_MAP = 202,
    IDC_HOTKEY_ELEV = 203,
    IDC_HOTKEY_BOTH = 204,
    IDC_TITLE = 205,
    IDC_GRID_PROCESSED = 301,
    IDC_DRAW_GRID = 302,
    IDC_CANNY1 = 303,
    IDC_CANNY2 = 304,
    IDC_LINE_TH = 305,
    IDC_LINE_GAP = 306,
    IDC_MERGE = 307,
    IDC_DRAW_MARKS = 401,
    IDC_MARK_PROCESSED = 402,
    IDC_ZOOM = 403,
    IDC_YOLO = 404,
    IDC_COLOR = 405,
    IDC_MIN_R = 406,
    IDC_MAX_R = 407,
    IDC_LOAD_MAP = 408,
    IDC_YOLO_HINT = 409,
    IDC_ELEV_POINTS = 501,
    IDC_ELEV_PROCESSED = 502,
    IDC_FOV = 503,
    IDC_LOAD_ELEV = 504,
    IDC_MINI = 601,
    IDC_MINI_OFFSET = 602,
    IDC_MINI_SMALL = 603,
    IDC_MINI_LARGE = 604,
    IDC_MINI_HINT = 605,
    IDC_VOICE = 701,
    IDC_VOLUME = 702,
    IDC_RATE = 703,
    IDC_OV_ENABLED = 801,
    IDC_OV_BORDER = 802,
    IDC_OV_MAP = 803,
    IDC_OV_ELEV = 804,
    IDC_OV_MINI = 805,
    IDC_OV_SCALE = 806,
    IDC_OV_USE = 807,
    IDC_HK_MAP_STATE = 206,
    IDC_HK_ELEV_STATE = 207,
    IDC_HK_BOTH_STATE = 208,
    IDC_MAP_GAP = 901,
    IDC_MAP_MARK = 902,
    IDC_MAP_PLAYER = 903,
    IDC_MAP_DIST = 904,
    IDC_MAP_MINI = 905,
    IDC_ELEV_MARK = 911,
    IDC_ELEV_HEIGHT = 912,
    IDC_ELEV_DIST = 913,
    IDC_ELEV_MORTAR = 914,
    IDC_STATUS = 920,
    IDC_CAPTURE = 921,
    IDC_KEY_FEEDBACK = 922,
    IDC_OVERLAY_PLACE = 923,
    IDC_TAB_HINT = 924,
};

constexpr UINT_PTR kFollowTimer = 1;
constexpr COLORREF kBg = RGB(32, 32, 32);
constexpr COLORREF kPanel = RGB(46, 46, 49);
constexpr COLORREF kField = RGB(58, 60, 64);
constexpr COLORREF kText = RGB(240, 240, 240);
constexpr COLORREF kHint = RGB(196, 210, 224);
constexpr int kMapX = 8;
constexpr int kMapY = 8;
constexpr int kMapW = 430;
constexpr int kMapH = 188;
constexpr int kElevX = 446;
constexpr int kElevW = 68;
constexpr int kElevH = 330;
constexpr int kInfoY = 204;
constexpr int kPaneX = 524;
constexpr int kPaneW = 492;
constexpr int kClientW = 1024;
constexpr int kClientH = 424;

struct PreviewState {
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;
};

struct ParsedHotkey {
    UINT mods = 0;
    UINT vk = 0;
    bool ok = false;
};

struct App {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND map_view = nullptr;
    HWND elevation_view = nullptr;
    Overlay overlay;
    OnnxModels models;
    Settings settings;
    std::filesystem::path root;
    std::vector<int> mortar_distances;
    cv::Mat map_raw;
    cv::Mat elevation_raw;
    MapResult map;
    ElevationResult elevation;
    bool have_map = false;
    bool have_elevation = false;
    std::wstring status;
    bool hotkeys_ready = false;
    ParsedHotkey hotkey_map{};
    ParsedHotkey hotkey_elevation{};
    ParsedHotkey hotkey_both{};
    bool hotkey_registered[3] = {};
    bool hotkey_held[3] = {};
    bool watch_held[6] = {};
    ULONGLONG hotkey_at[3] = {};
    bool key_recognized = false;
    ULONGLONG notice_until = 0;
};

App g_app;
HFONT g_font = nullptr;
HFONT g_title_font = nullptr;
HBRUSH g_dark = nullptr;
HBRUSH g_panel = nullptr;
HBRUSH g_field = nullptr;
std::vector<HWND> g_tabs[7];
int g_tab_bottom[7] = {};
std::unordered_map<int, HWND> g_slider_labels;
std::unordered_map<int, int> g_slider_committed;
WNDPROC g_slider_proc = nullptr;

LRESULT CALLBACK slider_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

std::wstring exe_directory() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path().wstring();
}

void set_text(HWND hwnd, const std::wstring& text) {
    if (hwnd == nullptr) {
        return;
    }
    const int length = GetWindowTextLengthW(hwnd);
    std::wstring current(static_cast<size_t>(length), L'\0');
    if (length > 0) {
        GetWindowTextW(hwnd, current.data(), length + 1);
    }
    if (current == text) {
        return;
    }
    SetWindowTextW(hwnd, text.c_str());
}

void set_utf8(HWND hwnd, const std::string& text) {
    set_text(hwnd, utf8_to_wide(text));
}

void set_status(const std::string& text) {
    g_app.status = utf8_to_wide(text);
    set_text(GetDlgItem(g_app.window, IDC_STATUS), g_app.status);
}

std::wstring one_decimal(double value) {
    std::wostringstream stream;
    stream.setf(std::ios::fixed);
    stream.precision(1);
    stream << value;
    return stream.str();
}

std::wstring point_text(const std::optional<cv::Point>& point) {
    if (!point) {
        return utf8_to_wide("없음");
    }
    return L"(" + std::to_wstring(point->x) + L", " + std::to_wstring(point->y) + L")";
}

void update_map_labels() {
    const MapResult& map = g_app.map;
    set_text(GetDlgItem(g_app.window, IDC_MAP_GAP), map.grid_gap ? one_decimal(*map.grid_gap) + L"px" : utf8_to_wide("없음"));
    set_text(GetDlgItem(g_app.window, IDC_MAP_MARK), point_text(map.mark));
    set_text(GetDlgItem(g_app.window, IDC_MAP_PLAYER), point_text(map.player));
    set_text(GetDlgItem(g_app.window, IDC_MAP_DIST), map.distance ? one_decimal(*map.distance) + L"m" : utf8_to_wide("없음"));
    const char* kinds[] = {"없음", "소형", "대형"};
    const int kind = std::clamp(map.minimap_type, 0, 2);
    set_utf8(GetDlgItem(g_app.window, IDC_MAP_MINI), kinds[kind]);
}

void update_elevation_labels() {
    if (!g_app.have_elevation) {
        const std::wstring none = utf8_to_wide("없음");
        set_text(GetDlgItem(g_app.window, IDC_ELEV_MARK), none);
        set_text(GetDlgItem(g_app.window, IDC_ELEV_HEIGHT), none);
        set_text(GetDlgItem(g_app.window, IDC_ELEV_DIST), none);
        set_text(GetDlgItem(g_app.window, IDC_ELEV_MORTAR), none);
        return;
    }
    const ElevationResult& elevation = g_app.elevation;
    set_text(GetDlgItem(g_app.window, IDC_ELEV_MARK), point_text(elevation.mark));
    set_text(GetDlgItem(g_app.window, IDC_ELEV_HEIGHT), elevation.elevation ? one_decimal(*elevation.elevation) + L"m" : utf8_to_wide("없음"));
    set_text(GetDlgItem(g_app.window, IDC_ELEV_DIST), elevation.elevated_distance ? one_decimal(*elevation.elevated_distance) + L"m" : utf8_to_wide("없음"));
    std::wstring mortar = utf8_to_wide(elevation.mortar_label);
    const bool digits = !mortar.empty() && std::all_of(mortar.begin(), mortar.end(), [](wchar_t ch) { return iswdigit(ch) != 0; });
    if (digits) {
        mortar += L" m";
    }
    set_text(GetDlgItem(g_app.window, IDC_ELEV_MORTAR), mortar);
}

void set_preview(HWND hwnd, const cv::Mat& bgr) {
    auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (state == nullptr) {
        return;
    }
    if (state->bitmap != nullptr) {
        DeleteObject(state->bitmap);
        state->bitmap = nullptr;
        state->width = 0;
        state->height = 0;
    }
    if (!bgr.empty()) {
        cv::Mat bgra;
        cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bgra.cols;
        info.bmiHeader.biHeight = -bgra.rows;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        state->bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (bits != nullptr) {
            std::memcpy(bits, bgra.data, bgra.total() * bgra.elemSize());
            state->width = bgra.cols;
            state->height = bgra.rows;
        }
    }
    InvalidateRect(hwnd, nullptr, TRUE);
}

LRESULT CALLBACK preview_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(hwnd, &paint);
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (state != nullptr && state->bitmap != nullptr && state->width > 0 && state->height > 0) {
                const int cw = rect.right - rect.left;
                const int ch = rect.bottom - rect.top;
                const float scale = std::min(static_cast<float>(cw) / state->width, static_cast<float>(ch) / state->height);
                const int dw = std::max(1, static_cast<int>(state->width * scale));
                const int dh = std::max(1, static_cast<int>(state->height * scale));
                const int x = (cw - dw) / 2;
                const int y = (ch - dh) / 2;
                HDC memory = CreateCompatibleDC(dc);
                HGDIOBJ old = SelectObject(memory, state->bitmap);
                SetStretchBltMode(dc, HALFTONE);
                StretchBlt(dc, x, y, dw, dh, memory, 0, 0, state->width, state->height, SRCCOPY);
                SelectObject(memory, old);
                DeleteDC(memory);
            }
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_DESTROY: {
            auto* state = reinterpret_cast<PreviewState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (state != nullptr) {
                if (state->bitmap != nullptr) {
                    DeleteObject(state->bitmap);
                }
                delete state;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

HWND make_preview(int x, int y, int width, int height) {
    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        L"PubgMortarPreview",
        L"",
        WS_CHILD | WS_VISIBLE,
        x,
        y,
        width,
        height,
        g_app.window,
        nullptr,
        g_app.instance,
        nullptr
    );
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new PreviewState()));
    return hwnd;
}

HWND add_widget(int tab, const wchar_t* klass, const std::wstring& text, DWORD style, int x, int y, int width, int height, int id) {
    HWND hwnd = CreateWindowExW(
        0,
        klass,
        text.c_str(),
        WS_CHILD | style | (tab <= 0 ? WS_VISIBLE : 0),
        x,
        y,
        width,
        height,
        g_app.window,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        g_app.instance,
        nullptr
    );
    SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    if (lstrcmpW(klass, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        SetWindowTheme(hwnd, L"", L"");
    }
    if (lstrcmpW(klass, L"EDIT") == 0 || lstrcmpW(klass, L"COMBOBOX") == 0) {
        SetWindowTheme(hwnd, L"", L"");
    }
    if (tab >= 0) {
        g_tabs[tab].push_back(hwnd);
        const int used_height = lstrcmpW(klass, L"COMBOBOX") == 0 ? 26 : height;
        g_tab_bottom[tab] = std::max(g_tab_bottom[tab], y + used_height);
    }
    return hwnd;
}

HWND add_utf8(int tab, const wchar_t* klass, const std::string& text, DWORD style, int x, int y, int width, int height, int id) {
    return add_widget(tab, klass, utf8_to_wide(text), style, x, y, width, height, id);
}

void add_slider(int tab, int id, const std::string& name, int y, int min_value, int max_value) {
    add_utf8(tab, L"STATIC", name, SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, y, 156, 26, 0);
    HWND bar = add_widget(tab, TRACKBAR_CLASSW, L"", TBS_NOTICKS, kPaneX + 172, y, 220, 28, id);
    HWND value = add_widget(tab, L"STATIC", L"0", SS_LEFT | SS_CENTERIMAGE, kPaneX + 400, y, 56, 26, 0);
    SendMessageW(bar, TBM_SETRANGE, TRUE, MAKELPARAM(min_value, max_value));
    g_slider_labels[id] = value;
    if (g_slider_proc == nullptr) {
        g_slider_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(bar, GWLP_WNDPROC));
    }
    SetWindowLongPtrW(bar, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(slider_proc));
}

void set_tab_hint(int index) {
    const char* hints[] = {
        "지도가 보일 때 거리 키, 박격포로 핑을 조준했을 때 사거리 키를 누르세요.",
        "칸 선이 안 보이면 흐린 선을 낮추세요. 글자까지 선이 되면 진한 선을 높이세요.",
        "게임에서 찍은 핑 색과 같아야 합니다. 내 위치와 목표 핑이 둘 다 잡혀야 거리가 나옵니다.",
        "평면 거리가 먼저 있어야 합니다. 시야각은 게임 설정과 같게 맞추세요.",
        "M으로 연 큰 지도만 쓸 때는 미니맵 사용을 끄세요.",
        "계산이 끝나면 거리를 읽어 줍니다.",
        "게임 위에 숫자와 선을 띄울 때만 사용을 켜세요.",
    };
    if (index < 0 || index > 6) {
        index = 0;
    }
    set_utf8(GetDlgItem(g_app.window, IDC_TAB_HINT), hints[index]);
}

void place_tab_hint(int index) {
    if (index < 0 || index > 6) {
        index = 0;
    }
    HWND hint = GetDlgItem(g_app.window, IDC_TAB_HINT);
    if (hint == nullptr) {
        return;
    }
    SetWindowPos(hint, nullptr, kPaneX + 12, g_tab_bottom[index] + 8, kPaneW - 24, 40, SWP_NOZORDER | SWP_NOACTIVATE);
}

void show_tab(int index) {
    for (int tab = 0; tab < 7; ++tab) {
        for (HWND hwnd : g_tabs[tab]) {
            ShowWindow(hwnd, tab == index ? SW_SHOW : SW_HIDE);
        }
    }
    set_tab_hint(index);
    place_tab_hint(index);
    RedrawWindow(g_app.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
}

bool checked(int id) {
    return SendMessageW(GetDlgItem(g_app.window, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int slider_value(int id) {
    return static_cast<int>(SendMessageW(GetDlgItem(g_app.window, id), TBM_GETPOS, 0, 0));
}

std::wstring edit_text(int id) {
    HWND control = GetDlgItem(g_app.window, id);
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<size_t>(length + 1), L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    if (!text.empty() && text.back() == L'\0') {
        text.pop_back();
    }
    return text;
}

std::string lower_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

void pull_settings() {
    Settings& settings = g_app.settings;
    settings.debug_mode = checked(IDC_DEBUG);
    settings.hotkey_map = lower_copy(wide_to_utf8(edit_text(IDC_HOTKEY_MAP)));
    settings.hotkey_elevation = lower_copy(wide_to_utf8(edit_text(IDC_HOTKEY_ELEV)));
    settings.hotkey_both = lower_copy(wide_to_utf8(edit_text(IDC_HOTKEY_BOTH)));
    settings.window_title = edit_text(IDC_TITLE);
    settings.grid_show_processed = checked(IDC_GRID_PROCESSED);
    settings.grid_draw_lines = checked(IDC_DRAW_GRID);
    settings.canny1 = slider_value(IDC_CANNY1);
    settings.canny2 = slider_value(IDC_CANNY2);
    settings.line_threshold = slider_value(IDC_LINE_TH) / 100.0;
    settings.line_gap = slider_value(IDC_LINE_GAP) / 100.0;
    settings.line_merge = slider_value(IDC_MERGE);
    settings.mark_draw = checked(IDC_DRAW_MARKS);
    settings.mark_show_processed = checked(IDC_MARK_PROCESSED);
    settings.mark_zoom = checked(IDC_ZOOM);
    settings.mark_yolo = checked(IDC_YOLO) && g_app.models.has_mark();
    const int color = static_cast<int>(SendMessageW(GetDlgItem(g_app.window, IDC_COLOR), CB_GETCURSEL, 0, 0));
    const char* colors[] = {"orange", "yellow", "blue", "green"};
    if (color >= 0 && color < 4) {
        settings.color = colors[color];
    }
    settings.min_radius = slider_value(IDC_MIN_R);
    settings.max_radius = slider_value(IDC_MAX_R);
    settings.elevation_draw_points = checked(IDC_ELEV_POINTS);
    settings.elevation_draw_processed = checked(IDC_ELEV_PROCESSED);
    settings.fov = slider_value(IDC_FOV);
    settings.minimap_enabled = checked(IDC_MINI) && g_app.models.has_minimap();
    settings.minimap_offset = slider_value(IDC_MINI_OFFSET);
    settings.minimap_small = slider_value(IDC_MINI_SMALL);
    settings.minimap_large = slider_value(IDC_MINI_LARGE);
    settings.voice_enabled = checked(IDC_VOICE);
    settings.voice_volume = slider_value(IDC_VOLUME);
    settings.voice_rate = slider_value(IDC_RATE);
    settings.overlay_enabled = checked(IDC_OV_USE);
    settings.overlay_capture_hidden = checked(IDC_OV_ENABLED);
    settings.overlay_borders = checked(IDC_OV_BORDER);
    settings.overlay_map_marks = checked(IDC_OV_MAP);
    settings.overlay_elevation_marks = checked(IDC_OV_ELEV);
    settings.overlay_minimap_box = checked(IDC_OV_MINI);
    settings.overlay_scale = slider_value(IDC_OV_SCALE);
    configure_speech(settings.voice_volume, settings.voice_rate);
}

void set_check(int id, bool value) {
    SendMessageW(GetDlgItem(g_app.window, id), BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
}

void set_slider(int id, int value) {
    g_slider_committed[id] = value;
    SendMessageW(GetDlgItem(g_app.window, id), TBM_SETPOS, TRUE, value);
    auto found = g_slider_labels.find(id);
    if (found != g_slider_labels.end()) {
        set_text(found->second, std::to_wstring(value));
    }
}

bool slider_blocked() {
    return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

bool point_on_thumb(HWND hwnd, LPARAM lparam) {
    RECT thumb{};
    SendMessageW(hwnd, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
    InflateRect(&thumb, 4, 6);
    const POINT point{
        static_cast<int>(static_cast<short>(LOWORD(lparam))),
        static_cast<int>(static_cast<short>(HIWORD(lparam)))
    };
    return PtInRect(&thumb, point) != FALSE;
}

LRESULT CALLBACK slider_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (slider_blocked()) {
        switch (message) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
            case WM_LBUTTONUP:
            case WM_MOUSEWHEEL:
            case WM_KEYDOWN:
            case WM_KEYUP:
                return 0;
            case WM_MOUSEMOVE:
                if ((wparam & MK_LBUTTON) != 0) {
                    return 0;
                }
                break;
            default:
                break;
        }
    }
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
            if (!point_on_thumb(hwnd, lparam)) {
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
        case WM_KEYDOWN:
        case WM_KEYUP:
            return 0;
        default:
            break;
    }
    return CallWindowProcW(g_slider_proc, hwnd, message, wparam, lparam);
}

void apply_settings_to_ui() {
    const Settings& settings = g_app.settings;
    set_check(IDC_DEBUG, settings.debug_mode);
    set_text(GetDlgItem(g_app.window, IDC_HOTKEY_MAP), utf8_to_wide(settings.hotkey_map));
    set_text(GetDlgItem(g_app.window, IDC_HOTKEY_ELEV), utf8_to_wide(settings.hotkey_elevation));
    set_text(GetDlgItem(g_app.window, IDC_HOTKEY_BOTH), utf8_to_wide(settings.hotkey_both));
    set_text(GetDlgItem(g_app.window, IDC_TITLE), settings.window_title);
    set_check(IDC_GRID_PROCESSED, settings.grid_show_processed);
    set_check(IDC_DRAW_GRID, settings.grid_draw_lines);
    set_slider(IDC_CANNY1, settings.canny1);
    set_slider(IDC_CANNY2, settings.canny2);
    set_slider(IDC_LINE_TH, static_cast<int>(std::lround(settings.line_threshold * 100)));
    set_slider(IDC_LINE_GAP, static_cast<int>(std::lround(settings.line_gap * 100)));
    set_slider(IDC_MERGE, settings.line_merge);
    set_check(IDC_DRAW_MARKS, settings.mark_draw);
    set_check(IDC_MARK_PROCESSED, settings.mark_show_processed);
    set_check(IDC_ZOOM, settings.mark_zoom);
    set_check(IDC_YOLO, settings.mark_yolo && g_app.models.has_mark());
    EnableWindow(GetDlgItem(g_app.window, IDC_YOLO), g_app.models.has_mark() ? TRUE : FALSE);
    set_utf8(GetDlgItem(g_app.window, IDC_YOLO_HINT), g_app.models.has_mark() ? "" : "모델 없음");
    int color = 3;
    if (settings.color == "orange") {
        color = 0;
    } else if (settings.color == "yellow") {
        color = 1;
    } else if (settings.color == "blue") {
        color = 2;
    }
    SendMessageW(GetDlgItem(g_app.window, IDC_COLOR), CB_SETCURSEL, color, 0);
    set_slider(IDC_MIN_R, settings.min_radius);
    set_slider(IDC_MAX_R, settings.max_radius);
    set_check(IDC_ELEV_POINTS, settings.elevation_draw_points);
    set_check(IDC_ELEV_PROCESSED, settings.elevation_draw_processed);
    set_slider(IDC_FOV, settings.fov);
    set_check(IDC_MINI, settings.minimap_enabled && g_app.models.has_minimap());
    EnableWindow(GetDlgItem(g_app.window, IDC_MINI), g_app.models.has_minimap() ? TRUE : FALSE);
    set_utf8(GetDlgItem(g_app.window, IDC_MINI_HINT), g_app.models.has_minimap() ? "" : "모델 없음");
    set_slider(IDC_MINI_OFFSET, settings.minimap_offset);
    set_slider(IDC_MINI_SMALL, settings.minimap_small);
    set_slider(IDC_MINI_LARGE, settings.minimap_large);
    set_check(IDC_VOICE, settings.voice_enabled);
    set_slider(IDC_VOLUME, settings.voice_volume);
    set_slider(IDC_RATE, settings.voice_rate);
    set_check(IDC_OV_USE, settings.overlay_enabled);
    set_check(IDC_OV_ENABLED, settings.overlay_capture_hidden);
    set_check(IDC_OV_BORDER, settings.overlay_borders);
    set_check(IDC_OV_MAP, settings.overlay_map_marks);
    set_check(IDC_OV_ELEV, settings.overlay_elevation_marks);
    set_check(IDC_OV_MINI, settings.overlay_minimap_box);
    set_slider(IDC_OV_SCALE, settings.overlay_scale);
}

void update_slider_label(int id) {
    auto found = g_slider_labels.find(id);
    if (found != g_slider_labels.end()) {
        set_text(found->second, std::to_wstring(slider_value(id)));
    }
}

void save_current_settings() {
    save_settings(g_app.root / "settings.json", g_app.settings);
}

OverlayView current_view() {
    OverlayView view;
    view.enabled = g_app.settings.overlay_enabled;
    view.borders = g_app.settings.overlay_borders;
    view.map_marks = g_app.settings.overlay_map_marks;
    view.elevation_marks = g_app.settings.overlay_elevation_marks;
    view.minimap_box = g_app.settings.overlay_minimap_box;
    view.scale = g_app.settings.overlay_scale;
    return view;
}

void update_overlay_place(const OverlayAnchor& anchor);

void sync_overlay() {
    if (!g_app.overlay.set_hidden_from_capture(g_app.settings.overlay_capture_hidden)) {
        set_status("캡처 설정 실패");
    }
    g_app.overlay.set_view(current_view());
    g_app.overlay.show_results(g_app.map, g_app.elevation, g_app.have_elevation);
    update_overlay_place(g_app.overlay.follow(g_app.settings.window_title));
}

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    const std::tm local = *std::localtime(&now);
    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M-%S", &local);
    return buffer;
}

void store_capture(const cv::Mat& image, const wchar_t* cache_name, const char* suffix, bool combat) {
    write_image_file((g_app.root / "temp" / cache_name).wstring(), image);
    if (combat && g_app.settings.debug_mode) {
        const std::string name = timestamp() + suffix;
        write_image_file((g_app.root / "debug_files" / utf8_to_wide(name)).wstring(), image);
    }
}

void compute_elevation(bool combat, bool speak) {
    if (g_app.elevation_raw.empty()) {
        return;
    }
    store_capture(g_app.elevation_raw, L"elevation_preview.png", "_elevation.png", combat);
    g_app.elevation = analyze_elevation(g_app.elevation_raw, g_app.settings, g_app.map.distance, g_app.mortar_distances);
    g_app.have_elevation = true;
    set_preview(g_app.elevation_view, g_app.elevation.preview);
    update_elevation_labels();
    if (speak && g_app.settings.voice_enabled) {
        speak_text(utf8_to_wide(g_app.elevation.mortar_label));
    }
    sync_overlay();
    if (combat) {
        set_status("");
    }
}

void compute_map(bool combat, bool speak) {
    if (g_app.map_raw.empty()) {
        set_status("지도 없음");
        return;
    }
    set_status("");
    store_capture(g_app.map_raw, L"map_preview.png", "_map.png", combat);
    g_app.map = analyze_map(g_app.map_raw, g_app.settings, &g_app.models);
    g_app.have_map = true;
    set_preview(g_app.map_view, g_app.map.preview);
    update_map_labels();
    if (speak && g_app.settings.voice_enabled && g_app.map.distance) {
        speak_text(std::to_wstring(std::lround(*g_app.map.distance)));
    }
    if (!g_app.elevation_raw.empty()) {
        compute_elevation(false, false);
    } else {
        g_app.have_elevation = false;
        update_elevation_labels();
        sync_overlay();
    }
    if (g_app.map.distance) {
        set_status("");
    } else if (!g_app.map.player || !g_app.map.mark) {
        set_status("표식 없음");
    } else {
        set_status("격자 없음");
    }
}

cv::Mat capture_game() {
    HWND game = find_window_by_title(g_app.settings.window_title);
    if (game == nullptr) {
        set_status("게임 창 없음");
        return {};
    }
    const CapturedFrame frame = capture_window(game);
    if (frame.bgr.empty()) {
        set_status("캡처 실패");
        return {};
    }
    return frame.bgr;
}

void capture_and_map() {
    cv::Mat image = capture_game();
    if (image.empty()) {
        return;
    }
    g_app.map_raw = image;
    compute_map(true, true);
}

void capture_and_elevation() {
    cv::Mat image = capture_game();
    if (image.empty()) {
        return;
    }
    g_app.elevation_raw = image;
    compute_elevation(true, true);
}

void capture_both() {
    cv::Mat image = capture_game();
    if (image.empty()) {
        return;
    }
    g_app.map_raw = image;
    compute_map(true, false);
    image = capture_game();
    if (image.empty()) {
        return;
    }
    g_app.elevation_raw = image;
    compute_elevation(true, true);
}

std::wstring pick_image() {
    wchar_t file[MAX_PATH] = {};
    std::wstring filter = utf8_to_wide("이미지");
    filter.push_back(L'\0');
    filter += L"*.png;*.jpg;*.jpeg;*.bmp;*.webp";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    std::wstring title = utf8_to_wide("이미지 열기");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_app.window;
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = file;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = title.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&dialog)) {
        return {};
    }
    return file;
}

void load_map_file() {
    const std::wstring path = pick_image();
    if (path.empty()) {
        return;
    }
    cv::Mat image = read_image_file(path);
    if (image.empty()) {
        set_status("이미지 없음");
        return;
    }
    g_app.map_raw = image;
    compute_map(false, false);
}

void load_elevation_file() {
    const std::wstring path = pick_image();
    if (path.empty()) {
        return;
    }
    cv::Mat image = read_image_file(path);
    if (image.empty()) {
        set_status("이미지 없음");
        return;
    }
    g_app.elevation_raw = image;
    compute_elevation(false, false);
}

ParsedHotkey parse_hotkey(const std::string& text) {
    ParsedHotkey result;
    std::string token;
    std::string key;
    std::string lower = lower_copy(text);
    lower.push_back('+');
    UINT mods = MOD_NOREPEAT;
    for (char ch : lower) {
        if (ch != '+') {
            token.push_back(ch);
            continue;
        }
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
            token.erase(token.begin());
        }
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
            token.pop_back();
        }
        if (token == "alt") {
            mods |= MOD_ALT;
        } else if (token == "ctrl" || token == "control") {
            mods |= MOD_CONTROL;
        } else if (token == "shift") {
            mods |= MOD_SHIFT;
        } else if (token == "win" || token == "windows") {
            mods |= MOD_WIN;
        } else {
            key = token;
        }
        token.clear();
    }
    if (key.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(key[0]);
        if (ch >= 'a' && ch <= 'z') {
            result.vk = static_cast<UINT>(std::toupper(ch));
        } else if (ch >= '0' && ch <= '9') {
            result.vk = ch;
        }
    } else if (key.size() >= 2 && key[0] == 'f') {
        try {
            const int number = std::stoi(key.substr(1));
            if (number >= 1 && number <= 24) {
                result.vk = VK_F1 + static_cast<UINT>(number - 1);
            }
        } catch (const std::exception&) {
        }
    }
    result.mods = mods;
    result.ok = result.vk != 0;
    return result;
}

std::wstring hotkey_state_text(int index) {
    const ParsedHotkey* keys[] = {&g_app.hotkey_map, &g_app.hotkey_elevation, &g_app.hotkey_both};
    if (!keys[index]->ok) {
        return utf8_to_wide("형식 오류");
    }
    if (g_app.hotkey_registered[index]) {
        return utf8_to_wide("등록됨");
    }
    return utf8_to_wide("감시 중");
}

void refresh_hotkey_states() {
    set_text(GetDlgItem(g_app.window, IDC_HK_MAP_STATE), hotkey_state_text(0));
    set_text(GetDlgItem(g_app.window, IDC_HK_ELEV_STATE), hotkey_state_text(1));
    set_text(GetDlgItem(g_app.window, IDC_HK_BOTH_STATE), hotkey_state_text(2));
}

void show_key_feedback(const std::wstring& text, bool recognized) {
    g_app.key_recognized = recognized;
    set_text(GetDlgItem(g_app.window, IDC_KEY_FEEDBACK), text);
    InvalidateRect(GetDlgItem(g_app.window, IDC_KEY_FEEDBACK), nullptr, TRUE);
}

std::wstring clock_text() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[16] = {};
    swprintf(buffer, 16, L"%02d:%02d:%02d", time.wHour, time.wMinute, time.wSecond);
    return buffer;
}

void note_hotkey(int index) {
    const wchar_t* names[] = {L"거리", L"고도", L"일괄"};
    const std::string* labels[] = {
        &g_app.settings.hotkey_map,
        &g_app.settings.hotkey_elevation,
        &g_app.settings.hotkey_both,
    };
    const std::wstring text = utf8_to_wide("인식됨  ") + names[index] + L"  " + utf8_to_wide(*labels[index]) + L"  " + clock_text();
    show_key_feedback(text, true);
    g_app.overlay.set_notice(text);
    g_app.notice_until = GetTickCount64() + 2000;
}

void trigger_hotkey(int index) {
    const ULONGLONG now = GetTickCount64();
    if (now - g_app.hotkey_at[index] < 500) {
        return;
    }
    g_app.hotkey_at[index] = now;
    note_hotkey(index);
    if (index == 0) {
        capture_and_map();
    } else if (index == 1) {
        capture_and_elevation();
    } else {
        capture_both();
    }
    g_app.hotkey_at[index] = GetTickCount64();
}

bool combo_down(UINT mods, UINT vk) {
    if (vk == 0) {
        return false;
    }
    if ((mods & MOD_ALT) && (GetAsyncKeyState(VK_MENU) & 0x8000) == 0) {
        return false;
    }
    if ((mods & MOD_CONTROL) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0) {
        return false;
    }
    if ((mods & MOD_SHIFT) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0) {
        return false;
    }
    if ((mods & MOD_WIN) && (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0 && (GetAsyncKeyState(VK_RWIN) & 0x8000) == 0) {
        return false;
    }
    return (GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
}

bool same_hotkey(const ParsedHotkey& key, UINT mods, UINT vk) {
    return key.ok && key.mods == (mods | MOD_NOREPEAT) && key.vk == vk;
}

void poll_hotkeys() {
    const ParsedHotkey bound[] = {g_app.hotkey_map, g_app.hotkey_elevation, g_app.hotkey_both};
    for (int i = 0; i < 3; ++i) {
        const bool down = bound[i].ok && combo_down(bound[i].mods, bound[i].vk);
        if (down && !g_app.hotkey_held[i]) {
            trigger_hotkey(i);
        }
        g_app.hotkey_held[i] = down;
    }

    const UINT watch_vk[] = {VK_F1, VK_F2, VK_F3, '1', '2', '3'};
    const wchar_t* watch_name[] = {L"Alt+F1", L"Alt+F2", L"Alt+F3", L"Alt+1", L"Alt+2", L"Alt+3"};
    for (int i = 0; i < 6; ++i) {
        const bool down = combo_down(MOD_ALT, watch_vk[i]);
        if (down && !g_app.watch_held[i]) {
            const bool bound_key =
                same_hotkey(g_app.hotkey_map, MOD_ALT | MOD_NOREPEAT, watch_vk[i]) ||
                same_hotkey(g_app.hotkey_elevation, MOD_ALT | MOD_NOREPEAT, watch_vk[i]) ||
                same_hotkey(g_app.hotkey_both, MOD_ALT | MOD_NOREPEAT, watch_vk[i]);
            if (!bound_key) {
                show_key_feedback(std::wstring(watch_name[i]) + utf8_to_wide(" 눌림, 등록된 단축키 아님"), false);
                g_app.overlay.set_notice(std::wstring(watch_name[i]) + utf8_to_wide(" 은 등록된 단축키가 아님"));
                g_app.notice_until = GetTickCount64() + 2000;
            }
        }
        g_app.watch_held[i] = down;
    }

    if (g_app.notice_until != 0 && GetTickCount64() > g_app.notice_until) {
        g_app.notice_until = 0;
        g_app.overlay.set_notice(L"");
    }
}

void update_overlay_place(const OverlayAnchor& anchor) {
    std::wstring text;
    if (!g_app.settings.overlay_enabled) {
        text = utf8_to_wide("오버레이: 꺼짐");
    } else if (!anchor.shown) {
        text = utf8_to_wide("오버레이: 게임 창 없음");
    } else {
        text = utf8_to_wide("오버레이: 왼쪽 위  ") +
            std::to_wstring(anchor.x) + L"," + std::to_wstring(anchor.y) +
            L"  " + std::to_wstring(anchor.width) + L"x" + std::to_wstring(anchor.height);
    }
    set_text(GetDlgItem(g_app.window, IDC_OVERLAY_PLACE), text);
}

void bind_hotkeys() {
    UnregisterHotKey(g_app.window, 1);
    UnregisterHotKey(g_app.window, 2);
    UnregisterHotKey(g_app.window, 3);
    g_app.hotkey_map = parse_hotkey(g_app.settings.hotkey_map);
    g_app.hotkey_elevation = parse_hotkey(g_app.settings.hotkey_elevation);
    g_app.hotkey_both = parse_hotkey(g_app.settings.hotkey_both);
    const ParsedHotkey keys[] = {g_app.hotkey_map, g_app.hotkey_elevation, g_app.hotkey_both};
    g_app.hotkeys_ready = true;
    for (int i = 0; i < 3; ++i) {
        g_app.hotkey_registered[i] = keys[i].ok && RegisterHotKey(g_app.window, i + 1, keys[i].mods, keys[i].vk);
        if (!g_app.hotkey_registered[i]) {
            g_app.hotkeys_ready = false;
        }
    }
    refresh_hotkey_states();
    if (!g_app.hotkey_map.ok || !g_app.hotkey_elevation.ok || !g_app.hotkey_both.ok) {
        set_status("단축키 형식 오류");
    }
}

void create_controls() {
    HWND pane = CreateWindowExW(
        0,
        L"PubgMortarPane",
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        kPaneX,
        46,
        kPaneW,
        kClientH - 54,
        g_app.window,
        nullptr,
        g_app.instance,
        nullptr
    );
    g_app.map_view = make_preview(kMapX, kMapY, kMapW, kMapH);
    g_app.elevation_view = make_preview(kElevX, kMapY, kElevW, kElevH);

    auto title = [](const char* text, int x, int y, int width) {
        HWND hwnd = add_utf8(-1, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE, x, y, width, 22, 0);
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_title_font), TRUE);
    };
    title("지도 정보", 8, kInfoY, 200);
    add_utf8(-1, L"STATIC", "격자", SS_LEFT | SS_CENTERIMAGE, 8, kInfoY + 24, 88, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 100, kInfoY + 24, 120, 22, IDC_MAP_GAP);
    add_utf8(-1, L"STATIC", "플레이어", SS_LEFT | SS_CENTERIMAGE, 8, kInfoY + 46, 88, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 100, kInfoY + 46, 120, 22, IDC_MAP_PLAYER);
    add_utf8(-1, L"STATIC", "마커", SS_LEFT | SS_CENTERIMAGE, 8, kInfoY + 68, 88, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 100, kInfoY + 68, 120, 22, IDC_MAP_MARK);
    add_utf8(-1, L"STATIC", "거리", SS_LEFT | SS_CENTERIMAGE, 8, kInfoY + 90, 88, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 100, kInfoY + 90, 120, 22, IDC_MAP_DIST);
    add_utf8(-1, L"STATIC", "미니맵", SS_LEFT | SS_CENTERIMAGE, 8, kInfoY + 112, 88, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 100, kInfoY + 112, 120, 22, IDC_MAP_MINI);

    title("고도 정보", 230, kInfoY, 200);
    add_utf8(-1, L"STATIC", "마커", SS_LEFT | SS_CENTERIMAGE, 230, kInfoY + 24, 72, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 306, kInfoY + 24, 130, 22, IDC_ELEV_MARK);
    add_utf8(-1, L"STATIC", "고도", SS_LEFT | SS_CENTERIMAGE, 230, kInfoY + 46, 72, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 306, kInfoY + 46, 130, 22, IDC_ELEV_HEIGHT);
    add_utf8(-1, L"STATIC", "거리", SS_LEFT | SS_CENTERIMAGE, 230, kInfoY + 68, 72, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 306, kInfoY + 68, 130, 22, IDC_ELEV_DIST);
    add_utf8(-1, L"STATIC", "사거리", SS_LEFT | SS_CENTERIMAGE, 230, kInfoY + 90, 72, 22, 0);
    add_utf8(-1, L"STATIC", "없음", SS_LEFT | SS_CENTERIMAGE, 306, kInfoY + 90, 130, 22, IDC_ELEV_MORTAR);
    add_utf8(-1, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, 8, 348, 500, 22, IDC_STATUS);
    add_utf8(-1, L"STATIC", "단축키: 아직 안 눌림", SS_LEFT | SS_CENTERIMAGE, 8, 370, 500, 22, IDC_KEY_FEEDBACK);
    add_utf8(-1, L"STATIC", "오버레이: 확인 중", SS_LEFT | SS_CENTERIMAGE, 8, 392, 500, 22, IDC_OVERLAY_PLACE);

    HWND tab = add_widget(-1, WC_TABCONTROLW, L"", WS_CLIPSIBLINGS | TCS_TABS, kPaneX, 8, kPaneW, 36, IDC_TAB);
    const char* names[] = {"일반", "격자", "마커", "고도", "미니맵", "음성", "오버레이"};
    for (int i = 0; i < 7; ++i) {
        std::wstring label = utf8_to_wide(names[i]);
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = label.data();
        SendMessageW(tab, TCM_INSERTITEMW, i, reinterpret_cast<LPARAM>(&item));
    }

    add_utf8(0, L"BUTTON", "디버그", BS_AUTOCHECKBOX, kPaneX + 12, 48, 160, 24, IDC_DEBUG);
    add_utf8(0, L"STATIC", "거리 단축키", SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, 80, 120, 24, 0);
    add_widget(0, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kPaneX + 136, 78, 140, 26, IDC_HOTKEY_MAP);
    add_utf8(0, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, kPaneX + 284, 78, 120, 24, IDC_HK_MAP_STATE);
    add_utf8(0, L"STATIC", "고도 단축키", SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, 112, 120, 24, 0);
    add_widget(0, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kPaneX + 136, 110, 140, 26, IDC_HOTKEY_ELEV);
    add_utf8(0, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, kPaneX + 284, 110, 120, 24, IDC_HK_ELEV_STATE);
    add_utf8(0, L"STATIC", "일괄 단축키", SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, 144, 120, 24, 0);
    add_widget(0, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kPaneX + 136, 142, 140, 26, IDC_HOTKEY_BOTH);
    add_utf8(0, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, kPaneX + 284, 142, 120, 24, IDC_HK_BOTH_STATE);
    add_utf8(0, L"STATIC", "창 제목", SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, 176, 120, 24, 0);
    add_widget(0, L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kPaneX + 136, 174, 180, 26, IDC_TITLE);

    add_utf8(1, L"BUTTON", "처리 결과", BS_AUTOCHECKBOX, kPaneX + 12, 48, 150, 24, IDC_GRID_PROCESSED);
    add_utf8(1, L"BUTTON", "격자 표시", BS_AUTOCHECKBOX, kPaneX + 180, 48, 150, 24, IDC_DRAW_GRID);
    add_slider(1, IDC_CANNY1, "흐린 선", 84, 0, 100);
    add_slider(1, IDC_CANNY2, "진한 선", 118, 0, 100);
    add_slider(1, IDC_LINE_TH, "짧은 선 빼기", 152, 20, 100);
    add_slider(1, IDC_LINE_GAP, "끊긴 선 잇기", 186, 0, 100);
    add_slider(1, IDC_MERGE, "겹친 선 합치기", 220, 0, 50);

    add_utf8(2, L"BUTTON", "마커 표시", BS_AUTOCHECKBOX, kPaneX + 12, 48, 140, 24, IDC_DRAW_MARKS);
    add_utf8(2, L"BUTTON", "처리 결과", BS_AUTOCHECKBOX, kPaneX + 170, 48, 140, 24, IDC_MARK_PROCESSED);
    add_utf8(2, L"BUTTON", "지점 확대", BS_AUTOCHECKBOX, kPaneX + 12, 76, 160, 24, IDC_ZOOM);
    add_utf8(2, L"BUTTON", "YOLO", BS_AUTOCHECKBOX, kPaneX + 180, 76, 140, 24, IDC_YOLO);
    add_utf8(2, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, kPaneX + 330, 76, 120, 24, IDC_YOLO_HINT);
    add_utf8(2, L"STATIC", "색상", SS_LEFT | SS_CENTERIMAGE, kPaneX + 12, 108, 60, 24, 0);
    HWND colors = add_widget(2, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, kPaneX + 76, 106, 120, 160, IDC_COLOR);
    const char* color_labels[] = {"주황", "노랑", "파랑", "초록"};
    for (const char* label : color_labels) {
        std::wstring wide = utf8_to_wide(label);
        SendMessageW(colors, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide.c_str()));
    }
    add_slider(2, IDC_MIN_R, "최소 반지름", 140, 0, 50);
    add_slider(2, IDC_MAX_R, "최대 반지름", 174, 5, 50);
    add_utf8(2, L"BUTTON", "지도 열기", BS_PUSHBUTTON, kPaneX + 12, 214, 160, 28, IDC_LOAD_MAP);

    add_utf8(3, L"BUTTON", "지점 표시", BS_AUTOCHECKBOX, kPaneX + 12, 48, 150, 24, IDC_ELEV_POINTS);
    add_utf8(3, L"BUTTON", "처리 결과", BS_AUTOCHECKBOX, kPaneX + 180, 48, 140, 24, IDC_ELEV_PROCESSED);
    add_slider(3, IDC_FOV, "시야각", 84, 80, 103);
    add_utf8(3, L"BUTTON", "고도 열기", BS_PUSHBUTTON, kPaneX + 12, 128, 160, 28, IDC_LOAD_ELEV);

    add_utf8(4, L"BUTTON", "미니맵 사용", BS_AUTOCHECKBOX, kPaneX + 12, 48, 160, 24, IDC_MINI);
    add_utf8(4, L"STATIC", "", SS_LEFT | SS_CENTERIMAGE, kPaneX + 180, 48, 160, 24, IDC_MINI_HINT);
    add_slider(4, IDC_MINI_OFFSET, "오프셋", 84, 0, 1000);
    add_slider(4, IDC_MINI_SMALL, "소형 크기", 118, 100, 1000);
    add_slider(4, IDC_MINI_LARGE, "대형 크기", 152, 100, 1000);

    add_utf8(5, L"BUTTON", "음성", BS_AUTOCHECKBOX, kPaneX + 12, 48, 180, 24, IDC_VOICE);
    add_slider(5, IDC_VOLUME, "음량", 84, 0, 100);
    add_slider(5, IDC_RATE, "속도", 118, 50, 300);

    add_utf8(6, L"BUTTON", "사용", BS_AUTOCHECKBOX, kPaneX + 12, 48, 150, 24, IDC_OV_USE);
    add_utf8(6, L"BUTTON", "캡처에 숨김", BS_AUTOCHECKBOX, kPaneX + 180, 48, 160, 24, IDC_OV_ENABLED);
    add_utf8(6, L"BUTTON", "테두리", BS_AUTOCHECKBOX, kPaneX + 360, 48, 120, 24, IDC_OV_BORDER);
    add_utf8(6, L"BUTTON", "지도 마커", BS_AUTOCHECKBOX, kPaneX + 12, 78, 150, 24, IDC_OV_MAP);
    add_utf8(6, L"BUTTON", "고도 마커", BS_AUTOCHECKBOX, kPaneX + 180, 78, 150, 24, IDC_OV_ELEV);
    add_utf8(6, L"BUTTON", "미니맵 영역", BS_AUTOCHECKBOX, kPaneX + 12, 108, 180, 24, IDC_OV_MINI);
    add_slider(6, IDC_OV_SCALE, "배율", 146, 50, 250);
    add_utf8(-1, L"STATIC", "", SS_LEFT, kPaneX + 12, 260, kPaneW - 24, 40, IDC_TAB_HINT);
    set_tab_hint(0);
    place_tab_hint(0);
    SetWindowPos(pane, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetWindowPos(tab, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void refresh_from_control(int id) {
    pull_settings();
    save_current_settings();
    if (id == IDC_DEBUG || id == IDC_VOICE || id == IDC_VOLUME || id == IDC_RATE) {
        return;
    }
    if (id == IDC_HOTKEY_MAP || id == IDC_HOTKEY_ELEV || id == IDC_HOTKEY_BOTH) {
        bind_hotkeys();
        return;
    }
    if (id == IDC_TITLE || id >= IDC_OV_ENABLED) {
        sync_overlay();
        return;
    }
    if (id >= IDC_ELEV_POINTS && id < IDC_MINI) {
        compute_elevation(false, false);
        return;
    }
    compute_map(false, false);
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_NOTIFY: {
            const auto* header = reinterpret_cast<NMHDR*>(lparam);
            if (header->idFrom == IDC_TAB && header->code == TCN_SELCHANGE) {
                show_tab(static_cast<int>(SendMessageW(header->hwndFrom, TCM_GETCURSEL, 0, 0)));
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);
            if (code == BN_CLICKED) {
                if (id == IDC_LOAD_MAP) {
                    load_map_file();
                    return 0;
                }
                if (id == IDC_LOAD_ELEV) {
                    load_elevation_file();
                    return 0;
                }
                refresh_from_control(id);
            } else if (code == CBN_SELCHANGE || code == EN_KILLFOCUS) {
                refresh_from_control(id);
            }
            return 0;
        }
        case WM_HSCROLL: {
            auto bar = reinterpret_cast<HWND>(lparam);
            const int id = GetDlgCtrlID(bar);
            if (slider_blocked()) {
                const auto saved = g_slider_committed.find(id);
                if (saved != g_slider_committed.end()) {
                    set_slider(id, saved->second);
                }
                return 0;
            }
            update_slider_label(id);
            const int code = LOWORD(wparam);
            if (code == TB_ENDTRACK || code == TB_LINEUP || code == TB_LINEDOWN || code == TB_PAGEUP || code == TB_PAGEDOWN) {
                refresh_from_control(id);
            }
            return 0;
        }
        case WM_HOTKEY:
            if (wparam >= 1 && wparam <= 3) {
                trigger_hotkey(static_cast<int>(wparam) - 1);
            }
            return 0;
        case WM_TIMER:
            if (wparam == kFollowTimer) {
                poll_hotkeys();
                update_overlay_place(g_app.overlay.follow(g_app.settings.window_title));
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            auto control = reinterpret_cast<HWND>(lparam);
            auto hdc = reinterpret_cast<HDC>(wparam);
            const int id = GetDlgCtrlID(control);
            COLORREF text = kText;
            if (id == IDC_KEY_FEEDBACK) {
                text = g_app.key_recognized ? RGB(80, 220, 120) : RGB(255, 196, 80);
            } else if (id == IDC_TAB_HINT) {
                text = kHint;
            } else if (!IsWindowEnabled(control)) {
                text = RGB(150, 150, 150);
            }
            RECT rc{};
            GetWindowRect(control, &rc);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&rc), 2);
            const bool on_panel = rc.left >= kPaneX;
            SetTextColor(hdc, text);
            SetBkColor(hdc, on_panel ? kPanel : kBg);
            return reinterpret_cast<LRESULT>(on_panel ? g_panel : g_dark);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            auto hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kText);
            SetBkColor(hdc, kField);
            return reinterpret_cast<LRESULT>(g_field);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using SetDpiFn = BOOL(WINAPI*)(HANDLE);
    auto set_dpi = reinterpret_cast<SetDpiFn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (set_dpi == nullptr || !set_dpi(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)))) {
        SetProcessDPIAware();
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    g_app.instance = instance;
    g_app.root = find_project_root(exe_directory());
    const std::filesystem::path table = g_app.root / "assets" / "mortar_distances.txt";
    if (!std::filesystem::exists(table)) {
        MessageBoxW(nullptr, utf8_to_wide("mortar_distances.txt 를 찾지 못했습니다.").c_str(), L"PUBG", MB_ICONERROR);
        return 1;
    }
    g_app.mortar_distances = load_mortar_distances(table.wstring());
    load_settings(g_app.root / "settings.json", g_app.settings);
    g_app.models.load(
        std::filesystem::path(exe_directory()) / L"onnxruntime.dll",
        (g_app.root / "assets" / "mark_model.onnx").wstring(),
        (g_app.root / "assets" / "map_model.onnx").wstring()
    );
    bool settings_changed = false;
    if (!g_app.models.has_mark() && g_app.settings.mark_yolo) {
        g_app.settings.mark_yolo = false;
        settings_changed = true;
    }
    if (!g_app.models.has_minimap() && g_app.settings.minimap_enabled) {
        g_app.settings.minimap_enabled = false;
        settings_changed = true;
    }
    if (settings_changed) {
        save_current_settings();
    }

    if (!g_app.overlay.create(instance)) {
        MessageBoxW(nullptr, utf8_to_wide("오버레이 창을 만들지 못했습니다.").c_str(), L"PUBG", MB_ICONERROR);
        return 1;
    }

    g_font = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic"
    );
    g_title_font = CreateFontW(
        16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, HANGUL_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic"
    );
    g_dark = CreateSolidBrush(kBg);
    g_panel = CreateSolidBrush(kPanel);
    g_field = CreateSolidBrush(kField);

    WNDCLASSW preview_class{};
    preview_class.lpfnWndProc = preview_proc;
    preview_class.hInstance = instance;
    preview_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    preview_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    preview_class.lpszClassName = L"PubgMortarPreview";
    RegisterClassW(&preview_class);

    WNDCLASSW pane_class{};
    pane_class.lpfnWndProc = DefWindowProcW;
    pane_class.hInstance = instance;
    pane_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pane_class.hbrBackground = g_panel;
    pane_class.lpszClassName = L"PubgMortarPane";
    RegisterClassW(&pane_class);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.hbrBackground = g_dark;
    window_class.lpszClassName = L"PubgMortarPanel";
    window_class.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_SHARED));
    window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_SHARED));
    RegisterClassExW(&window_class);

    const DWORD window_style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT bounds{0, 0, kClientW, kClientH};
    AdjustWindowRectEx(&bounds, window_style, FALSE, 0);
    g_app.window = CreateWindowExW(
        0,
        L"PubgMortarPanel",
        utf8_to_wide("PUBG 박격포 계산기").c_str(),
        window_style,
        40,
        40,
        bounds.right - bounds.left,
        bounds.bottom - bounds.top,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (window_class.hIcon != nullptr) {
        SendMessageW(g_app.window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(window_class.hIcon));
    }
    if (window_class.hIconSm != nullptr) {
        SendMessageW(g_app.window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(window_class.hIconSm));
    }
    create_controls();
    apply_settings_to_ui();
    configure_speech(g_app.settings.voice_volume, g_app.settings.voice_rate);
    const bool panel_excluded = exclude_from_capture(g_app.window);
    const bool overlay_capture_ok = g_app.overlay.set_hidden_from_capture(g_app.settings.overlay_capture_hidden);
    if (!panel_excluded || !overlay_capture_ok) {
        set_status("캡처 설정 실패");
    }
    ShowWindow(g_app.window, SW_SHOW);
    UpdateWindow(g_app.window);
    bind_hotkeys();

    g_app.map_raw = read_image_file((g_app.root / "temp" / "map_preview.png").wstring());
    g_app.elevation_raw = read_image_file((g_app.root / "temp" / "elevation_preview.png").wstring());
    if (!g_app.map_raw.empty()) {
        compute_map(false, false);
    } else if (!g_app.elevation_raw.empty()) {
        compute_elevation(false, false);
    }
    if (!g_app.hotkey_map.ok || !g_app.hotkey_elevation.ok || !g_app.hotkey_both.ok) {
        set_status("단축키 형식 오류");
    }
    {
        std::ofstream log{std::filesystem::path(exe_directory()) / L"startup.txt"};
        log << "overlay_excluded=" << (g_app.overlay.excluded_from_capture() ? 1 : 0) << "\n";
        log << "panel_excluded=" << (panel_excluded ? 1 : 0) << "\n";
        log << "mortar_rows=" << g_app.mortar_distances.size() << "\n";
        log << "mark_model=" << (g_app.models.has_mark() ? 1 : 0) << "\n";
        log << "minimap_model=" << (g_app.models.has_minimap() ? 1 : 0) << "\n";
        log << "model_error=" << g_app.models.last_error() << "\n";
        log << "hotkeys=" << (g_app.hotkeys_ready ? 1 : 0) << "\n";
    }
    SetTimer(g_app.window, kFollowTimer, 40, nullptr);
    sync_overlay();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnregisterHotKey(g_app.window, 1);
    UnregisterHotKey(g_app.window, 2);
    UnregisterHotKey(g_app.window, 3);
    CoUninitialize();
    return 0;
}
