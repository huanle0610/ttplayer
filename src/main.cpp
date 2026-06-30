#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>

#include "playlist_model.h"
#include "equalizer_model.h"
#include "lyric_mode.h"
#include "desktop_lyric_model.h"
#include "skin_definition.h"
#include "skin_package.h"
#include "skin_selection.h"
#include "window_layout.h"
#include "resource.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace {

constexpr int kMainWidth = 313;
constexpr int kMainHeight = 126;
constexpr int kPanelWidth = 313;
constexpr int kLyricHeight = 200;
constexpr int kPlaylistHeight = 200;
constexpr int kEqualizerHeight = 126;
constexpr int kTitleHeight = 25;
constexpr int kSnapDistance = 12;
constexpr UINT_PTR kPlaybackTimerId = 1;
constexpr UINT kPlaybackTimerMs = 200;
constexpr UINT kTrayCallbackMessage = WM_APP + 1;
constexpr UINT_PTR kTrayIconId = 1;
constexpr UINT kTrayMenuToggle = 7001;
constexpr UINT kTrayMenuPlayPause = 7002;
constexpr UINT kTrayMenuExit = 7003;
constexpr UINT kTrayMenuDesktopLyrics = 7004;
constexpr UINT kTrayMenuLockDesktopLyrics = 7005;
constexpr int kDesktopLyricWidth = 550;
constexpr int kDesktopLyricHeight = 89;

HINSTANCE g_instance = nullptr;

struct SkinBitmaps {
    HBITMAP player{};
    HBITMAP lyrics{};
    HBITMAP playlist{};
    HBITMAP equalizer{};
    HBITMAP desktop_lyric_toolbar{};
    std::unordered_map<std::string, HBITMAP> desktop_lyric_controls;
    std::unordered_map<std::string, HBITMAP> player_controls;
    std::unordered_map<std::string, HBITMAP> panel_controls;
    std::unordered_map<std::string, HICON> player_icons;
};

SkinBitmaps g_skin;
std::optional<SkinDefinition> g_skin_definition;
std::vector<PlaylistTrack> g_playlist;
std::wstring g_playlist_name = L"100";
PlaylistLibrary g_playlist_library;
std::optional<PlaylistSessionRestore> g_loaded_playlist_session;

struct AudioMetadata {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring format;
    int track_number = 0;
    int rating = 0;
};

struct AudioState {
    int current_index = 0;
    bool opened = false;
    bool playing = false;
    int volume = 70;
};

AudioState g_audio;
std::wstring g_last_audio_error;
int g_playlist_scroll = 0;
int g_playlist_divider_x = 68;
bool g_playlist_divider_dragging = false;
bool g_playlist_scrollbar_dragging = false;
int g_playlist_scrollbar_drag_offset = 0;
bool g_playlist_list_dragging = false;
int g_playlist_list_drag_index = -1;
int g_playlist_list_drag_target_index = -1;
int g_playlist_list_drag_mouse_x = 0;
int g_playlist_list_drag_mouse_y = 0;
enum class PlaylistPlayMode { Single, SingleLoop, Sequence, Loop, Random };
PlaylistPlayMode g_playlist_play_mode = PlaylistPlayMode::Loop;
bool g_playlist_follow_cursor = false;
bool g_playlist_auto_switch = false;
EqualizerState g_equalizer;
enum class EqDragTarget { None, Preamp, Band, Balance, Surround };
EqDragTarget g_eq_drag_target = EqDragTarget::None;
int g_eq_drag_band = -1;
std::unordered_map<std::wstring, AudioMetadata> g_metadata_cache;
struct LyricLine {
    int time_ms{};
    std::wstring text;
};
std::unordered_map<std::wstring, std::vector<LyricLine>> g_lyric_cache;
constexpr std::size_t kVisualBarCount = 58;
std::array<int, kVisualBarCount> g_visual_bars{};
std::array<int, kVisualBarCount> g_visual_peaks{};

struct AppConfig {
    std::optional<RECT> player_wnd;
    std::optional<RECT> lyric_wnd;
    std::optional<RECT> equalizer_wnd;
    std::optional<RECT> playlist_wnd;
    std::optional<RECT> desklrc_wnd;
    std::optional<bool> lyric_visible;
    std::optional<bool> equalizer_visible;
    std::optional<bool> playlist_visible;
    std::optional<bool> desktop_lyric_visible;
    std::optional<int> play_mode;
    std::optional<int> volume;
    std::optional<bool> play_follow_cursor;
    std::optional<bool> auto_switch_list;
    std::optional<int> desklrc_bkgnd_alpha;
    std::optional<std::uint32_t> desklrc_bkgnd_color;
    std::optional<std::uint32_t> desklrc_shadow_color;
    std::optional<std::uint32_t> desklrc_base_color;
    std::optional<std::uint32_t> desklrc_progress_color;
    std::optional<int> desklrc_font_height;
};

AppConfig g_config;

struct Rgb {
    BYTE r;
    BYTE g;
    BYTE b;
};

COLORREF color(Rgb c) { return RGB(c.r, c.g, c.b); }

COLORREF color_from_rgb24(std::uint32_t rgb) {
    return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

COLORREF desktop_lyric_transparent_key() {
    return RGB(1, 2, 3);
}

COLORREF skin_transparent_key() {
    return RGB(255, 0, 255);
}

std::wstring_view panel_class_name() { return L"TTPlayerClassicPanelWindow"; }
std::wstring_view main_class_name() { return L"TTPlayerClassicMainWindow"; }
std::wstring_view desktop_lyric_class_name() { return L"TTPlayerClassicDesktopLyricWindow"; }
std::wstring_view desktop_lyric_toolbar_class_name() { return L"TTPlayerClassicDesktopLyricToolbar"; }

enum class PanelKind {
    Lyrics,
    Playlist,
    Equalizer,
};

struct PanelWindow {
    PanelKind kind{};
    HWND hwnd{};
    HWND attached_to{};
    RECT last_rect{};
    std::wstring title;
};

struct AppState {
    HWND main_window{};
    std::vector<PanelWindow> panels;
    RECT last_main_rect{};
    bool arranging = false;
    bool interactive_move = false;
    std::string hovered_player_control;
    HWND desktop_lyric_window{};
    HWND desktop_lyric_toolbar{};
    bool desktop_lyric_visible = false;
    bool desktop_lyric_locked = false;
    bool desktop_lyric_hovered = false;
    bool desktop_lyric_tracking = false;
    bool desktop_toolbar_tracking = false;
    bool desktop_lyric_arranging = false;
    DesktopLyricToolbarAction desktop_toolbar_pressed = DesktopLyricToolbarAction::None;
};

AppState g_app;

std::wstring panel_title(PanelKind kind) {
    switch (kind) {
    case PanelKind::Lyrics:
        return L"歌词秀";
    case PanelKind::Playlist:
        return L"播放列表";
    case PanelKind::Equalizer:
        return L"均衡器";
    }
    return L"";
}

Rgb panel_accent(PanelKind kind) {
    switch (kind) {
    case PanelKind::Lyrics:
        return {0, 220, 210};
    case PanelKind::Playlist:
        return {0, 180, 150};
    case PanelKind::Equalizer:
        return {140, 205, 255};
    }
    return {0, 220, 210};
}

PanelWindow* find_panel(HWND hwnd) {
    for (auto& panel : g_app.panels) {
        if (panel.hwnd == hwnd) {
            return &panel;
        }
    }
    return nullptr;
}

PanelWindow* find_panel(PanelKind kind) {
    for (auto& panel : g_app.panels) {
        if (panel.kind == kind) {
            return &panel;
        }
    }
    return nullptr;
}

int playlist_visible_rows(const RECT& client) {
    return playlist_visible_row_count(static_cast<int>(client.bottom));
}

void clamp_playlist_scroll(int visible_rows) {
    const int max_scroll = std::max(0, static_cast<int>(g_playlist.size()) - std::max(1, visible_rows));
    g_playlist_scroll = std::clamp(g_playlist_scroll, 0, max_scroll);
}

void clamp_playlist_scroll_for_window(HWND hwnd) {
    RECT client{};
    GetClientRect(hwnd, &client);
    clamp_playlist_scroll(playlist_visible_rows(client));
}

void save_app_config();
void commit_current_playlist_to_library();
void refresh_desktop_lyric_layered();

int playlist_divider_min_x() {
    return 50;
}

int playlist_divider_max_x(const RECT& client) {
    return std::max(playlist_divider_min_x(), static_cast<int>(client.right) - 116);
}

void clamp_playlist_divider(const RECT& client) {
    g_playlist_divider_x = std::clamp(g_playlist_divider_x, playlist_divider_min_x(), playlist_divider_max_x(client));
}

bool hit_test_playlist_divider(int x, int y, const RECT& client) {
    const int top = 54;
    const int bottom = std::max(top, static_cast<int>(client.bottom) - 12);
    return y >= top && y <= bottom && std::abs(x - g_playlist_divider_x) <= 4;
}


PlaylistScrollbarGeometry playlist_scrollbar_for_client(const RECT& client) {
    return playlist_scrollbar_geometry(
        static_cast<int>(client.right - client.left),
        static_cast<int>(client.bottom - client.top),
        static_cast<int>(g_playlist.size()),
        playlist_visible_rows(client),
        g_playlist_scroll);
}

bool set_playlist_scroll(HWND hwnd, int next_scroll) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int visible_rows = playlist_visible_rows(client);
    const int max_scroll = std::max(0, static_cast<int>(g_playlist.size()) - std::max(1, visible_rows));
    const int clamped = std::clamp(next_scroll, 0, max_scroll);
    if (g_playlist_scroll == clamped) {
        return false;
    }
    g_playlist_scroll = clamped;
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}


std::optional<int> playlist_list_index_at(int x, int y, const RECT& client) {
    const auto feedback = playlist_list_drag_feedback_at(0, x, y, static_cast<int>(client.bottom), g_playlist_divider_x, g_playlist_library.list_count());
    if (!feedback) {
        return std::nullopt;
    }
    return feedback->target_index;
}

bool begin_playlist_list_drag(HWND hwnd, int x, int y) {
    RECT client{};
    GetClientRect(hwnd, &client);
    clamp_playlist_divider(client);
    const auto index = playlist_list_index_at(x, y, client);
    if (!index) {
        return false;
    }
    g_playlist_list_dragging = true;
    g_playlist_list_drag_index = *index;
    g_playlist_list_drag_target_index = *index;
    g_playlist_list_drag_mouse_x = x;
    g_playlist_list_drag_mouse_y = y;
    SetCapture(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
    return true;
}

void update_playlist_list_drag(HWND hwnd, int x, int y) {
    if (!g_playlist_list_dragging) {
        return;
    }
    g_playlist_list_drag_mouse_x = x;
    g_playlist_list_drag_mouse_y = y;
    RECT client{};
    GetClientRect(hwnd, &client);
    clamp_playlist_divider(client);
    const auto feedback = playlist_list_drag_feedback_at(g_playlist_list_drag_index, x, y, static_cast<int>(client.bottom), g_playlist_divider_x, g_playlist_library.list_count());
    const int next_target = feedback ? feedback->target_index : -1;
    if (g_playlist_list_drag_target_index != next_target) {
        g_playlist_list_drag_target_index = next_target;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

void finish_playlist_list_drag(HWND hwnd, bool save) {
    if (!g_playlist_list_dragging) {
        return;
    }
    const int from = g_playlist_list_drag_index;
    const int to = g_playlist_list_drag_target_index;
    g_playlist_list_dragging = false;
    g_playlist_list_drag_index = -1;
    g_playlist_list_drag_target_index = -1;
    g_playlist_list_drag_mouse_x = 0;
    g_playlist_list_drag_mouse_y = 0;
    ReleaseCapture();
    if (save && to >= 0 && to != from) {
        commit_current_playlist_to_library();
        if (g_playlist_library.move_list(static_cast<std::size_t>(from), static_cast<std::size_t>(to))) {
            g_playlist = g_playlist_library.active_tracks();
            g_playlist_name = g_playlist_library.active_name();
            g_audio.current_index = g_playlist_library.active_track_index();
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
    if (save) {
        save_app_config();
    }
}

bool handle_playlist_scrollbar_press(HWND hwnd, int x, int y) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int visible_rows = playlist_visible_rows(client);
    const auto geometry = playlist_scrollbar_for_client(client);
    const auto part = playlist_scrollbar_part_at(geometry, x, y);
    if (part == PlaylistScrollbarPart::None) {
        return false;
    }
    if (part == PlaylistScrollbarPart::Thumb) {
        g_playlist_scrollbar_dragging = true;
        g_playlist_scrollbar_drag_offset = y - geometry.thumb.top;
        SetCapture(hwnd);
        return true;
    }
    set_playlist_scroll(hwnd, playlist_scroll_after_scrollbar_click(part, g_playlist_scroll, static_cast<int>(g_playlist.size()), visible_rows));
    return true;
}

void update_playlist_scrollbar_drag(HWND hwnd, int y) {
    RECT client{};
    GetClientRect(hwnd, &client);
    const int visible_rows = playlist_visible_rows(client);
    const auto geometry = playlist_scrollbar_for_client(client);
    if (!geometry.visible) {
        return;
    }
    const int thumb_top = y - g_playlist_scrollbar_drag_offset;
    set_playlist_scroll(hwnd, playlist_scroll_from_thumb_top(geometry, thumb_top, static_cast<int>(g_playlist.size()), visible_rows));
}
void scroll_playlist_to_current() {
    auto* panel = find_panel(PanelKind::Playlist);
    if (!panel || g_playlist.empty()) {
        g_playlist_scroll = 0;
        return;
    }
    RECT client{};
    GetClientRect(panel->hwnd, &client);
    const int visible_rows = playlist_visible_rows(client);
    if (visible_rows <= 0) {
        return;
    }
    if (g_audio.current_index < g_playlist_scroll) {
        g_playlist_scroll = g_audio.current_index;
    } else if (g_audio.current_index >= g_playlist_scroll + visible_rows) {
        g_playlist_scroll = g_audio.current_index - visible_rows + 1;
    }
    clamp_playlist_scroll(visible_rows);
}

void invalidate_panel_kind(PanelKind kind) {
    if (auto* panel = find_panel(kind)) {
        InvalidateRect(panel->hwnd, nullptr, FALSE);
    }
}

void invalidate_playback_views(HWND hwnd) {
    InvalidateRect(hwnd, nullptr, FALSE);
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_panel_kind(PanelKind::Lyrics);
}

HICON load_app_icon(int cx = 0, int cy = 0) {
    const int width = cx > 0 ? cx : GetSystemMetrics(SM_CXSMICON);
    const int height = cy > 0 ? cy : GetSystemMetrics(SM_CYSMICON);
    auto* icon = static_cast<HICON>(LoadImageW(g_instance, MAKEINTRESOURCEW(IDI_TTPLAYER), IMAGE_ICON, width, height, LR_DEFAULTCOLOR));
    return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
}

void hide_desktop_lyrics_for_lyric_window() {
    if (!g_app.desktop_lyric_visible) {
        return;
    }
    g_app.desktop_lyric_visible = false;
    g_app.desktop_lyric_hovered = false;
    if (g_app.desktop_lyric_toolbar) {
        ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
    }
    if (g_app.desktop_lyric_window) {
        ShowWindow(g_app.desktop_lyric_window, SW_HIDE);
    }
}
void toggle_panel(PanelKind kind) {
    if (auto* panel = find_panel(kind)) {
        const BOOL visible = IsWindowVisible(panel->hwnd);
        if (visible) {
            ShowWindow(panel->hwnd, SW_HIDE);
        } else {
            RECT rect{};
            GetWindowRect(panel->hwnd, &rect);
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int dx = 0;
            int dy = 0;
            if (rect.right > work.right) {
                dx = work.right - rect.right;
            }
            if (rect.left + dx < work.left) {
                dx = work.left - rect.left;
            }
            if (rect.bottom > work.bottom) {
                dy = work.bottom - rect.bottom;
            }
            if (rect.top + dy < work.top) {
                dy = work.top - rect.top;
            }
            if (dx != 0 || dy != 0) {
                OffsetRect(&rect, dx, dy);
            }
            if (kind == PanelKind::Lyrics) {
                hide_desktop_lyrics_for_lyric_window();
            }
            SetWindowPos(panel->hwnd, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        save_app_config();
    }
}

void set_all_windows_visible(HWND hwnd, bool visible) {
    ShowWindow(hwnd, visible ? SW_SHOWNORMAL : SW_HIDE);
    for (const auto& panel : g_app.panels) {
        const bool show_panel = visible && !(panel.kind == PanelKind::Lyrics && g_app.desktop_lyric_visible);
        ShowWindow(panel.hwnd, show_panel ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    if (g_app.desktop_lyric_window) {
        ShowWindow(g_app.desktop_lyric_window, visible && g_app.desktop_lyric_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (visible && g_app.desktop_lyric_visible) {
            refresh_desktop_lyric_layered();
        }
    }
    if (g_app.desktop_lyric_toolbar) {
        ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
    }
    if (visible) {
        SetForegroundWindow(hwnd);
    }
}

void minimize_to_tray(HWND hwnd) {
    set_all_windows_visible(hwnd, false);
}

void restore_from_tray(HWND hwnd) {
    set_all_windows_visible(hwnd, true);
}

void add_tray_icon(HWND hwnd) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = kTrayIconId;
    data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data.uCallbackMessage = kTrayCallbackMessage;
    data.hIcon = load_app_icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wcscpy_s(data.szTip, L"千千静听");
    Shell_NotifyIconW(NIM_ADD, &data);
    data.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &data);
}

void remove_tray_icon(HWND hwnd) {
    NOTIFYICONDATAW data{};
    data.cbSize = sizeof(data);
    data.hWnd = hwnd;
    data.uID = kTrayIconId;
    Shell_NotifyIconW(NIM_DELETE, &data);
}

std::filesystem::path app_config_dir() {
    PWSTR raw_path = nullptr;
    std::filesystem::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &raw_path)) && raw_path) {
        dir = std::filesystem::path(raw_path) / L"TTPlayerClassic";
        CoTaskMemFree(raw_path);
    } else {
        dir = std::filesystem::temp_directory_path() / L"TTPlayerClassic";
    }
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    return dir;
}

std::filesystem::path app_xml_config_path() {
    return app_config_dir() / L"TTPlayer.xml";
}

std::string read_app_xml() {
    std::ifstream input(app_xml_config_path(), std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::optional<std::string> xml_attr(std::string_view xml, std::string_view tag, std::string_view name) {
    const std::string needle = "<" + std::string(tag);
    const auto tag_start = xml.find(needle);
    if (tag_start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto tag_end = xml.find('>', tag_start);
    if (tag_end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto tag_text = xml.substr(tag_start, tag_end - tag_start + 1);
    const std::string attr_needle = std::string(name) + "=\"";
    const auto attr_start = tag_text.find(attr_needle);
    if (attr_start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_start = attr_start + attr_needle.size();
    const auto value_end = tag_text.find('"', value_start);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(tag_text.substr(value_start, value_end - value_start));
}

std::optional<std::string> xml_block(std::string_view xml, std::string_view tag) {
    const std::string start_needle = "<" + std::string(tag);
    const std::string end_needle = "</" + std::string(tag) + ">";
    const auto start = xml.find(start_needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto end = xml.find(end_needle, start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(xml.substr(start, end + end_needle.size() - start));
}

void replace_xml_block(std::string& xml, std::string_view tag, const std::string& block) {
    if (xml.empty()) {
        xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n<ttplayer>\n</ttplayer>\n";
    }
    const std::string start_needle = "<" + std::string(tag);
    const std::string end_needle = "</" + std::string(tag) + ">";
    const auto start = xml.find(start_needle);
    if (start != std::string::npos) {
        const auto end = xml.find(end_needle, start);
        if (end != std::string::npos) {
            auto erase_end = end + end_needle.size();
            if (erase_end < xml.size() && xml[erase_end] == '\r') {
                ++erase_end;
            }
            if (erase_end < xml.size() && xml[erase_end] == '\n') {
                ++erase_end;
            }
            xml.replace(start, erase_end - start, block);
            return;
        }
    }
    const auto root_end = xml.find("</ttplayer>");
    if (root_end == std::string::npos) {
        xml += "\n<ttplayer>\n" + block + "</ttplayer>\n";
    } else {
        xml.insert(root_end, block);
    }
}


std::optional<RECT> parse_xml_rect(const std::string& value) {
    std::istringstream rect_stream(value);
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    char comma1 = 0;
    char comma2 = 0;
    char comma3 = 0;
    rect_stream >> left >> comma1 >> top >> comma2 >> right >> comma3 >> bottom;
    if (!rect_stream || comma1 != ',' || comma2 != ',' || comma3 != ',' ||
        right - left < desktop_lyric_min_width() || bottom - top < desktop_lyric_min_height()) {
        return std::nullopt;
    }
    return RECT{left, top, right, bottom};
}

std::optional<int> parse_xml_int_attr(std::string_view xml, std::string_view tag, std::string_view name) {
    const auto value = xml_attr(xml, tag, name);
    if (!value) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value->c_str(), &end, 10);
    if (!end || *end != '\0') {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

std::optional<bool> parse_xml_bool_attr(std::string_view xml, std::string_view tag, std::string_view name) {
    const auto value = parse_xml_int_attr(xml, tag, name);
    if (!value) {
        return std::nullopt;
    }
    return *value != 0;
}

std::optional<std::uint32_t> parse_xml_color_attr(std::string_view xml, std::string_view tag, std::string_view name) {
    const auto value = xml_attr(xml, tag, name);
    if (!value || value->size() != 7 || value->front() != '#') {
        return std::nullopt;
    }
    char* end = nullptr;
    const auto parsed = std::strtoul(value->c_str() + 1, &end, 16);
    if (!end || *end != '\0' || parsed > 0xffffffUL) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

std::optional<int> parse_logfont_height_attr(std::string_view xml, std::string_view tag, std::string_view name) {
    const auto value = xml_attr(xml, tag, name);
    if (!value) {
        return std::nullopt;
    }
    std::istringstream input(*value);
    int height = 0;
    input >> height;
    if (!input || height == 0) {
        return std::nullopt;
    }
    return std::abs(height);
}

std::string rect_to_xml_value(const RECT& rect) {
    return std::to_string(rect.left) + "," + std::to_string(rect.top) + "," +
        std::to_string(rect.right) + "," + std::to_string(rect.bottom);
}

void set_xml_attr(std::string& xml, std::string_view tag, std::string_view name, const std::string& value) {
    if (xml.empty()) {
        xml = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\" ?>\n<ttplayer>\n</ttplayer>\n";
    }
    const std::string tag_needle = "<" + std::string(tag);
    auto tag_start = xml.find(tag_needle);
    if (tag_start == std::string::npos) {
        const auto root_end = xml.find("</ttplayer>");
        const std::string element = "\t<" + std::string(tag) + " />\n";
        if (root_end == std::string::npos) {
            xml += "\n<ttplayer>\n" + element + "</ttplayer>\n";
        } else {
            xml.insert(root_end, element);
        }
        tag_start = xml.find(tag_needle);
    }
    const auto tag_end = xml.find('>', tag_start);
    if (tag_end == std::string::npos) {
        return;
    }
    const std::string attr_needle = std::string(name) + "=\"";
    const auto attr_start = xml.find(attr_needle, tag_start);
    if (attr_start != std::string::npos && attr_start < tag_end) {
        const auto value_start = attr_start + attr_needle.size();
        const auto value_end = xml.find('"', value_start);
        if (value_end != std::string::npos) {
            xml.replace(value_start, value_end - value_start, value);
        }
    } else {
        const auto insert_at = tag_start + tag_needle.size();
        xml.insert(insert_at, " " + std::string(name) + "=\"" + value + "\"");
    }
}

void write_app_xml(const std::string& xml) {
    std::ofstream output(app_xml_config_path(), std::ios::binary | std::ios::trunc);
    if (output) {
        output << xml;
    }
}

void load_app_config() {
    const auto xml = read_app_xml();
    if (xml.empty()) {
        return;
    }
    if (const auto value = xml_attr(xml, "Player", "PlayerWnd")) {
        g_config.player_wnd = parse_xml_rect(*value);
    }
    if (const auto value = xml_attr(xml, "Player", "LyricWnd")) {
        g_config.lyric_wnd = parse_xml_rect(*value);
    }
    if (const auto value = xml_attr(xml, "Player", "EqualizerWnd")) {
        g_config.equalizer_wnd = parse_xml_rect(*value);
    }
    if (const auto value = xml_attr(xml, "Player", "PlayListWnd")) {
        g_config.playlist_wnd = parse_xml_rect(*value);
    }
    if (const auto value = xml_attr(xml, "Player", "DesklrcWnd")) {
        g_config.desklrc_wnd = parse_xml_rect(*value);
    }
    g_config.lyric_visible = parse_xml_bool_attr(xml, "Player", "LyricVisible");
    g_config.equalizer_visible = parse_xml_bool_attr(xml, "Player", "EqualizerVisible");
    g_config.playlist_visible = parse_xml_bool_attr(xml, "Player", "PlayListVisible");
    g_config.desktop_lyric_visible = parse_xml_bool_attr(xml, "Player", "LyricVisible2");
    g_config.play_mode = parse_xml_int_attr(xml, "Player", "PlayMode");
    g_config.volume = parse_xml_int_attr(xml, "Player", "Volume");
    g_config.play_follow_cursor = parse_xml_bool_attr(xml, "Player", "PlayFollowCursor");
    g_config.auto_switch_list = parse_xml_bool_attr(xml, "Player", "AutoSwitchList");
    g_config.desklrc_bkgnd_alpha = parse_xml_int_attr(xml, "DeskLrc", "BkgndAlpha");
    g_config.desklrc_bkgnd_color = parse_xml_color_attr(xml, "DeskLrc", "BkgndColor");
    g_config.desklrc_shadow_color = parse_xml_color_attr(xml, "DeskLrc", "ShadowColor");
    g_config.desklrc_base_color = parse_xml_color_attr(xml, "DeskLrc", "Profile_BColor2");
    g_config.desklrc_progress_color = parse_xml_color_attr(xml, "DeskLrc", "Profile_PColor2");
    g_config.desklrc_font_height = parse_logfont_height_attr(xml, "DeskLrc", "Font");
    if (const auto session = xml_block(xml, "PlaylistSession")) {
        g_loaded_playlist_session = parse_playlist_session(*session);
    }
}

std::optional<RECT> window_rect_if_valid(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) {
        return std::nullopt;
    }
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    if (rect.right <= rect.left || rect.bottom <= rect.top) {
        return std::nullopt;
    }
    return rect;
}

std::string bool_xml_value(bool value) {
    return value ? "1" : "0";
}

std::string color_xml_value(std::uint32_t rgb) {
    constexpr char digits[] = "0123456789abcdef";
    std::string value = "#000000";
    for (int i = 0; i < 6; ++i) {
        const int shift = (5 - i) * 4;
        value[1 + i] = digits[(rgb >> shift) & 0x0f];
    }
    return value;
}

std::string desklrc_font_xml_value(int height) {
    return "-" + std::to_string(std::clamp(height, 12, 96)) + ",0,0,0,700,0,0,0,1,0,0,4,0,幼圆";
}

int play_mode_to_xml_value(PlaylistPlayMode mode) {
    switch (mode) {
    case PlaylistPlayMode::Single:
        return 0;
    case PlaylistPlayMode::SingleLoop:
        return 1;
    case PlaylistPlayMode::Sequence:
        return 2;
    case PlaylistPlayMode::Loop:
        return 3;
    case PlaylistPlayMode::Random:
        return 4;
    }
    return 3;
}

PlaylistPlayMode play_mode_from_xml_value(int value) {
    switch (value) {
    case 0:
        return PlaylistPlayMode::Single;
    case 1:
        return PlaylistPlayMode::SingleLoop;
    case 2:
        return PlaylistPlayMode::Sequence;
    case 3:
        return PlaylistPlayMode::Loop;
    case 4:
        return PlaylistPlayMode::Random;
    default:
        return PlaylistPlayMode::Loop;
    }
}

std::uint32_t runtime_desktop_lyric_base_rgb();
std::uint32_t runtime_desktop_lyric_progress_rgb();
std::uint32_t runtime_desktop_lyric_shadow_rgb();
std::uint32_t runtime_desktop_lyric_background_rgb();
int runtime_desktop_lyric_background_alpha();
int runtime_desktop_lyric_font_height();
int audio_position_ms();
void commit_current_playlist_to_library();
void show_message(HWND hwnd, const wchar_t* text);


void save_app_config() {
    std::string xml = read_app_xml();
    if (const auto rect = window_rect_if_valid(g_app.main_window)) {
        set_xml_attr(xml, "Player", "PlayerWnd", rect_to_xml_value(*rect));
    }
    if (const auto* panel = find_panel(PanelKind::Lyrics)) {
        if (const auto rect = window_rect_if_valid(panel->hwnd)) {
            set_xml_attr(xml, "Player", "LyricWnd", rect_to_xml_value(*rect));
        }
        set_xml_attr(xml, "Player", "LyricVisible", bool_xml_value(IsWindowVisible(panel->hwnd) != FALSE));
    }
    if (const auto* panel = find_panel(PanelKind::Equalizer)) {
        if (const auto rect = window_rect_if_valid(panel->hwnd)) {
            set_xml_attr(xml, "Player", "EqualizerWnd", rect_to_xml_value(*rect));
        }
        set_xml_attr(xml, "Player", "EqualizerVisible", bool_xml_value(IsWindowVisible(panel->hwnd) != FALSE));
    }
    if (const auto* panel = find_panel(PanelKind::Playlist)) {
        if (const auto rect = window_rect_if_valid(panel->hwnd)) {
            set_xml_attr(xml, "Player", "PlayListWnd", rect_to_xml_value(*rect));
        }
        set_xml_attr(xml, "Player", "PlayListVisible", bool_xml_value(IsWindowVisible(panel->hwnd) != FALSE));
    }
    if (const auto rect = window_rect_if_valid(g_app.desktop_lyric_window)) {
        set_xml_attr(xml, "Player", "DesklrcWnd", rect_to_xml_value(*rect));
    }
    set_xml_attr(xml, "Player", "LyricVisible2", bool_xml_value(g_app.desktop_lyric_visible));
    set_xml_attr(xml, "Player", "PlayMode", std::to_string(play_mode_to_xml_value(g_playlist_play_mode)));
    set_xml_attr(xml, "Player", "AutoSwitchList", bool_xml_value(g_playlist_auto_switch));
    set_xml_attr(xml, "Player", "PlayFollowCursor", bool_xml_value(g_playlist_follow_cursor));
    set_xml_attr(xml, "Player", "Volume", std::to_string(g_audio.volume));
    set_xml_attr(xml, "DeskLrc", "BkgndAlpha", std::to_string(runtime_desktop_lyric_background_alpha()));
    set_xml_attr(xml, "DeskLrc", "TextAlpha", "255");
    set_xml_attr(xml, "DeskLrc", "BkgndColor", color_xml_value(runtime_desktop_lyric_background_rgb()));
    set_xml_attr(xml, "DeskLrc", "ShadowColor", color_xml_value(runtime_desktop_lyric_shadow_rgb()));
    set_xml_attr(xml, "DeskLrc", "Profile_BColor2", color_xml_value(runtime_desktop_lyric_base_rgb()));
    set_xml_attr(xml, "DeskLrc", "Profile_PColor2", color_xml_value(runtime_desktop_lyric_progress_rgb()));
    set_xml_attr(xml, "DeskLrc", "Font", desklrc_font_xml_value(runtime_desktop_lyric_font_height()));
    commit_current_playlist_to_library();
    PlaylistSessionState playlist_state;
    playlist_state.active_list_index = g_playlist_library.active_index();
    playlist_state.resume_position_ms = audio_position_ms();
    playlist_state.resume_playing = g_audio.playing;
    replace_xml_block(xml, "PlaylistSession", serialize_playlist_session(g_playlist_library, playlist_state));
    write_app_xml(xml);
}

void save_desktop_lyric_rect() {
    save_app_config();
}

void apply_loaded_player_config() {
    if (g_config.play_mode) {
        g_playlist_play_mode = play_mode_from_xml_value(*g_config.play_mode);
    }
    if (g_config.volume) {
        g_audio.volume = std::clamp(*g_config.volume, 0, 100);
    }
    if (g_config.play_follow_cursor) {
        g_playlist_follow_cursor = *g_config.play_follow_cursor;
    }
    if (g_config.auto_switch_list) {
        g_playlist_auto_switch = *g_config.auto_switch_list;
    }
    if (g_config.desktop_lyric_visible) {
        g_app.desktop_lyric_visible = *g_config.desktop_lyric_visible;
    }
}

void show_tray_menu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    const bool visible = IsWindowVisible(hwnd) != FALSE;
    AppendMenuW(menu, MF_STRING, kTrayMenuToggle, visible ? L"隐藏" : L"显示");
    AppendMenuW(menu, MF_STRING, kTrayMenuPlayPause, g_audio.playing ? L"暂停" : L"播放");
    AppendMenuW(menu, MF_STRING | (g_app.desktop_lyric_visible ? MF_CHECKED : 0), kTrayMenuDesktopLyrics, L"显示桌面歌词");
    AppendMenuW(menu, MF_STRING | (g_app.desktop_lyric_locked ? MF_CHECKED : 0) | (g_app.desktop_lyric_visible ? 0 : MF_GRAYED), kTrayMenuLockDesktopLyrics, L"锁定桌面歌词");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayMenuExit, L"退出");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}
std::wstring mci_quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool mci_send(std::wstring command) {
    return mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0;
}

std::wstring mci_error_message(MCIERROR error) {
    std::array<wchar_t, 256> buffer{};
    if (mciGetErrorStringW(error, buffer.data(), static_cast<UINT>(buffer.size()))) {
        return std::wstring(buffer.data());
    }
    return L"MCI error " + std::to_wstring(error);
}

std::optional<std::wstring> mci_send_error(std::wstring command) {
    const MCIERROR error = mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    if (error == 0) {
        return std::nullopt;
    }
    return mci_error_message(error);
}

std::optional<std::wstring> mci_query(std::wstring command) {
    std::array<wchar_t, 128> buffer{};
    if (mciSendStringW(command.c_str(), buffer.data(), static_cast<UINT>(buffer.size()), nullptr) != 0) {
        return std::nullopt;
    }
    return std::wstring(buffer.data());
}

int to_int_or_zero(const std::optional<std::wstring>& value) {
    if (!value || value->empty()) {
        return 0;
    }
    return _wtoi(value->c_str());
}

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return L"";
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return L"";
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), count);
    return wide;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return "";
    }
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) {
        return "";
    }
    std::string utf8(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), count, nullptr, nullptr);
    return utf8;
}

std::wstring latin1_to_wide(std::string_view text) {
    std::wstring wide;
    wide.reserve(text.size());
    for (unsigned char c : text) {
        wide.push_back(static_cast<wchar_t>(c));
    }
    return wide;
}

std::wstring trim_nulls(std::wstring text) {
    while (!text.empty() && (text.back() == L'\0' || text.back() == L' ')) {
        text.pop_back();
    }
    return text;
}
bool is_numeric_noise(std::wstring_view text) {
    bool saw_digit = false;
    for (wchar_t c : text) {
        if (std::iswdigit(c)) {
            saw_digit = true;
            continue;
        }
        if (std::iswspace(c)) {
            continue;
        }
        return false;
    }
    return saw_digit;
}

bool is_meaningful_tag_text(std::wstring_view text) {
    return !text.empty() && !is_numeric_noise(text);
}

std::wstring decode_id3_text(const std::vector<char>& data) {
    if (data.empty()) {
        return L"";
    }
    const auto encoding = static_cast<unsigned char>(data[0]);
    const char* payload = data.data() + 1;
    const std::size_t payload_size = data.size() - 1;
    if (encoding == 3) {
        return trim_nulls(utf8_to_wide(std::string_view(payload, payload_size)));
    }
    if (encoding == 0) {
        return trim_nulls(latin1_to_wide(std::string_view(payload, payload_size)));
    }
    if ((encoding == 1 || encoding == 2) && payload_size >= 2) {
        const bool big_endian = encoding == 2 || (static_cast<unsigned char>(payload[0]) == 0xFE && static_cast<unsigned char>(payload[1]) == 0xFF);
        std::size_t offset = encoding == 1 && (static_cast<unsigned char>(payload[0]) == 0xFF || static_cast<unsigned char>(payload[0]) == 0xFE) ? 2 : 0;
        std::wstring result;
        for (; offset + 1 < payload_size; offset += 2) {
            const auto a = static_cast<unsigned char>(payload[offset]);
            const auto b = static_cast<unsigned char>(payload[offset + 1]);
            wchar_t ch = static_cast<wchar_t>(big_endian ? ((a << 8) | b) : (a | (b << 8)));
            if (ch == L'\0') {
                break;
            }
            result.push_back(ch);
        }
        return trim_nulls(result);
    }
    return L"";
}

std::uint32_t synchsafe(const char* p) {
    return (static_cast<std::uint32_t>(p[0] & 0x7F) << 21)
        | (static_cast<std::uint32_t>(p[1] & 0x7F) << 14)
        | (static_cast<std::uint32_t>(p[2] & 0x7F) << 7)
        | static_cast<std::uint32_t>(p[3] & 0x7F);
}

std::uint32_t be32(const char* p) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) << 24)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 16)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 8)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(p[3]));
}

int track_number_from_text(std::wstring_view text);
int rating_from_popm_frame(const std::vector<char>& frame);

void read_id3_metadata(const std::filesystem::path& path, AudioMetadata& metadata) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return;
    }
    std::array<char, 10> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (file.gcount() != static_cast<std::streamsize>(header.size()) || std::string_view(header.data(), 3) != "ID3") {
        return;
    }
    const int major = static_cast<unsigned char>(header[3]);
    const auto tag_size = synchsafe(header.data() + 6);
    std::vector<char> tag(tag_size);
    file.read(tag.data(), static_cast<std::streamsize>(tag.size()));
    std::size_t offset = 0;
    while (offset + 10 <= tag.size()) {
        const std::string id(tag.data() + offset, 4);
        if (id[0] == '\0') {
            break;
        }
        const auto frame_size = major == 4 ? synchsafe(tag.data() + offset + 4) : be32(tag.data() + offset + 4);
        offset += 10;
        if (frame_size == 0 || offset + frame_size > tag.size()) {
            break;
        }
        std::vector<char> frame(tag.begin() + static_cast<std::ptrdiff_t>(offset), tag.begin() + static_cast<std::ptrdiff_t>(offset + frame_size));
        const auto text = decode_id3_text(frame);
        if (id == "TIT2" && is_meaningful_tag_text(text)) {
            metadata.title = text;
        } else if (id == "TPE1" && is_meaningful_tag_text(text)) {
            metadata.artist = text;
        } else if (id == "TALB" && is_meaningful_tag_text(text)) {
            metadata.album = text;
        } else if (id == "TRCK" && is_meaningful_tag_text(text)) {
            metadata.track_number = track_number_from_text(text);
        } else if (id == "POPM") {
            metadata.rating = rating_from_popm_frame(frame);
        }
        offset += frame_size;
    }
}

std::wstring uppercase_extension(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    if (!ext.empty() && ext.front() == L'.') {
        ext.erase(ext.begin());
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    return ext;
}

std::optional<std::wstring> mp3_format_label(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::vector<unsigned char> data(512 * 1024);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(file.gcount()));
    static constexpr int bitrate_v1_l3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static constexpr int bitrate_v2_l3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static constexpr int sample_v1[] = {44100, 48000, 32000, 0};
    for (std::size_t i = 0; i + 3 < data.size(); ++i) {
        if (data[i] != 0xFF || (data[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        const int version = (data[i + 1] >> 3) & 0x03;
        const int layer = (data[i + 1] >> 1) & 0x03;
        const int bitrate_index = (data[i + 2] >> 4) & 0x0F;
        const int sample_index = (data[i + 2] >> 2) & 0x03;
        if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 || sample_index == 3) {
            continue;
        }
        int sample_rate = sample_v1[sample_index];
        if (version == 2) {
            sample_rate /= 2;
        } else if (version == 0) {
            sample_rate /= 4;
        }
        const int bitrate = version == 3 ? bitrate_v1_l3[bitrate_index] : bitrate_v2_l3[bitrate_index];
        if (sample_rate > 0 && bitrate > 0) {
            return L"MP3 " + std::to_wstring(sample_rate / 1000) + L"kHz " + std::to_wstring(bitrate) + L"K";
        }
    }
    return std::nullopt;
}


std::wstring known_album_for_track(std::wstring_view artist, std::wstring_view title) {
    if (artist == L"Guns N' Roses" && title == L"Don't Cry") {
        return L"Use Your Illusion I";
    }
    return L"";
}

int track_number_from_text(std::wstring_view text) {
    int value = 0;
    bool saw_digit = false;
    for (wchar_t c : text) {
        if (c == L'/' && saw_digit) {
            break;
        }
        if (!std::iswdigit(c)) {
            if (saw_digit) {
                break;
            }
            continue;
        }
        saw_digit = true;
        value = value * 10 + static_cast<int>(c - L'0');
    }
    return saw_digit ? value : 0;
}

int rating_from_popm_frame(const std::vector<char>& frame) {
    const auto terminator = std::find(frame.begin(), frame.end(), '\0');
    if (terminator == frame.end() || terminator + 1 == frame.end()) {
        return 0;
    }
    const int raw = static_cast<unsigned char>(*(terminator + 1));
    if (raw == 0) {
        return 0;
    }
    return std::clamp((raw + 25) / 51, 1, 5);
}

int fallback_track_number_from_path(const std::filesystem::path& path) {
    return track_number_from_text(path.stem().wstring());
}

std::wstring sort_text_key(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

AudioMetadata metadata_for_track(const PlaylistTrack& track) {
    const auto key = track.path.wstring();
    if (const auto it = g_metadata_cache.find(key); it != g_metadata_cache.end()) {
        return it->second;
    }
    AudioMetadata metadata;
    metadata.title = track.title;
    const auto dash = track.title.find(L" - ");
    if (dash != std::wstring::npos) {
        metadata.artist = track.title.substr(0, dash);
        metadata.title = track.title.substr(dash + 3);
    }
    read_id3_metadata(track.path, metadata);
    if (metadata.album.empty()) {
        metadata.album = known_album_for_track(metadata.artist, metadata.title);
    }
    if (metadata.track_number <= 0) {
        metadata.track_number = fallback_track_number_from_path(track.path);
    }
    if (metadata.format.empty()) {
        if (uppercase_extension(track.path) == L"MP3") {
            metadata.format = mp3_format_label(track.path).value_or(L"MP3");
        } else {
            metadata.format = uppercase_extension(track.path);
        }
    }
    g_metadata_cache.emplace(key, metadata);
    return metadata;
}

const PlaylistTrack* current_track() {
    if (g_playlist.empty() || g_audio.current_index < 0 || g_audio.current_index >= static_cast<int>(g_playlist.size())) {
        return nullptr;
    }
    return &g_playlist[static_cast<std::size_t>(g_audio.current_index)];
}

int audio_position_ms() {
    return g_audio.opened ? to_int_or_zero(mci_query(L"status ttplayer_audio position")) : 0;
}

int audio_length_ms() {
    return g_audio.opened ? to_int_or_zero(mci_query(L"status ttplayer_audio length")) : 0;
}


std::optional<std::uint32_t> id3v2_total_size(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::array<char, 10> header{};
    file.read(header.data(), static_cast<std::streamsize>(header.size()));
    if (file.gcount() != static_cast<std::streamsize>(header.size()) || std::string_view(header.data(), 3) != "ID3") {
        return 0;
    }
    return synchsafe(header.data() + 6) + 10;
}

std::optional<int> mp3_duration_ms_from_file(const std::filesystem::path& path) {
    const auto start = id3v2_total_size(path).value_or(0);
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::error_code error;
    auto file_size = std::filesystem::file_size(path, error);
    if (error || file_size <= start) {
        return std::nullopt;
    }
    if (file_size > 128) {
        file.seekg(-128, std::ios::end);
        std::array<char, 3> tag{};
        file.read(tag.data(), static_cast<std::streamsize>(tag.size()));
        if (std::string_view(tag.data(), tag.size()) == "TAG") {
            file_size -= 128;
        }
    }
    file.clear();
    file.seekg(static_cast<std::streamoff>(start), std::ios::beg);
    std::array<unsigned char, 4096> data{};
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    static constexpr int bitrate_v1_l3[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static constexpr int bitrate_v2_l3[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    for (std::size_t i = 0; i + 3 < count; ++i) {
        if (data[i] != 0xFF || (data[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        const int version = (data[i + 1] >> 3) & 0x03;
        const int layer = (data[i + 1] >> 1) & 0x03;
        const int bitrate_index = (data[i + 2] >> 4) & 0x0F;
        const int sample_index = (data[i + 2] >> 2) & 0x03;
        if (version == 1 || layer != 1 || bitrate_index == 0 || bitrate_index == 15 || sample_index == 3) {
            continue;
        }
        const int bitrate = version == 3 ? bitrate_v1_l3[bitrate_index] : bitrate_v2_l3[bitrate_index];
        if (bitrate <= 0) {
            continue;
        }
        const auto audio_bytes = file_size - start - i;
        return static_cast<int>((audio_bytes * 8ULL * 1000ULL) / static_cast<unsigned long long>(bitrate * 1000));
    }
    return std::nullopt;
}

int display_length_ms() {
    const auto* track = current_track();
    if (track && uppercase_extension(track->path) == L"MP3") {
        if (const auto duration = mp3_duration_ms_from_file(track->path)) {
            return *duration;
        }
    }
    return audio_length_ms();
}
std::wstring format_position_time_ms(int ms) {
    const int total_seconds = std::max(0, ms / 1000);
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    std::wostringstream out;
    if (minutes < 10) {
        out << L"0";
    }
    out << minutes << L":";
    if (seconds < 10) {
        out << L"0";
    }
    out << seconds;
    return out.str();
}

std::wstring format_length_time_ms(int ms) {
    const int total_seconds = std::max(0, ms / 1000);
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    std::wostringstream out;
    out << minutes << L":";
    if (seconds < 10) {
        out << L"0";
    }
    out << seconds;
    return out.str();
}

void close_audio() {
    if (g_audio.opened) {
        mci_send(L"close ttplayer_audio");
        g_audio.opened = false;
        g_audio.playing = false;
    }
}

bool open_current_audio() {
    close_audio();
    g_last_audio_error.clear();
    const auto* track = current_track();
    if (!track) {
        g_last_audio_error = L"播放列表里没有可播放的音频。";
        return false;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(track->path, error)) {
        g_last_audio_error = L"找不到音频文件：\n" + track->path.wstring();
        return false;
    }
    if (const auto open_error = mci_send_error(L"open " + mci_quote(track->path) + L" type mpegvideo alias ttplayer_audio")) {
        g_last_audio_error = L"无法打开音频文件：\n" + track->path.wstring()
            + L"\n\n" + *open_error
            + L"\n\n当前版本使用 Windows MCI/WinMM 播放。MP3/WAV/WMA 最可靠；FLAC/APE/OGG/M4A 需要系统编解码器支持。";
        return false;
    }
    g_audio.opened = true;
    mci_send(L"set ttplayer_audio time format milliseconds");
    mci_send(L"setaudio ttplayer_audio volume to " + std::to_wstring(g_audio.volume * 10));
    return true;
}

void report_audio_error(HWND hwnd) {
    show_message(hwnd, g_last_audio_error.empty() ? L"无法播放当前音频。" : g_last_audio_error.c_str());
}

void play_audio(HWND hwnd) {
    if (!g_audio.opened && !open_current_audio()) {
        report_audio_error(hwnd);
        invalidate_playback_views(hwnd);
        return;
    }
    if (const auto play_error = mci_send_error(L"play ttplayer_audio")) {
        g_audio.playing = false;
        g_last_audio_error = L"无法开始播放：\n" + *play_error;
        report_audio_error(hwnd);
        invalidate_playback_views(hwnd);
        return;
    }
    g_audio.playing = true;
    invalidate_playback_views(hwnd);
    save_app_config();
}
void pause_audio(HWND hwnd) {
    if (g_audio.opened) {
        mci_send(L"pause ttplayer_audio");
    }
    g_audio.playing = false;
    invalidate_playback_views(hwnd);
    save_app_config();
}

void stop_audio(HWND hwnd) {
    if (g_audio.opened) {
        mci_send(L"stop ttplayer_audio");
        mci_send(L"seek ttplayer_audio to start");
    }
    g_audio.playing = false;
    invalidate_playback_views(hwnd);
    save_app_config();
}

void play_track_at(HWND hwnd, int index) {
    if (g_playlist.empty()) {
        return;
    }
    const int count = static_cast<int>(g_playlist.size());
    g_audio.current_index = (index % count + count) % count;
    g_playlist_library.set_active_track_index(g_audio.current_index);
    scroll_playlist_to_current();
    if (open_current_audio()) {
        play_audio(hwnd);
    } else {
        report_audio_error(hwnd);
        invalidate_playback_views(hwnd);
        save_app_config();
    }
}

void next_track(HWND hwnd) {
    play_track_at(hwnd, g_audio.current_index + 1);
}

void previous_track(HWND hwnd) {
    play_track_at(hwnd, g_audio.current_index - 1);
}

void populate_track_duration(PlaylistTrack& track) {
    if (uppercase_extension(track.path) == L"MP3") {
        track.duration_ms = mp3_duration_ms_from_file(track.path).value_or(0);
    }
}

void commit_current_playlist_to_library() {
    g_playlist_library.replace_active(g_playlist, g_playlist_name, g_audio.current_index);
}

void load_active_playlist_from_library(HWND hwnd) {
    close_audio();
    g_playlist = g_playlist_library.active_tracks();
    g_playlist_name = g_playlist_library.active_name();
    g_audio.current_index = g_playlist_library.active_track_index();
    g_playlist_scroll = 0;
    open_current_audio();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void activate_playlist_list(HWND hwnd, std::size_t index) {
    if (index == g_playlist_library.active_index()) {
        return;
    }
    commit_current_playlist_to_library();
    if (g_playlist_library.switch_to(index)) {
        load_active_playlist_from_library(hwnd);
        save_app_config();
    }
}

std::wstring playlist_name_from_file(const std::filesystem::path& path) {
    auto name = path.stem().wstring();
    const auto first = std::find_if_not(name.begin(), name.end(), [](wchar_t c) { return std::iswspace(c); });
    const auto last = std::find_if_not(name.rbegin(), name.rend(), [](wchar_t c) { return std::iswspace(c); }).base();
    if (first >= last) {
        return L"";
    }
    return std::wstring(first, last);
}

std::vector<PlaylistTrack> load_directory_tracks_with_durations(const std::filesystem::path& directory) {
    auto tracks = load_playlist_from_directory(directory);
    for (auto& track : tracks) {
        populate_track_duration(track);
    }
    return tracks;
}

void append_track_path(HWND hwnd, const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || !is_supported_audio_file(path)) {
        return;
    }
    PlaylistTrack track{path, playlist_title_from_path(path)};
    populate_track_duration(track);
    g_playlist.push_back(std::move(track));
    commit_current_playlist_to_library();
    clamp_playlist_scroll_for_window(find_panel(PanelKind::Playlist) ? find_panel(PanelKind::Playlist)->hwnd : hwnd);
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void append_tracks(HWND hwnd, std::vector<PlaylistTrack> tracks) {
    if (tracks.empty()) {
        return;
    }
    for (auto& track : tracks) {
        populate_track_duration(track);
        g_playlist.push_back(std::move(track));
    }
    commit_current_playlist_to_library();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

std::vector<PlaylistTrack> tracks_from_dropped_path(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        return load_directory_tracks_with_durations(path);
    }
    if (!std::filesystem::is_regular_file(path, error) || !is_supported_audio_file(path)) {
        return {};
    }
    PlaylistTrack track{path, playlist_title_from_path(path)};
    populate_track_duration(track);
    return {std::move(track)};
}

std::wstring drop_list_name(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        return L"";
    }
    auto name = path.filename().wstring();
    return name.empty() ? path.wstring() : name;
}

std::vector<PlaylistTrack> tracks_from_drop(HDROP drop, std::wstring& list_name) {
    std::vector<PlaylistTrack> tracks;
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(drop, i, nullptr, 0);
        if (length == 0) {
            continue;
        }
        std::wstring buffer(static_cast<std::size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, i, buffer.data(), length + 1);
        buffer.resize(length);
        const std::filesystem::path dropped_path(buffer);
        if (list_name.empty()) {
            list_name = drop_list_name(dropped_path);
        }
        auto item_tracks = tracks_from_dropped_path(dropped_path);
        tracks.insert(tracks.end(), std::make_move_iterator(item_tracks.begin()), std::make_move_iterator(item_tracks.end()));
    }
    return tracks;
}

void replace_playlist(HWND hwnd, std::vector<PlaylistTrack> tracks, std::wstring list_name = L"") {
    if (tracks.empty()) {
        return;
    }
    close_audio();
    if (!list_name.empty()) {
        g_playlist_name = std::move(list_name);
    }
    g_playlist = std::move(tracks);
    g_audio.current_index = 0;
    commit_current_playlist_to_library();
    g_playlist_scroll = 0;
    open_current_audio();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void handle_playlist_drop(HWND hwnd, HDROP drop) {
    POINT point{};
    DragQueryPoint(drop, &point);
    RECT client{};
    GetClientRect(hwnd, &client);
    std::wstring list_name;
    auto tracks = tracks_from_drop(drop, list_name);
    DragFinish(drop);
    if (tracks.empty()) {
        return;
    }
    const auto mode = playlist_drop_mode_at(point.x, point.y, client.bottom, g_playlist_divider_x);
    if (mode == PlaylistDropMode::AppendToList) {
        append_tracks(g_app.main_window ? g_app.main_window : hwnd, std::move(tracks));
    } else {
        commit_current_playlist_to_library();
        g_playlist_library.add_list(std::move(tracks), std::move(list_name));
        load_active_playlist_from_library(g_app.main_window ? g_app.main_window : hwnd);
        save_app_config();
    }
}
void remove_track_at(HWND hwnd, int index) {
    if (index < 0 || index >= static_cast<int>(g_playlist.size())) {
        return;
    }
    const bool removing_current = index == g_audio.current_index;
    if (removing_current) {
        close_audio();
    }
    g_playlist.erase(g_playlist.begin() + index);
    if (g_playlist.empty()) {
        g_audio.current_index = 0;
    } else {
        g_audio.current_index = std::clamp(g_audio.current_index, 0, static_cast<int>(g_playlist.size()) - 1);
        if (removing_current) {
            open_current_audio();
        }
    }
    commit_current_playlist_to_library();
    scroll_playlist_to_current();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void clear_playlist(HWND hwnd) {
    close_audio();
    g_playlist.clear();
    commit_current_playlist_to_library();
    g_audio.current_index = 0;
    g_playlist_scroll = 0;
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void remove_duplicate_tracks(HWND hwnd) {
    std::set<std::wstring> seen;
    std::vector<PlaylistTrack> kept;
    kept.reserve(g_playlist.size());
    for (auto& track : g_playlist) {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(track.path, error);
        if (error) {
            canonical = std::filesystem::absolute(track.path, error);
        }
        auto key = (error ? track.path : canonical).wstring();
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (seen.insert(key).second) {
            kept.push_back(std::move(track));
        }
    }
    g_playlist = std::move(kept);
    commit_current_playlist_to_library();
    g_audio.current_index = std::clamp(g_audio.current_index, 0, std::max(0, static_cast<int>(g_playlist.size()) - 1));
    scroll_playlist_to_current();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void remove_missing_tracks(HWND hwnd) {
    g_playlist.erase(std::remove_if(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& track) {
                         std::error_code error;
                         return !std::filesystem::exists(track.path, error);
                     }),
                     g_playlist.end());
    commit_current_playlist_to_library();
    g_audio.current_index = std::clamp(g_audio.current_index, 0, std::max(0, static_cast<int>(g_playlist.size()) - 1));
    scroll_playlist_to_current();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

std::filesystem::path current_track_path_before_reorder() {
    if (g_playlist.empty() || g_audio.current_index < 0 || g_audio.current_index >= static_cast<int>(g_playlist.size())) {
        return {};
    }
    return g_playlist[static_cast<std::size_t>(g_audio.current_index)].path;
}

void restore_current_track_after_reorder(const std::filesystem::path& current_path) {
    if (g_playlist.empty()) {
        g_audio.current_index = 0;
        return;
    }
    if (!current_path.empty()) {
        const auto key = current_path.wstring();
        for (std::size_t i = 0; i < g_playlist.size(); ++i) {
            if (g_playlist[i].path.wstring() == key) {
                g_audio.current_index = static_cast<int>(i);
                return;
            }
        }
    }
    g_audio.current_index = std::clamp(g_audio.current_index, 0, static_cast<int>(g_playlist.size()) - 1);
}

void finish_playlist_reorder(HWND hwnd, const std::filesystem::path& current_path) {
    restore_current_track_after_reorder(current_path);
    scroll_playlist_to_current();
    invalidate_panel_kind(PanelKind::Playlist);
    invalidate_playback_views(hwnd);
}

void sort_playlist_by_title(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        return a.title < b.title;
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void sort_playlist_by_filename(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        return a.path.filename().wstring() < b.path.filename().wstring();
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void sort_playlist_by_path(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        return a.path.wstring() < b.path.wstring();
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void sort_playlist_by_duration(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        return a.duration_ms < b.duration_ms;
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}


void sort_playlist_by_album(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        const auto ma = metadata_for_track(a);
        const auto mb = metadata_for_track(b);
        const auto aa = sort_text_key(ma.album);
        const auto ab = sort_text_key(mb.album);
        if (aa != ab) {
            return aa < ab;
        }
        if (ma.track_number != mb.track_number) {
            return ma.track_number < mb.track_number;
        }
        return sort_text_key(a.title) < sort_text_key(b.title);
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void sort_playlist_by_rating(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        const auto ma = metadata_for_track(a);
        const auto mb = metadata_for_track(b);
        if (ma.rating != mb.rating) {
            return ma.rating > mb.rating;
        }
        return sort_text_key(a.title) < sort_text_key(b.title);
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void sort_playlist_by_track_number(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        const auto ma = metadata_for_track(a);
        const auto mb = metadata_for_track(b);
        if (ma.track_number != mb.track_number) {
            if (ma.track_number == 0) {
                return false;
            }
            if (mb.track_number == 0) {
                return true;
            }
            return ma.track_number < mb.track_number;
        }
        return sort_text_key(a.title) < sort_text_key(b.title);
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void shuffle_playlist(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::mt19937 rng{std::random_device{}()};
    std::shuffle(g_playlist.begin(), g_playlist.end(), rng);
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void advance_finished_track_for_mode(HWND hwnd) {
    if (g_playlist.empty()) {
        stop_audio(hwnd);
        return;
    }
    switch (g_playlist_play_mode) {
    case PlaylistPlayMode::Single:
        stop_audio(hwnd);
        break;
    case PlaylistPlayMode::SingleLoop:
        play_track_at(hwnd, g_audio.current_index);
        break;
    case PlaylistPlayMode::Sequence:
        if (g_audio.current_index + 1 >= static_cast<int>(g_playlist.size())) {
            stop_audio(hwnd);
        } else {
            play_track_at(hwnd, g_audio.current_index + 1);
        }
        break;
    case PlaylistPlayMode::Loop:
        play_track_at(hwnd, g_audio.current_index + 1);
        break;
    case PlaylistPlayMode::Random: {
        std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int> dist(0, static_cast<int>(g_playlist.size()) - 1);
        play_track_at(hwnd, dist(rng));
        break;
    }
    }
}
void set_audio_volume(HWND hwnd, int volume) {
    g_audio.volume = std::clamp(volume, 0, 100);
    if (g_audio.opened) {
        mci_send(L"setaudio ttplayer_audio volume to " + std::to_wstring(g_audio.volume * 10));
    }
    invalidate_playback_views(hwnd);
    save_app_config();
}


void seek_audio(HWND hwnd, int position_ms) {
    if (!g_audio.opened) {
        return;
    }
    const int target = std::clamp(position_ms, 0, std::max(0, audio_length_ms()));
    mci_send(L"seek ttplayer_audio to " + std::to_wstring(target));
    if (g_audio.playing) {
        mci_send(L"play ttplayer_audio");
    }
    invalidate_playback_views(hwnd);
    save_app_config();
}

void handle_slider_click(HWND hwnd, const std::string& control, int x) {
    if (!g_skin_definition) {
        return;
    }
    const auto it = g_skin_definition->player.controls.find(control);
    if (it == g_skin_definition->player.controls.end()) {
        return;
    }
    const auto& rect = it->second.position;
    const int width = std::max(1, rect.right - rect.left);
    const double fraction = std::clamp(static_cast<double>(x - rect.left) / static_cast<double>(width), 0.0, 1.0);
    if (control == "progress") {
        const int length = audio_length_ms();
        if (length > 0) {
            seek_audio(hwnd, static_cast<int>(length * fraction));
        }
    } else if (control == "volume") {
        set_audio_volume(hwnd, static_cast<int>(fraction * 100.0));
    }
}
void maybe_advance_finished_track(HWND hwnd) {
    if (!g_audio.playing || !g_audio.opened) {
        return;
    }
    const int length = audio_length_ms();
    const int position = audio_position_ms();
    if (length > 0 && position >= length - 250) {
        advance_finished_track_for_mode(hwnd);
    }
}
void handle_player_control(HWND hwnd, const std::string& control) {
    if (control == "exit") {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    } else if (control == "minimize") {
        minimize_to_tray(hwnd);
    } else if (control == "play") {
        if (g_audio.playing) {
            pause_audio(hwnd);
        } else {
            play_audio(hwnd);
        }
    } else if (control == "pause") {
        pause_audio(hwnd);
    } else if (control == "stop") {
        stop_audio(hwnd);
    } else if (control == "prev") {
        previous_track(hwnd);
    } else if (control == "next") {
        next_track(hwnd);
    } else if (control == "lyric") {
        toggle_panel(PanelKind::Lyrics);
    } else if (control == "playlist") {
        toggle_panel(PanelKind::Playlist);
    } else if (control == "equalizer") {
        toggle_panel(PanelKind::Equalizer);
    } else if (control == "visual") {
        if (g_skin_definition) {
            const auto it = g_skin_definition->player.controls.find("visual");
            if (it != g_skin_definition->player.controls.end() && it->second.position.top < 32) {
                toggle_panel(PanelKind::Equalizer);
            }
        }
    }
}
HBITMAP load_bitmap_file(const std::filesystem::path& path) {
    return static_cast<HBITMAP>(LoadImageW(nullptr, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE));
}

HICON load_icon_file(const std::filesystem::path& path) {
    return static_cast<HICON>(LoadImageW(nullptr, path.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
}

bool is_icon_asset(const std::filesystem::path& path) {
    const auto extension = path.extension().wstring();
    return extension == L".ico" || extension == L".ICO";
}

std::filesystem::path executable_dir() {
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length == 0) {
        return std::filesystem::current_path();
    }
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}
void load_skin_bitmaps() {
    const auto skin_path = find_default_packaged_skin(packaged_skin_dir(executable_dir()));
    if (!skin_path) {
        return;
    }
    const auto skin = SkinPackage::open(*skin_path);
    if (!skin) {
        return;
    }
    const auto xml = skin->read_text("Skin.xml");
    if (xml) {
        auto definition = parse_skin_definition(*xml);
        if (definition && apply_bitmap_sizes(*definition, *skin)) {
            g_skin_definition = *definition;
        }
    }

    const auto player_image = g_skin_definition ? g_skin_definition->player.image : std::string("player_skin.bmp");
    const auto lyric_image = g_skin_definition ? g_skin_definition->lyrics.image : std::string("lyric_skin.bmp");
    const auto playlist_image = g_skin_definition ? g_skin_definition->playlist.image : std::string("playlist_skin.bmp");
    const auto equalizer_image = g_skin_definition ? g_skin_definition->equalizer.image : std::string("equalizer_skin.bmp");

    if (const auto path = skin->materialize(player_image)) {
        g_skin.player = load_bitmap_file(*path);
    }
    if (const auto path = skin->materialize(lyric_image)) {
        g_skin.lyrics = load_bitmap_file(*path);
    }
    if (const auto path = skin->materialize(playlist_image)) {
        g_skin.playlist = load_bitmap_file(*path);
    }
    if (const auto path = skin->materialize(equalizer_image)) {
        g_skin.equalizer = load_bitmap_file(*path);
    }
    if (g_skin_definition) {
        for (const auto& asset_name : control_asset_names(g_skin_definition->player)) {
            if (const auto path = skin->materialize(asset_name)) {
                if (is_icon_asset(*path)) {
                    if (HICON icon = load_icon_file(*path)) {
                        g_skin.player_icons.emplace(asset_name, icon);
                    }
                } else if (HBITMAP bitmap = load_bitmap_file(*path)) {
                    g_skin.player_controls.emplace(asset_name, bitmap);
                }
            }
        }
        for (const auto& window : {g_skin_definition->lyrics, g_skin_definition->playlist, g_skin_definition->equalizer}) {
            for (const auto& asset_name : control_asset_names(window)) {
                if (asset_name.empty() || g_skin.panel_controls.contains(asset_name)) {
                    continue;
                }
                if (const auto path = skin->materialize(asset_name)) {
                    if (HBITMAP bitmap = load_bitmap_file(*path)) {
                        g_skin.panel_controls.emplace(asset_name, bitmap);
                    }
                }
            }
        }
        if (const auto path = skin->materialize("desklrc_bar.bmp")) {
            g_skin.desktop_lyric_toolbar = load_bitmap_file(*path);
        }
        for (const std::string asset_name : {
                 "desklrc_prev.bmp",
                 "desklrc_play.bmp",
                 "desklrc_pause.bmp",
                 "desklrc_next.bmp",
                 "desklrc_lines.bmp",
                 "desklrc_list.bmp",
                 "desklrc_kalaok.bmp",
                 "desklrc_settings.bmp",
                 "desklrc_lock.bmp",
                 "desklrc_return.bmp",
                 "desklrc_close.bmp",
             }) {
            if (g_skin.desktop_lyric_controls.contains(asset_name)) {
                continue;
            }
            if (const auto path = skin->materialize(asset_name)) {
                if (HBITMAP bitmap = load_bitmap_file(*path)) {
                    g_skin.desktop_lyric_controls.emplace(asset_name, bitmap);
                    continue;
                }
            }
        }
        for (const std::string asset_name : {"scrollbar_bar.bmp", "scrollbar_thumb.bmp", "scrollbar_button.bmp"}) {
            if (g_skin.panel_controls.contains(asset_name)) {
                continue;
            }
            if (const auto path = skin->materialize(asset_name)) {
                if (HBITMAP bitmap = load_bitmap_file(*path)) {
                    g_skin.panel_controls.emplace(asset_name, bitmap);
                }
            }
        }
    }
}

void destroy_skin_bitmaps() {
    for (HBITMAP bitmap : {g_skin.player, g_skin.lyrics, g_skin.playlist, g_skin.equalizer, g_skin.desktop_lyric_toolbar}) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }
    for (auto& [_, bitmap] : g_skin.player_controls) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }
    for (auto& [_, bitmap] : g_skin.desktop_lyric_controls) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }
    for (auto& [_, bitmap] : g_skin.panel_controls) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
    }
    for (auto& [_, icon] : g_skin.player_icons) {
        if (icon) {
            DestroyIcon(icon);
        }
    }
    g_skin = {};
    g_skin_definition.reset();
}

SkinSize player_window_size() {
    if (g_skin_definition) {
        return g_skin_definition->player.size;
    }
    return SkinSize{kMainWidth, kMainHeight};
}

SkinSize panel_window_size(PanelKind kind) {
    if (g_skin_definition) {
        switch (kind) {
        case PanelKind::Lyrics:
            return g_skin_definition->lyrics.size;
        case PanelKind::Playlist:
            return g_skin_definition->playlist.size;
        case PanelKind::Equalizer:
            return g_skin_definition->equalizer.size;
        }
    }
    switch (kind) {
    case PanelKind::Lyrics:
        return SkinSize{kPanelWidth, kLyricHeight};
    case PanelKind::Playlist:
        return SkinSize{kPanelWidth, kPlaylistHeight};
    case PanelKind::Equalizer:
        return SkinSize{kPanelWidth, kEqualizerHeight};
    }
    return SkinSize{kPanelWidth, kPlaylistHeight};
}

std::optional<SkinRect> panel_resize_rect(PanelKind kind) {
    if (!g_skin_definition) {
        return std::nullopt;
    }
    switch (kind) {
    case PanelKind::Lyrics:
        return g_skin_definition->lyrics.resize_rect;
    case PanelKind::Playlist:
        return g_skin_definition->playlist.resize_rect;
    case PanelKind::Equalizer:
        return g_skin_definition->equalizer.resize_rect;
    }
    return std::nullopt;
}
bool window_can_resize(HWND hwnd) {
    if (hwnd == g_app.main_window) {
        return g_skin_definition && g_skin_definition->player.resize_rect.has_value();
    }
    if (auto* panel = find_panel(hwnd)) {
        return panel_resize_rect(panel->kind).has_value();
    }
    return false;
}
std::optional<SkinRect> panel_window_position(PanelKind kind) {
    if (!g_skin_definition) {
        return std::nullopt;
    }
    switch (kind) {
    case PanelKind::Lyrics:
        return g_skin_definition->lyrics.position;
    case PanelKind::Playlist:
        return g_skin_definition->playlist.position;
    case PanelKind::Equalizer:
        return g_skin_definition->equalizer.position;
    }
    return std::nullopt;
}
HBITMAP skin_bitmap_for_panel(PanelKind kind) {
    switch (kind) {
    case PanelKind::Lyrics:
        return g_skin.lyrics;
    case PanelKind::Playlist:
        return g_skin.playlist;
    case PanelKind::Equalizer:
        return g_skin.equalizer;
    }
    return nullptr;
}

WindowRect skin_rect_to_window_rect(const SkinRect& rect) {
    return WindowRect{rect.left, rect.top, rect.right, rect.bottom};
}

void transparent_blit_rect(HDC dc, HDC source, const WindowRect& dest, const WindowRect& src) {
    const int width = dest.right - dest.left;
    const int height = dest.bottom - dest.top;
    const int source_width = src.right - src.left;
    const int source_height = src.bottom - src.top;
    if (width <= 0 || height <= 0 || source_width <= 0 || source_height <= 0) {
        return;
    }
    TransparentBlt(dc, dest.left, dest.top, width, height, source, src.left, src.top, source_width, source_height, skin_transparent_key());
}
bool draw_bitmap_to_client(HDC dc, HBITMAP bitmap, const RECT& client, std::optional<SkinRect> resize_rect = std::nullopt, COLORREF background = RGB(0, 0, 0)) {
    if (!bitmap) {
        return false;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return false;
    }
    HBRUSH brush = CreateSolidBrush(background);
    FillRect(dc, &client, brush);
    DeleteObject(brush);

    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    if (resize_rect) {
        const auto segments = resize_segments_for_skin(
            WindowRect{0, 0, info.bmWidth, info.bmHeight},
            WindowRect{0, 0, client.right - client.left, client.bottom - client.top},
            skin_rect_to_window_rect(*resize_rect));
        for (const auto& segment : segments) {
            transparent_blit_rect(dc, source, segment.dest, segment.source);
        }
    } else {
        TransparentBlt(dc, 0, 0, client.right - client.left, client.bottom - client.top, source, 0, 0, info.bmWidth, info.bmHeight, skin_transparent_key());
    }
    SelectObject(source, old);
    DeleteDC(source);
    return true;
}
std::optional<SkinSize> bitmap_size(HBITMAP bitmap) {
    if (!bitmap) {
        return std::nullopt;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return std::nullopt;
    }
    return SkinSize{info.bmWidth, info.bmHeight};
}

void draw_bitmap_stretched(HDC dc, HBITMAP bitmap, const SkinRect& rect) {
    if (!bitmap) {
        return;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    TransparentBlt(dc, rect.left, rect.top, width, height, source, 0, 0, info.bmWidth, info.bmHeight, skin_transparent_key());
    SelectObject(source, old);
    DeleteDC(source);
}
void draw_bitmap_at(HDC dc, HBITMAP bitmap, const SkinRect& rect) {
    if (!bitmap) {
        return;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    TransparentBlt(dc, rect.left, rect.top, width, height, source, 0, 0, info.bmWidth, info.bmHeight, RGB(255, 0, 255));
    SelectObject(source, old);
    DeleteDC(source);
}


void draw_bitmap_native_at(HDC dc, HBITMAP bitmap, int x, int y) {
    if (!bitmap) {
        return;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    TransparentBlt(dc, x, y, info.bmWidth, info.bmHeight, source, 0, 0, info.bmWidth, info.bmHeight, RGB(255, 0, 255));
    SelectObject(source, old);
    DeleteDC(source);
}

void draw_bitmap_part_scaled(HDC dc, HBITMAP bitmap, const RECT& dest, const RECT& src) {
    if (!bitmap || dest.right <= dest.left || dest.bottom <= dest.top || src.right <= src.left || src.bottom <= src.top) {
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    TransparentBlt(dc, dest.left, dest.top, dest.right - dest.left, dest.bottom - dest.top,
        source, src.left, src.top, src.right - src.left, src.bottom - src.top, RGB(255, 0, 255));
    SelectObject(source, old);
    DeleteDC(source);
}

void draw_first_state_bitmap_at(HDC dc, HBITMAP bitmap, const SkinRect& rect, int state_count = 4) {
    BITMAP info{};
    if (!bitmap || GetObjectW(bitmap, sizeof(info), &info) == 0 || state_count <= 0) {
        return;
    }
    const int frame_width = std::max(1, static_cast<int>(info.bmWidth) / state_count);
    const RECT dest{rect.left, rect.top, rect.left + frame_width, rect.top + static_cast<int>(info.bmHeight)};
    const RECT src{0, 0, frame_width, static_cast<int>(info.bmHeight)};
    draw_bitmap_part_scaled(dc, bitmap, dest, src);
}

void draw_state_bitmap_at(HDC dc, HBITMAP bitmap, const SkinRect& rect, int frame_index, int state_count = 4) {
    BITMAP info{};
    if (!bitmap || GetObjectW(bitmap, sizeof(info), &info) == 0 || state_count <= 0) {
        return;
    }
    const int frame_width = std::max(1, static_cast<int>(info.bmWidth) / state_count);
    const int frame = std::clamp(frame_index, 0, state_count - 1);
    const RECT dest{rect.left, rect.top, rect.left + frame_width, rect.top + static_cast<int>(info.bmHeight)};
    const RECT src{frame * frame_width, 0, (frame + 1) * frame_width, static_cast<int>(info.bmHeight)};
    draw_bitmap_part_scaled(dc, bitmap, dest, src);
}

void draw_slider_control(HDC dc, const SkinControlDefinition& control, double fraction) {
    if (!control.fill_image.empty()) {
        const auto fill_bitmap = g_skin.player_controls.find(control.fill_image);
        if (fill_bitmap != g_skin.player_controls.end()) {
            draw_bitmap_stretched(dc, fill_bitmap->second, slider_fill_rect(control, fraction));
        }
    }
    if (!control.thumb_image.empty()) {
        const auto thumb_bitmap = g_skin.player_controls.find(control.thumb_image);
        if (thumb_bitmap != g_skin.player_controls.end()) {
            const auto size = bitmap_size(thumb_bitmap->second);
            if (size) {
                draw_bitmap_at(dc, thumb_bitmap->second, slider_thumb_rect(control, *size, fraction));
            }
        }
    }
}

bool panel_is_visible(PanelKind kind) {
    if (const auto* panel = find_panel(kind)) {
        return IsWindowVisible(panel->hwnd) != FALSE;
    }
    return false;
}

bool control_is_active_or_hovered(std::string_view name) {
    if (g_app.hovered_player_control == name) {
        return true;
    }
    if (name == "lyric") {
        return panel_is_visible(PanelKind::Lyrics);
    }
    if (name == "playlist") {
        return panel_is_visible(PanelKind::Playlist);
    }
    if (name == "equalizer") {
        return panel_is_visible(PanelKind::Equalizer);
    }
    return false;
}

int control_frame_index(std::string_view name) {
    return control_is_active_or_hovered(name) ? 1 : 0;
}

void draw_volume_slider_control(HDC dc, const SkinControlDefinition& control, double fraction) {
    if (!control.fill_image.empty()) {
        const auto fill_bitmap = g_skin.player_controls.find(control.fill_image);
        if (fill_bitmap != g_skin.player_controls.end()) {
            BITMAP info{};
            if (GetObjectW(fill_bitmap->second, sizeof(info), &info) != 0) {
                const int source_width = std::clamp(static_cast<int>(info.bmWidth * std::clamp(fraction, 0.0, 1.0)), 0, static_cast<int>(info.bmWidth));
                const int source_height = static_cast<int>(info.bmHeight);
                const int dest_top = control.position.top + std::max(0, (control.position.bottom - control.position.top - source_height) / 2);
                HDC source = CreateCompatibleDC(dc);
                HBITMAP old = static_cast<HBITMAP>(SelectObject(source, fill_bitmap->second));
                TransparentBlt(dc, control.position.left, dest_top, source_width, source_height, source, 0, 0, source_width, source_height, RGB(255, 0, 255));
                SelectObject(source, old);
                DeleteDC(source);
            }
        }
    }
    if (!control.thumb_image.empty()) {
        const auto thumb_bitmap = g_skin.player_controls.find(control.thumb_image);
        if (thumb_bitmap != g_skin.player_controls.end()) {
            const auto size = bitmap_size(thumb_bitmap->second);
            if (size) {
                draw_bitmap_at(dc, thumb_bitmap->second, slider_thumb_rect(control, *size, fraction));
            }
        }
    }
}
int control_frame_count(std::string_view name) {
    if (name == "prev" || name == "next" || name == "stop" || name == "play" || name == "pause"
        || name == "lyric" || name == "playlist" || name == "equalizer" || name == "minimize"
        || name == "minimode" || name == "exit" || name == "open" || name == "mute") {
        return 4;
    }
    return 1;
}

void draw_control_bitmap(HDC dc, HBITMAP bitmap, const SkinControlDefinition& control, std::string_view name) {
    if (!bitmap) {
        return;
    }
    BITMAP info{};
    if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
        return;
    }
    HDC source = CreateCompatibleDC(dc);
    HBITMAP old = static_cast<HBITMAP>(SelectObject(source, bitmap));
    const int frame_count = control_frame_count(name);
    const int source_width = std::max(1, static_cast<int>(info.bmWidth) / frame_count);
    const int source_height = static_cast<int>(info.bmHeight);
    const int source_x = std::clamp(control_frame_index(name), 0, frame_count - 1) * source_width;
    TransparentBlt(dc, control.position.left, control.position.top, source_width, source_height, source, source_x, 0, source_width, source_height, RGB(255, 0, 255));
    SelectObject(source, old);
    DeleteDC(source);
}


int led_glyph_index(wchar_t ch) {
    if (ch >= L'0' && ch <= L'9') {
        return static_cast<int>(ch - L'0');
    }
    if (ch == L':') {
        return 10;
    }
    if (ch == L'-') {
        return 11;
    }
    return -1;
}

bool draw_led_text(HDC dc, const SkinControlDefinition& control, std::wstring_view text) {
    if (control.image.empty()) {
        return false;
    }
    const auto bitmap_it = g_skin.player_controls.find(control.image);
    if (bitmap_it == g_skin.player_controls.end()) {
        return false;
    }
    BITMAP info{};
    if (GetObjectW(bitmap_it->second, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
        return false;
    }
    const int glyph_count = 12;
    const int glyph_width = info.bmWidth / glyph_count;
    const int glyph_height = info.bmHeight;
    if (glyph_width <= 0 || glyph_height <= 0) {
        return false;
    }

    HDC source = CreateCompatibleDC(dc);
    HBITMAP old_source = static_cast<HBITMAP>(SelectObject(source, bitmap_it->second));
    const int dest_height = control.position.bottom - control.position.top;
    const int total_width = glyph_width * static_cast<int>(text.size());
    int x = control.position.right - total_width;
    int y = control.position.top + std::max(0, (dest_height - glyph_height) / 2);
    if (x < control.position.left) {
        x = control.position.left;
    }

    for (wchar_t ch : text) {
        const int glyph_index = led_glyph_index(ch);
        if (glyph_index < 0) {
            x += glyph_width;
            continue;
        }
        TransparentBlt(dc, x, y, glyph_width, glyph_height, source, glyph_index * glyph_width, 0, glyph_width, glyph_height, RGB(255, 0, 255));
        x += glyph_width;
    }

    SelectObject(source, old_source);
    DeleteDC(source);
    return true;
}

void draw_control_icon(HDC dc, HICON icon, const SkinControlDefinition& control) {
    if (!icon) {
        return;
    }
    const int width = control.position.right - control.position.left;
    const int height = control.position.bottom - control.position.top;
    DrawIconEx(dc, control.position.left, control.position.top, icon, width, height, 0, nullptr, DI_NORMAL);
}

void draw_named_player_controls(HDC dc, std::span<const std::string_view> names) {
    if (!g_skin_definition) {
        return;
    }
    for (const auto name : names) {
        const auto control_it = g_skin_definition->player.controls.find(std::string(name));
        if (control_it == g_skin_definition->player.controls.end() || control_it->second.image.empty()) {
            continue;
        }
        const auto bitmap_it = g_skin.player_controls.find(control_it->second.image);
        if (bitmap_it != g_skin.player_controls.end()) {
            draw_control_bitmap(dc, bitmap_it->second, control_it->second, name);
            continue;
        }
        const auto icon_it = g_skin.player_icons.find(control_it->second.image);
        if (icon_it != g_skin.player_icons.end()) {
            draw_control_icon(dc, icon_it->second, control_it->second);
        }
    }
}
void draw_player_sliders(HDC dc) {
    if (!g_skin_definition) {
        return;
    }
    if (const auto progress = g_skin_definition->player.controls.find("progress"); progress != g_skin_definition->player.controls.end()) {
        const int length = audio_length_ms();
        const double fraction = length > 0 ? static_cast<double>(audio_position_ms()) / static_cast<double>(length) : 0.0;
        draw_slider_control(dc, progress->second, fraction);
    }
    if (const auto volume = g_skin_definition->player.controls.find("volume"); volume != g_skin_definition->player.controls.end()) {
        draw_volume_slider_control(dc, volume->second, static_cast<double>(g_audio.volume) / 100.0);
    }
}

void draw_player_control_bitmaps(HDC dc) {
    constexpr std::array<std::string_view, 12> button_order{
        "prev", "next", "stop", "play", "mute", "open", "lyric", "equalizer", "playlist",
        "minimize", "minimode", "exit"
    };
    draw_named_player_controls(dc, std::span<const std::string_view>(button_order.data(), button_order.size()));
    draw_player_sliders(dc);
}
void fill_rect(HDC dc, const RECT& rect, COLORREF c) {
    HBRUSH brush = CreateSolidBrush(c);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void draw_text_line(HDC dc, int x, int y, std::wstring_view text, COLORREF c, int height = 18) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rect{x, y, 2000, y + height};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

void draw_gradient_title(HDC dc, RECT rect, std::wstring_view title, bool active) {
    const int height = rect.bottom - rect.top;
    for (int y = 0; y < height; ++y) {
        const int t = height > 1 ? (y * 255 / (height - 1)) : 0;
        const BYTE r = static_cast<BYTE>(active ? (92 - t / 5) : (58 - t / 9));
        const BYTE g = static_cast<BYTE>(active ? (166 - t / 6) : (120 - t / 10));
        const BYTE b = static_cast<BYTE>(active ? (238 - t / 10) : (188 - t / 12));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(r, g, b));
        HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
        MoveToEx(dc, rect.left, rect.top + y, nullptr);
        LineTo(dc, rect.right, rect.top + y);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
    draw_text_line(dc, rect.left + 9, rect.top + 4, title, RGB(215, 245, 255), 18);
}

void draw_border(HDC dc, RECT rect, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_brush);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_title_buttons(HDC dc, int client_width) {
    RECT minimize{client_width - 44, 6, client_width - 28, 21};
    RECT close{client_width - 22, 6, client_width - 6, 21};
    fill_rect(dc, minimize, RGB(18, 72, 130));
    fill_rect(dc, close, RGB(18, 72, 130));
    draw_border(dc, minimize, RGB(126, 188, 255));
    draw_border(dc, close, RGB(126, 188, 255));
    draw_text_line(dc, minimize.left + 5, minimize.top - 1, L"_", RGB(215, 245, 255), 16);
    draw_text_line(dc, close.left + 4, close.top + 1, L"x", RGB(215, 245, 255), 16);
}

TitleButton title_button_from_lparam(HWND hwnd, LPARAM lparam) {
    RECT client{};
    GetClientRect(hwnd, &client);
    return title_button_at(client.right - client.left, kTitleHeight, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
}

void draw_main_shortcuts(HDC dc, int client_width, int client_height) {
    constexpr std::array<std::wstring_view, 4> labels{L"L", L"P", L"E", L"R"};
    const int start_x = client_width - 153;
    const int top = client_height - 28;
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        RECT button{start_x + i * 28, top, start_x + i * 28 + 22, top + 20};
        fill_rect(dc, button, RGB(18, 72, 130));
        draw_border(dc, button, RGB(126, 188, 255));
        draw_text_line(dc, button.left + 7, button.top + 2, labels[static_cast<std::size_t>(i)], RGB(215, 245, 255), 16);
    }
}

MainShortcut main_shortcut_from_lparam(HWND hwnd, LPARAM lparam) {
    RECT client{};
    GetClientRect(hwnd, &client);
    return main_shortcut_at(client.right - client.left, client.bottom - client.top, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
}

RECT to_rect(const SkinRect& rect) {
    return RECT{rect.left, rect.top, rect.right, rect.bottom};
}

void draw_text_in_rect(HDC dc, RECT rect, std::wstring_view text, COLORREF c, UINT align = DT_LEFT) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, align | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | DT_VCENTER);
    if (old_font) {
        SelectObject(dc, old_font);
    }
    if (font) {
        DeleteObject(font);
    }
}


std::wstring display_text_for_slot(int slot) {
    const auto* track = current_track();
    if (!track) {
        return L"";
    }
    const auto metadata = metadata_for_track(*track);
    switch (slot % 4) {
    case 0:
        return L"标题: " + (metadata.title.empty() ? track->title : metadata.title);
    case 1:
        if (!metadata.album.empty()) {
            return L"专辑: " + metadata.album;
        }
        if (!metadata.artist.empty()) {
            return L"艺人: " + metadata.artist;
        }
        return L"文件: " + track->path.filename().wstring();
    case 2:
        return L"格式: " + metadata.format;
    default: {
        const int length = display_length_ms();
        return L"长度: " + (length > 0 ? format_length_time_ms(length) : L"--:--");
    }
    }
}


std::array<int, kVisualBarCount> visual_bars_for_current_track(const SkinRect& visual_rect) {
    const int visual_height = std::max(1, visual_rect.bottom - visual_rect.top - 2);
    const bool has_cached_bars = std::any_of(g_visual_bars.begin(), g_visual_bars.end(), [](int value) { return value > 0; });
    if (!has_cached_bars) {
        for (std::size_t i = 0; i < g_visual_bars.size(); ++i) {
            const int envelope = std::max(3, visual_height - static_cast<int>(i / 9));
            g_visual_bars[i] = 2 + static_cast<int>((i * 5) % std::max(3, envelope / 2));
        }
    }

    const auto* track = current_track();
    if (!track || !g_audio.playing) {
        return g_visual_bars;
    }

    std::error_code error;
    const auto file_size = std::filesystem::file_size(track->path, error);
    const int length = audio_length_ms();
    const int position = audio_position_ms();
    const double fraction = length > 0 ? std::clamp(static_cast<double>(position) / static_cast<double>(length), 0.0, 1.0) : 0.0;
    std::ifstream file(track->path, std::ios::binary);
    if (!file || error || file_size == 0) {
        return g_visual_bars;
    }

    const auto offset = static_cast<std::uintmax_t>(static_cast<double>(file_size) * fraction);
    file.seekg(static_cast<std::streamoff>(std::min<std::uintmax_t>(offset, file_size - 1)), std::ios::beg);
    std::array<unsigned char, 384> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    const auto read_count = static_cast<int>(file.gcount());
    if (read_count <= 0) {
        return g_visual_bars;
    }

    for (int i = 0; i < static_cast<int>(g_visual_bars.size()); ++i) {
        int acc = 0;
        for (int j = 0; j < 5; ++j) {
            const int index = (i * 9 + j * 19 + position / 37) % read_count;
            acc += std::abs(static_cast<int>(bytes[static_cast<std::size_t>(index)]) - 128);
        }
        const int envelope = std::max(5, visual_height - 4 - i / 12);
        const int texture = (acc / 5 + i * 9 + position / 71) % 100;
        const int target = 2 + texture * envelope / 100;
        const int previous = g_visual_bars[static_cast<std::size_t>(i)];
        int next_height = previous;
        if (target > previous) {
            next_height = std::min(visual_height - 1, previous + 4);
        } else {
            next_height = std::max(2, previous - 2);
        }
        g_visual_bars[static_cast<std::size_t>(i)] = next_height;
        auto& peak = g_visual_peaks[static_cast<std::size_t>(i)];
        if (next_height >= peak) {
            peak = next_height;
        } else {
            peak = std::max(next_height, peak - ((position / 90 + i) % 3 == 0 ? 1 : 0));
        }
    }
    return g_visual_bars;
}

void draw_visual_wave_overlay(HDC dc, const SkinRect& visual_rect) {
    if (!g_audio.playing) {
        return;
    }
    const int width = visual_rect.right - visual_rect.left;
    const int height = visual_rect.bottom - visual_rect.top;
    if (width <= 0 || height <= 0) {
        return;
    }

    const int phase = static_cast<int>((GetTickCount64() / 32) % 64);
    for (int x = 0; x < width; x += 2) {
        const int local = x + phase;
        const int slope = local * std::max(1, height - 8) / std::max(1, width);
        const int wobble = std::abs(((local / 2) % 12) - 6) / 2;
        const int crest = visual_rect.top + 1 + slope + wobble;
        for (int tail = 0; tail < 5; ++tail) {
            const int y = crest + tail * 2;
            if (y < visual_rect.top || y >= visual_rect.bottom - 1) {
                continue;
            }
            const bool sparse = ((x / 2 + tail * 2 + phase) % (tail + 2)) <= 1;
            if (!sparse) {
                continue;
            }
            const BYTE shade = static_cast<BYTE>(std::clamp(168 - tail * 24 - x / 11, 72, 168));
            RECT dot{visual_rect.left + x, y, std::min(visual_rect.left + x + 2, visual_rect.right), y + 1};
            fill_rect(dc, dot, RGB(shade, shade, shade));
        }
    }
}

void draw_scrolling_info_text(HDC dc, RECT rect, COLORREF color_value) {
    constexpr ULONGLONG slot_ms = 5000;
    constexpr ULONGLONG transition_ms = 1200;
    const ULONGLONG now = GetTickCount64();
    const int slot = static_cast<int>((now / slot_ms) % 4);
    const ULONGLONG phase = now % slot_ms;
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    if (phase < slot_ms - transition_ms) {
        draw_text_in_rect(dc, rect, display_text_for_slot(slot), color_value);
    } else {
        const double raw_t = static_cast<double>(phase - (slot_ms - transition_ms)) / static_cast<double>(transition_ms);
        const double t = raw_t * raw_t * (3.0 - 2.0 * raw_t);
        const int dy = static_cast<int>((rect.bottom - rect.top) * t);
        RECT current_rect{rect.left, rect.top - dy, rect.right, rect.bottom - dy};
        RECT next_rect{rect.left, rect.bottom - dy, rect.right, rect.bottom + (rect.bottom - rect.top) - dy};
        draw_text_in_rect(dc, current_rect, display_text_for_slot(slot), color_value);
        draw_text_in_rect(dc, next_rect, display_text_for_slot(slot + 1), color_value);
    }
    RestoreDC(dc, saved);
}
void draw_tt07_player_overlay(HDC dc) {
    if (!g_skin_definition) {
        return;
    }

    constexpr std::array<std::string_view, 7> controls_before_play{
        "icon", "prev", "next", "stop", "open", "lyric", "playlist"
    };
    constexpr std::array<std::string_view, 4> controls_after_play{"equalizer", "minimize", "minimode", "exit"};
    draw_named_player_controls(dc, std::span<const std::string_view>(controls_before_play.data(), controls_before_play.size()));
    const std::array<std::string_view, 1> play_control{g_audio.playing ? std::string_view("pause") : std::string_view("play")};
    draw_named_player_controls(dc, std::span<const std::string_view>(play_control.data(), play_control.size()));
    draw_named_player_controls(dc, std::span<const std::string_view>(controls_after_play.data(), controls_after_play.size()));
    draw_player_sliders(dc);

    const auto& player_controls = g_skin_definition->player.controls;
    RECT info_rect{24, 12, 178, 27};
    if (const auto info = player_controls.find("info"); info != player_controls.end()) {
        info_rect = to_rect(info->second.position);
    }
    draw_scrolling_info_text(dc, info_rect, RGB(235, 235, 235));

    RECT led_rect{137, 23, 186, 43};
    if (const auto led = player_controls.find("led"); led != player_controls.end()) {
        led_rect = to_rect(led->second.position);
    }
    const std::wstring position_text = format_position_time_ms(audio_position_ms());
    if (const auto led = player_controls.find("led"); led == player_controls.end() || !draw_led_text(dc, led->second, position_text)) {
        draw_text_in_rect(dc, led_rect, position_text, RGB(235, 235, 235), DT_RIGHT);
    }

    SkinRect visual_rect{16, 57, 187, 88};
    if (const auto visual = player_controls.find("visual"); visual != player_controls.end()) {
        visual_rect = visual->second.position;
    }
    const auto bars = visual_bars_for_current_track(visual_rect);
    const int visual_width = visual_rect.right - visual_rect.left;
    const int visual_height = visual_rect.bottom - visual_rect.top;
    const int bottom = visual_rect.bottom - 2;
    const int step = 3;
    const int bar_width = 2;
    for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
        const int x = visual_rect.left + i * step;
        if (x >= visual_rect.left + visual_width) {
            break;
        }
        const int h = std::min(visual_height - 2, bars[static_cast<std::size_t>(i)]);
        RECT bar{x, bottom - h, std::min(x + bar_width, visual_rect.left + visual_width), bottom};
        fill_rect(dc, bar, RGB(118, 118, 118));
        const int peak_height = std::clamp(g_visual_peaks[static_cast<std::size_t>(i)], h, visual_height - 2);
        const int peak_y = bottom - peak_height;
        RECT peak{x, peak_y, std::min(x + bar_width, visual_rect.left + visual_width), peak_y + 1};
        fill_rect(dc, peak, RGB(178, 178, 178));
    }

    RECT status_rect{124, 94, 186, 112};
    if (const auto status = player_controls.find("status"); status != player_controls.end()) {
        status_rect = to_rect(status->second.position);
    }
    draw_text_in_rect(dc, status_rect, g_audio.playing ? L"状态: 播放" : L"状态: 暂停", RGB(235, 235, 235), DT_RIGHT);
}


void draw_tahoma_text_line(HDC dc, int x, int y, std::wstring_view text, COLORREF c, int font_height = 12, int box_width = 220) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    RECT rect{x, y, x + box_width, y + font_height + 4};
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old_font) {
        SelectObject(dc, old_font);
    }
    if (font) {
        DeleteObject(font);
    }
}

int measure_tahoma_text_width(HDC dc, std::wstring_view text, int font_height = 13) {
    HFONT font = CreateFontW(font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    SIZE size{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &size);
    if (old_font) {
        SelectObject(dc, old_font);
    }
    if (font) {
        DeleteObject(font);
    }
    return size.cx;
}

void draw_lyric_text_line(HDC dc, int x, int y, std::wstring_view text, bool current, double phase, int box_width) {
    constexpr COLORREF kLyricDim = RGB(90, 90, 90);
    constexpr COLORREF kLyricBright = RGB(5, 211, 255);
    draw_tahoma_text_line(dc, x, y, text, kLyricDim, 13, box_width);
    if (!current) {
        return;
    }
    const int text_width = std::min(box_width, std::max(1, measure_tahoma_text_width(dc, text, 13) + 4));
    const int fill_width = std::clamp(static_cast<int>(phase * static_cast<double>(text_width)), 1, text_width);
    const int saved = SaveDC(dc);
    IntersectClipRect(dc, x, y, x + fill_width, y + 15);
    draw_tahoma_text_line(dc, x, y, text, kLyricBright, 13, box_width);
    RestoreDC(dc, saved);
}


void draw_thin_vertical_line(HDC dc, int x, int top, int bottom, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    MoveToEx(dc, x, top, nullptr);
    LineTo(dc, x, bottom);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void draw_rect_outline(HDC dc, const RECT& rect, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH old_brush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    HPEN old_pen = static_cast<HPEN>(SelectObject(dc, pen));
    Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
}

SkinRect translated_rect(SkinRect rect, int dx, int dy = 0) {
    rect.left += dx;
    rect.right += dx;
    rect.top += dy;
    rect.bottom += dy;
    return rect;
}

int eq_band_step() {
    return 13;
}

SkinRect eq_band_rect(int band) {
    SkinRect rect{168, 33, 173, 93};
    if (g_skin_definition) {
        if (const auto eqfactor = g_skin_definition->equalizer.controls.find("eqfactor"); eqfactor != g_skin_definition->equalizer.controls.end()) {
            rect = eqfactor->second.position;
        }
    }
    return translated_rect(rect, std::clamp(band, 0, kEqualizerBandCount - 1) * eq_band_step());
}

void draw_eq_vertical_slider(HDC dc, const SkinRect& rect, int value) {
    const int thumb_height = 5;
    const int y = equalizer_value_to_slider_y(value, rect.top, rect.bottom, thumb_height);
    const int fill_top = std::clamp(y + thumb_height / 2, rect.top, rect.bottom);
    const int fill_height = std::max(0, rect.bottom - fill_top);
    const int fill_left = rect.left + std::max(0, (rect.right - rect.left - 3) / 2);
    if (fill_height > 0) {
        RECT dest{fill_left, fill_top, fill_left + 3, rect.bottom};
        if (const auto fill = g_skin.panel_controls.find("eqfactor_full.bmp"); fill != g_skin.panel_controls.end()) {
            RECT src{0, 58 - fill_height, 3, 58};
            draw_bitmap_part_scaled(dc, fill->second, dest, src);
        } else {
            fill_rect(dc, dest, RGB(0, 205, 236));
        }
    }
}

void draw_eq_horizontal_slider(HDC dc, const SkinRect& rect, int fill_width) {
    const int width = std::max(0, rect.right - rect.left);
    const int clipped_width = std::clamp(fill_width, 0, width);
    if (clipped_width > 0) {
        RECT dest{rect.left, rect.top, rect.left + clipped_width, rect.top + 3};
        if (const auto fill = g_skin.panel_controls.find("eqfactor_full2.bmp"); fill != g_skin.panel_controls.end()) {
            RECT src{0, 0, std::min(clipped_width, 58), 3};
            draw_bitmap_part_scaled(dc, fill->second, dest, src);
        } else {
            fill_rect(dc, dest, RGB(0, 205, 236));
        }
    }
}
void draw_tt07_equalizer_content(HDC dc) {
    if (!g_skin_definition) {
        return;
    }
    constexpr std::array<std::string_view, 3> names{"enabled", "reset", "profile"};
    for (const auto name : names) {
        const auto control = g_skin_definition->equalizer.controls.find(std::string(name));
        if (control == g_skin_definition->equalizer.controls.end() || control->second.image.empty()) {
            continue;
        }
        const auto bitmap = g_skin.panel_controls.find(control->second.image);
        if (bitmap != g_skin.panel_controls.end()) {
            int frame = 0;
            if (name == "enabled") {
                frame = g_equalizer.enabled ? 2 : 0;
            }
            draw_state_bitmap_at(dc, bitmap->second, control->second.position, frame);
        }
    }

    if (const auto preamp = g_skin_definition->equalizer.controls.find("preamp"); preamp != g_skin_definition->equalizer.controls.end()) {
        draw_eq_vertical_slider(dc, preamp->second.position, g_equalizer.preamp);
    }
    for (int i = 0; i < kEqualizerBandCount; ++i) {
        draw_eq_vertical_slider(dc, eq_band_rect(i), g_equalizer.bands[static_cast<std::size_t>(i)]);
    }
    if (const auto balance = g_skin_definition->equalizer.controls.find("balance"); balance != g_skin_definition->equalizer.controls.end()) {
        draw_eq_horizontal_slider(dc, balance->second.position,
                                  balance_slider_fill_width(g_equalizer.balance,
                                                            balance->second.position.right - balance->second.position.left));
    }
    if (const auto surround = g_skin_definition->equalizer.controls.find("surround"); surround != g_skin_definition->equalizer.controls.end()) {
        draw_eq_horizontal_slider(dc, surround->second.position,
                                  surround_slider_fill_width(g_equalizer.surround,
                                                             surround->second.position.right - surround->second.position.left));
    }
}

void draw_playlist_scrollbar(HDC dc, const RECT& client, int visible_rows) {
    (void)visible_rows;
    const auto geometry = playlist_scrollbar_for_client(client);
    if (!geometry.visible) {
        return;
    }
    RECT up_button{geometry.up_button.left, geometry.up_button.top, geometry.up_button.right, geometry.up_button.bottom};
    RECT down_button{geometry.down_button.left, geometry.down_button.top, geometry.down_button.right, geometry.down_button.bottom};

    if (const auto button = g_skin.panel_controls.find("scrollbar_button.bmp"); button != g_skin.panel_controls.end()) {
        draw_bitmap_part_scaled(dc, button->second, up_button, RECT{0, 0, 8, 7});
        draw_bitmap_part_scaled(dc, button->second, down_button, RECT{0, 7, 8, 14});
    } else {
        fill_rect(dc, up_button, RGB(10, 10, 10));
        fill_rect(dc, down_button, RGB(10, 10, 10));
    }

    RECT track{geometry.track.left, geometry.track.top, geometry.track.right, geometry.track.bottom};
    if (const auto bar = g_skin.panel_controls.find("scrollbar_bar.bmp"); bar != g_skin.panel_controls.end()) {
        draw_bitmap_part_scaled(dc, bar->second, track, RECT{0, 0, 8, 39});
    } else {
        fill_rect(dc, track, RGB(8, 8, 8));
        draw_thin_vertical_line(dc, track.left, track.top, track.bottom, RGB(79, 79, 79));
        draw_thin_vertical_line(dc, track.right - 1, track.top, track.bottom, RGB(35, 35, 35));
    }

    RECT thumb{geometry.thumb.left, geometry.thumb.top, geometry.thumb.right, geometry.thumb.bottom};
    if (const auto thumb_bitmap = g_skin.panel_controls.find("scrollbar_thumb.bmp"); thumb_bitmap != g_skin.panel_controls.end()) {
        draw_bitmap_part_scaled(dc, thumb_bitmap->second, thumb, RECT{0, 0, 8, 36});
    } else {
        fill_rect(dc, thumb, RGB(91, 91, 91));
        draw_thin_vertical_line(dc, thumb.left, thumb.top, thumb.bottom, RGB(150, 150, 150));
        draw_thin_vertical_line(dc, thumb.right - 1, thumb.top, thumb.bottom, RGB(43, 43, 43));
    }
}

void draw_tt07_playlist_content(HDC dc, const RECT& client) {
    if (g_skin_definition) {
        if (const auto toolbar = g_skin_definition->playlist.controls.find("toolbar"); toolbar != g_skin_definition->playlist.controls.end()) {
            if (const auto bitmap = g_skin.panel_controls.find(toolbar->second.image); bitmap != g_skin.panel_controls.end()) {
                draw_bitmap_native_at(dc, bitmap->second, toolbar->second.position.left, toolbar->second.position.top);
            }
        }
    }

    const int visible_rows = playlist_visible_rows(client);
    clamp_playlist_scroll(visible_rows);
    clamp_playlist_divider(client);
    const int list_right = std::max(g_playlist_divider_x + 72, static_cast<int>(client.right) - 17);
    const int list_name_width = std::max(28, g_playlist_divider_x - 12);
    const int list_name_rows = std::max(1, playlist_visible_rows(client));
    const auto& playlist_lists = g_playlist_library.lists();
    const int drag_source = g_playlist_list_dragging ? g_playlist_list_drag_index : -1;
    const int drag_target = g_playlist_list_dragging ? g_playlist_list_drag_target_index : -1;
    const auto drag_feedback = g_playlist_list_dragging
        ? playlist_list_drag_feedback_at(drag_source, g_playlist_list_drag_mouse_x, g_playlist_list_drag_mouse_y, static_cast<int>(client.bottom), g_playlist_divider_x, playlist_lists.size())
        : std::optional<PlaylistListDragFeedback>{};
    for (int i = 0; i < list_name_rows && i < static_cast<int>(playlist_lists.size()); ++i) {
        const int y = 57 + i * 16;
        const int row_top = y - 1;
        const int row_bottom = y + 15;
        const bool active = static_cast<std::size_t>(i) == g_playlist_library.active_index();
        const bool drag_source_row = i == drag_source;
        const bool drag_target_row = i == drag_target && i != drag_source;
        RECT row_rect{4, row_top, std::max(24, g_playlist_divider_x - 4), row_bottom};
        if (active) {
            fill_rect(dc, row_rect, RGB(19, 30, 34));
        }
        if (drag_target_row) {
            fill_rect(dc, row_rect, RGB(22, 53, 61));
        }
        if (drag_source_row) {
            fill_rect(dc, row_rect, RGB(45, 39, 30));
        }
        COLORREF row_color = RGB(86, 86, 86);
        if (active) {
            row_color = RGB(5, 211, 255);
        }
        if (drag_target_row) {
            row_color = RGB(5, 211, 255);
        }
        if (drag_source_row) {
            row_color = RGB(232, 205, 143);
        }
        draw_tahoma_text_line(dc, 8, y, playlist_lists[static_cast<std::size_t>(i)].name, row_color, 13, list_name_width);
    }
    if (drag_feedback && drag_target >= 0 && drag_target < static_cast<int>(playlist_lists.size()) && drag_target != drag_source) {
        RECT insert_line{4, drag_feedback->insert_y - 1, std::max(24, g_playlist_divider_x - 4), drag_feedback->insert_y + 1};
        fill_rect(dc, insert_line, RGB(5, 211, 255));
        POINT marker[3] = {{std::max(4, g_playlist_divider_x - 9), drag_feedback->insert_y - 4}, {std::max(4, g_playlist_divider_x - 9), drag_feedback->insert_y + 4}, {std::max(4, g_playlist_divider_x - 4), drag_feedback->insert_y}};
        HBRUSH brush = CreateSolidBrush(RGB(5, 211, 255));
        HBRUSH old_brush = brush ? static_cast<HBRUSH>(SelectObject(dc, brush)) : nullptr;
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(5, 211, 255));
        HPEN old_pen = pen ? static_cast<HPEN>(SelectObject(dc, pen)) : nullptr;
        Polygon(dc, marker, 3);
        if (old_pen) {
            SelectObject(dc, old_pen);
        }
        if (pen) {
            DeleteObject(pen);
        }
        if (old_brush) {
            SelectObject(dc, old_brush);
        }
        if (brush) {
            DeleteObject(brush);
        }
    }
    if (drag_feedback && drag_source >= 0 && drag_source < static_cast<int>(playlist_lists.size())) {
        RECT ghost_rect{8, drag_feedback->ghost_top, std::max(30, g_playlist_divider_x - 4), drag_feedback->ghost_bottom};
        const auto& ghost_name = playlist_lists[static_cast<std::size_t>(drag_source)].name;
        const int ghost_width = std::max<int>(16, static_cast<int>(ghost_rect.right - ghost_rect.left - 6));
        draw_tahoma_text_line(dc, ghost_rect.left + 4, ghost_rect.top + 2, ghost_name, RGB(24, 18, 10), 13, ghost_width);
        draw_tahoma_text_line(dc, ghost_rect.left + 2, ghost_rect.top, ghost_name, RGB(24, 18, 10), 13, ghost_width);
        draw_tahoma_text_line(dc, ghost_rect.left + 3, ghost_rect.top + 1, ghost_name, RGB(248, 224, 155), 13, ghost_width);
    }
    draw_thin_vertical_line(dc, g_playlist_divider_x, 54, std::max(54, static_cast<int>(client.bottom) - 12), RGB(66, 66, 66));
    const int rows = std::min<int>(visible_rows, static_cast<int>(g_playlist.size()) - g_playlist_scroll);
    const int text_left = g_playlist_divider_x + 16;
    const int arrow_left = g_playlist_divider_x + 6;
    const int duration_width = 34;
    const int duration_left = std::max(text_left + 50, list_right - duration_width);
    const int text_width = std::max(80, duration_left - text_left - 4);
    const int selected_right = std::max(text_left + 40, list_right);
    for (int row_index = 0; row_index < rows; ++row_index) {
        const int track_index = g_playlist_scroll + row_index;
        const auto& track = g_playlist[static_cast<std::size_t>(track_index)];
        const std::wstring row = std::to_wstring(track_index + 1) + L"." + track.title;
        const bool selected = track_index == g_audio.current_index;
        const int y = 56 + row_index * 16;
        if (selected) {
            RECT selected_rect{text_left - 12, y, selected_right, y + 15};
            fill_rect(dc, selected_rect, RGB(19, 30, 34));
            draw_tahoma_text_line(dc, arrow_left, y, L"›", RGB(5, 211, 255), 13, 10);
        }
        draw_tahoma_text_line(dc, text_left, y + 1, row, selected ? RGB(5, 211, 255) : RGB(86, 86, 86), 13, text_width);
        const auto duration = playlist_duration_text(track.duration_ms);
        if (!duration.empty()) {
            RECT duration_rect{duration_left, y + 1, duration_left + duration_width, y + 16};
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, selected ? RGB(5, 211, 255) : RGB(86, 86, 86));
            HFONT font = CreateFontW(13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                                     DEFAULT_PITCH | FF_DONTCARE, L"Tahoma");
            HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
            DrawTextW(dc, duration.data(), static_cast<int>(duration.size()), &duration_rect,
                      DT_RIGHT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (old_font) {
                SelectObject(dc, old_font);
            }
            if (font) {
                DeleteObject(font);
            }
        }
    }
    draw_playlist_scrollbar(dc, client, visible_rows);
}


std::wstring trim_lyric_text(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c); });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t c) { return std::iswspace(c); }).base();
    if (first >= last) {
        return L"";
    }
    return std::wstring(first, last);
}

std::optional<int> parse_lrc_timestamp(std::wstring_view tag) {
    const auto colon = tag.find(L':');
    if (colon == std::wstring_view::npos || colon == 0) {
        return std::nullopt;
    }
    int minutes = 0;
    for (std::size_t i = 0; i < colon; ++i) {
        if (!std::iswdigit(tag[i])) {
            return std::nullopt;
        }
        minutes = minutes * 10 + static_cast<int>(tag[i] - L'0');
    }
    int seconds = 0;
    std::size_t i = colon + 1;
    for (int digit = 0; digit < 2 && i < tag.size(); ++digit, ++i) {
        if (!std::iswdigit(tag[i])) {
            return std::nullopt;
        }
        seconds = seconds * 10 + static_cast<int>(tag[i] - L'0');
    }
    int centiseconds = 0;
    if (i < tag.size() && (tag[i] == L'.' || tag[i] == L':')) {
        ++i;
        int scale = 100;
        while (i < tag.size() && std::iswdigit(tag[i]) && scale > 0) {
            centiseconds += static_cast<int>(tag[i] - L'0') * scale;
            scale /= 10;
            ++i;
        }
    }
    return (minutes * 60 + seconds) * 1000 + centiseconds;
}

std::wstring read_text_file_guess_utf8(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return L"";
    }
    std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF && static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }
    auto wide = utf8_to_wide(bytes);
    if (!wide.empty()) {
        return wide;
    }
    return latin1_to_wide(bytes);
}

std::vector<LyricLine> parse_lrc_file(const std::filesystem::path& path) {
    std::vector<LyricLine> lines;
    const auto text = read_text_file_guess_utf8(path);
    std::wistringstream stream(text);
    std::wstring line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        std::vector<int> times;
        std::size_t offset = 0;
        while (offset < line.size() && line[offset] == L'[') {
            const auto close = line.find(L']', offset + 1);
            if (close == std::wstring::npos) {
                break;
            }
            if (const auto time = parse_lrc_timestamp(std::wstring_view(line).substr(offset + 1, close - offset - 1))) {
                times.push_back(*time);
            }
            offset = close + 1;
        }
        auto lyric = trim_lyric_text(line.substr(offset));
        if (lyric.empty()) {
            continue;
        }
        for (int time : times) {
            lines.push_back(LyricLine{time, lyric});
        }
    }
    std::sort(lines.begin(), lines.end(), [](const LyricLine& a, const LyricLine& b) {
        return a.time_ms < b.time_ms;
    });
    return lines;
}

std::vector<LyricLine> lyrics_for_track(const PlaylistTrack& track) {
    const auto key = track.path.wstring();
    if (const auto cached = g_lyric_cache.find(key); cached != g_lyric_cache.end()) {
        return cached->second;
    }
    auto lrc_path = track.path;
    lrc_path.replace_extension(L".lrc");
    auto lines = std::filesystem::exists(lrc_path) ? parse_lrc_file(lrc_path) : std::vector<LyricLine>{};
    g_lyric_cache.emplace(key, lines);
    return lines;
}

std::size_t lyric_start_index_for_position(const std::vector<LyricLine>& lines, int position_ms) {
    if (lines.empty()) {
        return 0;
    }
    auto it = std::upper_bound(lines.begin(), lines.end(), position_ms, [](int value, const LyricLine& line) {
        return value < line.time_ms;
    });
    if (it == lines.begin()) {
        return 0;
    }
    const auto active = static_cast<std::size_t>(std::distance(lines.begin(), it - 1));
    return active > 1 ? active - 1 : 0;
}
void draw_tt07_lyric_content(HDC dc, const RECT& client) {
    SkinRect lyric_rect{5, 32, 307, 194};
    if (g_skin_definition) {
        if (const auto lyric = g_skin_definition->lyrics.controls.find("lyric"); lyric != g_skin_definition->lyrics.controls.end()) {
            lyric_rect = lyric->second.position;
        }
    }
    lyric_rect = lyric_content_rect(lyric_rect, static_cast<int>(client.right), static_cast<int>(client.bottom));

    const auto* track = current_track();
    const int row_height = 13;
    const int text_width = std::max(40, lyric_rect.right - lyric_rect.left);
    if (!track) {
        return;
    }

    const RECT clip_rect{lyric_rect.left, lyric_rect.top, lyric_rect.right, lyric_rect.bottom};
    const int saved_dc = SaveDC(dc);
    IntersectClipRect(dc, clip_rect.left, clip_rect.top, clip_rect.right, clip_rect.bottom);

    const auto lines = lyrics_for_track(*track);
    if (lines.empty()) {
        draw_tahoma_text_line(dc, lyric_rect.left, lyric_rect.bottom - row_height - 2, track->title, RGB(88, 88, 88), 13, text_width);
        RestoreDC(dc, saved_dc);
        return;
    }

    const int position = audio_position_ms();
    const auto upper = std::upper_bound(lines.begin(), lines.end(), position, [](int value, const LyricLine& line) {
        return value < line.time_ms;
    });
    const bool has_active = upper != lines.begin();
    const std::size_t active = has_active ? static_cast<std::size_t>(std::distance(lines.begin(), upper - 1)) : 0;
    const int next_time = active + 1 < lines.size() ? lines[active + 1].time_ms : lines[active].time_ms + 4000;
    const int span = std::max(1, next_time - lines[active].time_ms);
    const double phase = has_active ? std::clamp(static_cast<double>(position - lines[active].time_ms) / static_cast<double>(span), 0.0, 1.0) : 0.0;
    const int active_row_from_top = std::clamp((lyric_rect.bottom - lyric_rect.top) / row_height - 5, 2, 7);
    const double scroll_rows = has_active ? static_cast<double>(active) + phase - static_cast<double>(active_row_from_top) : -static_cast<double>(active_row_from_top);
    const int origin_y = lyric_rect.top + 2;
    const int scroll_px = static_cast<int>(scroll_rows * row_height);

    for (std::size_t index = 0; index < lines.size(); ++index) {
        const int y = origin_y + static_cast<int>(index) * row_height - scroll_px;
        if (y > lyric_rect.bottom || y + row_height < lyric_rect.top) {
            continue;
        }
        const bool current = has_active && index == active;
        draw_lyric_text_line(dc, lyric_rect.left, y, lines[index].text, current, current ? phase : 0.0, text_width);
    }
    RestoreDC(dc, saved_dc);
}

LRESULT hit_test_border(HWND hwnd, LPARAM lparam) {
    POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    const int grip = 7;
    const bool left = pt.x >= wr.left && pt.x < wr.left + grip;
    const bool right = pt.x < wr.right && pt.x >= wr.right - grip;
    const bool top = pt.y >= wr.top && pt.y < wr.top + grip;
    const bool bottom = pt.y < wr.bottom && pt.y >= wr.bottom - grip;

    if (window_can_resize(hwnd)) {
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    POINT local = pt;
    ScreenToClient(hwnd, &local);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (hwnd == g_app.main_window && g_skin_definition &&
        hit_test_control(g_skin_definition->player, local.x, local.y)) {
        return HTCLIENT;
    }
    if (title_button_at(client.right - client.left, kTitleHeight, local.x, local.y) != TitleButton::None) {
        return HTCLIENT;
    }
    if (local.y >= 0 && local.y < kTitleHeight) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

WindowRect to_window_rect(const RECT& rect) {
    return WindowRect{rect.left, rect.top, rect.right, rect.bottom};
}

RECT to_rect(const WindowRect& rect) {
    return RECT{rect.left, rect.top, rect.right, rect.bottom};
}

bool spans_overlap(int a1, int a2, int b1, int b2) {
    return std::max(a1, b1) < std::min(a2, b2);
}

bool windows_touch(const WindowRect& a, const WindowRect& b) {
    const bool vertical_touch = (a.right == b.left || a.left == b.right) && spans_overlap(a.top, a.bottom, b.top, b.bottom);
    const bool horizontal_touch = (a.bottom == b.top || a.top == b.bottom) && spans_overlap(a.left, a.right, b.left, b.right);
    return vertical_touch || horizontal_touch;
}

bool is_attached_descendant(HWND candidate, HWND source) {
    for (const auto& panel : g_app.panels) {
        if (panel.hwnd == candidate) {
            HWND parent = panel.attached_to;
            while (parent) {
                if (parent == source) {
                    return true;
                }
                const auto* parent_panel = find_panel(parent);
                parent = parent_panel ? parent_panel->attached_to : nullptr;
            }
            return false;
        }
    }
    return false;
}

POINT clamp_panel_position_to_work_area(HWND hwnd, int x, int y) {
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return POINT{x, y};
    }
    const int min_x = info.rcWork.left - width + 48;
    const int max_x = info.rcWork.right - 48;
    const int min_y = info.rcWork.top - height + 32;
    const int max_y = info.rcWork.bottom - 32;
    return POINT{std::clamp(x, min_x, max_x), std::clamp(y, min_y, max_y)};
}
void set_panel_position(PanelWindow& panel, int x, int y) {
    const POINT target = clamp_panel_position_to_work_area(panel.hwnd, x, y);
    SetWindowPos(panel.hwnd, nullptr, target.x, target.y, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    GetWindowRect(panel.hwnd, &panel.last_rect);
}

std::uintptr_t window_id(HWND hwnd) {
    return reinterpret_cast<std::uintptr_t>(hwnd);
}

std::vector<DockNode> current_dock_nodes() {
    std::vector<DockNode> nodes;
    if (g_app.main_window) {
        nodes.push_back(DockNode{window_id(g_app.main_window), 0});
    }
    for (const auto& panel : g_app.panels) {
        if (panel.hwnd) {
            nodes.push_back(DockNode{window_id(panel.hwnd), window_id(panel.attached_to)});
        }
    }
    return nodes;
}

bool dock_group_contains(const std::vector<std::uintptr_t>& group, HWND hwnd) {
    return std::find(group.begin(), group.end(), window_id(hwnd)) != group.end();
}

void move_main_window_by(int dx, int dy) {
    RECT rect{};
    GetWindowRect(g_app.main_window, &rect);
    SetWindowPos(g_app.main_window, nullptr, rect.left + dx, rect.top + dy, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);
    GetWindowRect(g_app.main_window, &g_app.last_main_rect);
}

void move_dock_followers(HWND source, int dx, int dy) {
    if (dx == 0 && dy == 0) {
        return;
    }
    const auto followers = dock_followers_for_drag(window_id(source), current_dock_nodes());
    if (g_app.main_window && g_app.main_window != source && dock_group_contains(followers, g_app.main_window)) {
        move_main_window_by(dx, dy);
    }
    for (auto& panel : g_app.panels) {
        if (panel.hwnd && panel.hwnd != source && dock_group_contains(followers, panel.hwnd)) {
            RECT rect{};
            GetWindowRect(panel.hwnd, &rect);
            set_panel_position(panel, rect.left + dx, rect.top + dy);
        }
    }
}

void snap_panel_if_close(PanelWindow& panel) {
    if (g_app.arranging || !g_app.main_window || !panel.hwnd) {
        return;
    }

    RECT panel_rect_raw{};
    GetWindowRect(panel.hwnd, &panel_rect_raw);
    const WindowRect panel_rect = to_window_rect(panel_rect_raw);

    struct Candidate {
        HWND hwnd{};
        WindowRect rect{};
    };
    std::vector<Candidate> candidates;
    candidates.reserve(g_app.panels.size() + 1);
    for (const auto& other : g_app.panels) {
        if (other.hwnd && other.hwnd != panel.hwnd && IsWindowVisible(other.hwnd) && !is_attached_descendant(other.hwnd, panel.hwnd)) {
            RECT rect{};
            GetWindowRect(other.hwnd, &rect);
            candidates.push_back(Candidate{other.hwnd, to_window_rect(rect)});
        }
    }
    RECT main_rect{};
    GetWindowRect(g_app.main_window, &main_rect);
    candidates.push_back(Candidate{g_app.main_window, to_window_rect(main_rect)});

    std::vector<WindowRect> neighbor_rects;
    neighbor_rects.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        neighbor_rects.push_back(candidate.rect);
    }

    const auto snapped = snap_rect_to_neighbors(panel_rect, neighbor_rects, kSnapDistance);
    if (!snapped.attached) {
        panel.attached_to = nullptr;
        panel.last_rect = panel_rect_raw;
        return;
    }

    HWND target = g_app.main_window;
    for (const auto& candidate : candidates) {
        if (windows_touch(snapped.rect, candidate.rect)) {
            target = candidate.hwnd;
            break;
        }
    }
    panel.attached_to = target;

    if (snapped.rect.left != panel_rect.left || snapped.rect.top != panel_rect.top) {
        g_app.arranging = true;
        set_panel_position(panel, snapped.rect.left, snapped.rect.top);
        g_app.arranging = false;
    } else {
        panel.last_rect = to_rect(snapped.rect);
    }
}

void reset_panel_layout() {
    if (!g_app.main_window) {
        return;
    }
    RECT main_rect{};
    GetWindowRect(g_app.main_window, &main_rect);
    g_app.arranging = true;
    for (auto& panel : g_app.panels) {
        const auto size = panel_window_size(panel.kind);
        int x = main_rect.right;
        int y = main_rect.top;
        switch (panel.kind) {
        case PanelKind::Lyrics:
            x = main_rect.right;
            y = main_rect.top;
            break;
        case PanelKind::Playlist:
            x = main_rect.left;
            y = main_rect.bottom;
            break;
        case PanelKind::Equalizer:
            x = main_rect.right;
            y = main_rect.bottom;
            break;
        }
        if (const auto position = panel_window_position(panel.kind)) {
            x = main_rect.left + position->left;
            y = main_rect.top + position->top;
        }
        panel.attached_to = g_app.main_window;
        SetWindowPos(panel.hwnd, nullptr, x, y, size.width, size.height, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
        GetWindowRect(panel.hwnd, &panel.last_rect);
    }
    g_app.arranging = false;
}

bool handle_main_shortcut(MainShortcut shortcut) {
    switch (shortcut) {
    case MainShortcut::Lyrics:
        toggle_panel(PanelKind::Lyrics);
        return true;
    case MainShortcut::Playlist:
        toggle_panel(PanelKind::Playlist);
        return true;
    case MainShortcut::Equalizer:
        toggle_panel(PanelKind::Equalizer);
        return true;
    case MainShortcut::ResetLayout:
        reset_panel_layout();
        return true;
    case MainShortcut::None:
        return false;
    }
    return false;
}

void paint_main(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem, bitmap));

    const bool has_skin_bitmap = draw_bitmap_to_client(mem, g_skin.player, client, g_skin_definition ? g_skin_definition->player.resize_rect : std::nullopt);
    const auto paint_mode = paint_mode_for_skin_bitmap(has_skin_bitmap);
    if (paint_mode == SkinPaintMode::FallbackChrome) {
        fill_rect(mem, client, RGB(0, 8, 12));
        RECT title{0, 0, client.right, kTitleHeight};
        draw_gradient_title(mem, title, L"千千静听", GetActiveWindow() == hwnd);
        draw_border(mem, client, RGB(88, 164, 238));
        draw_title_buttons(mem, client.right - client.left);

        draw_text_line(mem, 12, 35, L"格式: FLAC 44kHz", RGB(0, 230, 220));
        draw_text_line(mem, 12, 54, L"状态: 停止", RGB(0, 230, 220));
        draw_text_line(mem, 12, 73, L"曲目: Beyond - 真的爱你", RGB(0, 230, 220));

        RECT display{166, 33, client.right - 10, 91};
        draw_border(mem, display, RGB(88, 164, 238));
        draw_text_line(mem, display.left + 26, display.top + 16, L"TT", RGB(0, 230, 220), 26);
        draw_text_line(mem, display.left + 26, display.top + 37, L"Player", RGB(0, 120, 130));

        const std::array<int, 8> bars{20, 31, 16, 42, 27, 36, 52, 24};
        for (int i = 0; i < static_cast<int>(bars.size()); ++i) {
            RECT bar{92 + i * 8, 86 - bars[i], 95 + i * 8, 86};
            fill_rect(mem, bar, RGB(0, 220, 210));
        }

        RECT progress{12, client.bottom - 24, 146, client.bottom - 15};
        draw_border(mem, progress, RGB(126, 188, 255));
        RECT fill{13, client.bottom - 23, 92, client.bottom - 16};
        fill_rect(mem, fill, RGB(118, 184, 238));
    }
    if (paint_mode == SkinPaintMode::SkinBitmap) {
        draw_tt07_player_overlay(mem);
    }
    if (should_overlay_player_controls(paint_mode)) {
        draw_player_control_bitmaps(mem);
    }
    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void paint_panel(HWND hwnd, PanelKind kind, std::wstring_view title) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem, bitmap));

    const bool has_skin_bitmap = draw_bitmap_to_client(mem, skin_bitmap_for_panel(kind), client, panel_resize_rect(kind));
    const auto paint_mode = paint_mode_for_skin_bitmap(has_skin_bitmap);
    if (paint_mode == SkinPaintMode::FallbackChrome) {
        fill_rect(mem, client, RGB(0, 8, 12));
        RECT title_rect{0, 0, client.right, kTitleHeight};
        draw_gradient_title(mem, title_rect, title, GetActiveWindow() == hwnd);
        draw_border(mem, client, RGB(88, 164, 238));
        draw_title_buttons(mem, client.right - client.left);
    }
    if (!should_draw_fallback_panel_content(paint_mode)) {
        if (kind == PanelKind::Playlist) {
            draw_tt07_playlist_content(mem, client);
        } else if (kind == PanelKind::Lyrics) {
            draw_tt07_lyric_content(mem, client);
        } else if (kind == PanelKind::Equalizer) {
            draw_tt07_equalizer_content(mem);
        }
        BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return;
    }
    const auto accent = color(panel_accent(kind));
    if (kind == PanelKind::Lyrics) {
        draw_text_line(mem, 18, 44, L"Beyond", RGB(0, 120, 130));
        draw_text_line(mem, 18, 75, L"真的爱你(粤)", accent, 30);
        draw_text_line(mem, 18, 118, L"无法可修饰的一对手", RGB(0, 120, 130));
        draw_text_line(mem, 18, 144, L"带出温暖永远在背后", RGB(0, 220, 210));
        RECT karaoke{18, 171, client.right - 18, 176};
        fill_rect(mem, karaoke, RGB(0, 75, 80));
        karaoke.right = karaoke.left + (client.right - 36) / 2;
        fill_rect(mem, karaoke, accent);
    } else if (kind == PanelKind::Playlist) {
        draw_text_line(mem, 14, 34, L"+    x", RGB(0, 220, 120));
        RECT line{8, 57, client.right - 8, 58};
        fill_rect(mem, line, RGB(0, 80, 130));
        std::array<std::wstring_view, 7> rows{
            L"> 1. Beyond - 真的爱你",
            L"  2. Guns N' Roses - Don't Cry",
            L"  3. Metallica - Fade To Black",
            L"  4. The Cranberries - Dreams",
            L"  5. The Cranberries - Dying In The Sun",
            L"  6. The Beatles - Let It Be",
            L"  7. Green Day - Boulevard"
        };
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            if (i == 0) {
                RECT selected{8, 64 + i * 20, client.right - 8, 83 + i * 20};
                fill_rect(mem, selected, RGB(18, 86, 154));
            }
            draw_text_line(mem, 14, 65 + i * 20, rows[i], i == 0 ? RGB(0, 240, 230) : RGB(0, 160, 150));
        }
    } else if (kind == PanelKind::Equalizer) {
        draw_text_line(mem, 14, 38, L"均衡器: 关", RGB(0, 220, 210));
        const int base = client.bottom - 28;
        for (int i = 0; i < 10; ++i) {
            const int x = 34 + i * 24;
            HPEN pen = CreatePen(PS_SOLID, 1, RGB(88, 164, 238));
            HPEN old = static_cast<HPEN>(SelectObject(mem, pen));
            MoveToEx(mem, x, 80, nullptr);
            LineTo(mem, x, base);
            SelectObject(mem, old);
            DeleteObject(pen);
            RECT thumb{x - 4, 90 + (i % 3) * 5, x + 5, 99 + (i % 3) * 5};
            fill_rect(mem, thumb, RGB(215, 245, 255));
        }
    }

    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

constexpr UINT kPlaylistCommandBase = 6000;
constexpr UINT kPlaylistAddFile = kPlaylistCommandBase + 1;
constexpr UINT kPlaylistAddFolder = kPlaylistCommandBase + 2;
constexpr UINT kPlaylistAddUrl = kPlaylistCommandBase + 3;
constexpr UINT kPlaylistDeleteSelected = kPlaylistCommandBase + 11;
constexpr UINT kPlaylistDeleteDuplicate = kPlaylistCommandBase + 12;
constexpr UINT kPlaylistDeleteMissing = kPlaylistCommandBase + 13;
constexpr UINT kPlaylistDeleteAll = kPlaylistCommandBase + 14;
constexpr UINT kPlaylistDeletePhysical = kPlaylistCommandBase + 15;
constexpr UINT kPlaylistListNew = kPlaylistCommandBase + 21;
constexpr UINT kPlaylistListAppend = kPlaylistCommandBase + 22;
constexpr UINT kPlaylistListOpen = kPlaylistCommandBase + 23;
constexpr UINT kPlaylistListSave = kPlaylistCommandBase + 24;
constexpr UINT kPlaylistListDelete = kPlaylistCommandBase + 25;
constexpr UINT kPlaylistListSaveAll = kPlaylistCommandBase + 26;
constexpr UINT kPlaylistListLibraryMode = kPlaylistCommandBase + 27;
constexpr UINT kPlaylistSortTitle = kPlaylistCommandBase + 31;
constexpr UINT kPlaylistSortFilename = kPlaylistCommandBase + 32;
constexpr UINT kPlaylistSortPath = kPlaylistCommandBase + 33;
constexpr UINT kPlaylistSortAlbum = kPlaylistCommandBase + 34;
constexpr UINT kPlaylistSortRating = kPlaylistCommandBase + 35;
constexpr UINT kPlaylistSortFileTime = kPlaylistCommandBase + 36;
constexpr UINT kPlaylistSortTrackNo = kPlaylistCommandBase + 37;
constexpr UINT kPlaylistSortDuration = kPlaylistCommandBase + 38;
constexpr UINT kPlaylistSortShuffle = kPlaylistCommandBase + 39;
constexpr UINT kPlaylistFindLocate = kPlaylistCommandBase + 41;
constexpr UINT kPlaylistFindSong = kPlaylistCommandBase + 42;
constexpr UINT kPlaylistFindNext = kPlaylistCommandBase + 43;
constexpr UINT kPlaylistEditCut = kPlaylistCommandBase + 51;
constexpr UINT kPlaylistEditCopy = kPlaylistCommandBase + 52;
constexpr UINT kPlaylistEditPaste = kPlaylistCommandBase + 53;
constexpr UINT kPlaylistEditMoveToList = kPlaylistCommandBase + 54;
constexpr UINT kPlaylistEditCopyToList = kPlaylistCommandBase + 55;
constexpr UINT kPlaylistEditSelectAll = kPlaylistCommandBase + 56;
constexpr UINT kPlaylistEditSelectNone = kPlaylistCommandBase + 57;
constexpr UINT kPlaylistEditInvertSelect = kPlaylistCommandBase + 58;
constexpr UINT kPlaylistModeSingle = kPlaylistCommandBase + 61;
constexpr UINT kPlaylistModeSingleLoop = kPlaylistCommandBase + 62;
constexpr UINT kPlaylistModeSequence = kPlaylistCommandBase + 63;
constexpr UINT kPlaylistModeLoop = kPlaylistCommandBase + 64;
constexpr UINT kPlaylistModeRandom = kPlaylistCommandBase + 65;
constexpr UINT kPlaylistModeFollowCursor = kPlaylistCommandBase + 66;
constexpr UINT kPlaylistModeAutoSwitch = kPlaylistCommandBase + 67;
constexpr UINT kEqPresetCommandBase = 5000;

constexpr std::array<std::wstring_view, 16> kEqualizerPresetMenuItems{
    L"推荐配置", L"自定义", L"流行音乐", L"摇滚", L"金属乐", L"舞曲", L"电子乐",
    L"乡村音乐", L"爵士乐", L"古典乐", L"布鲁斯", L"怀旧音乐", L"歌剧", L"语音", L"从文件加载...", L"保存到文件..."
};

bool point_in_skin_rect(const SkinRect& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

EqDragTarget eq_drag_target_at(int x, int y, int& band) {
    band = -1;
    if (!g_skin_definition) {
        return EqDragTarget::None;
    }
    if (const auto preamp = g_skin_definition->equalizer.controls.find("preamp"); preamp != g_skin_definition->equalizer.controls.end()) {
        SkinRect hit = preamp->second.position;
        hit.left -= 5;
        hit.right += 5;
        if (point_in_skin_rect(hit, x, y)) {
            return EqDragTarget::Preamp;
        }
    }
    for (int i = 0; i < kEqualizerBandCount; ++i) {
        SkinRect hit = eq_band_rect(i);
        hit.left -= 5;
        hit.right += 5;
        if (point_in_skin_rect(hit, x, y)) {
            band = i;
            return EqDragTarget::Band;
        }
    }
    if (const auto balance = g_skin_definition->equalizer.controls.find("balance"); balance != g_skin_definition->equalizer.controls.end()) {
        SkinRect hit = balance->second.position;
        hit.top -= 4;
        hit.bottom += 4;
        if (point_in_skin_rect(hit, x, y)) {
            return EqDragTarget::Balance;
        }
    }
    if (const auto surround = g_skin_definition->equalizer.controls.find("surround"); surround != g_skin_definition->equalizer.controls.end()) {
        SkinRect hit = surround->second.position;
        hit.top -= 4;
        hit.bottom += 4;
        if (point_in_skin_rect(hit, x, y)) {
            return EqDragTarget::Surround;
        }
    }
    return EqDragTarget::None;
}

int horizontal_slider_value_from_x(int x, const SkinRect& rect) {
    const int thumb_width = 5;
    const int travel = std::max(1, rect.right - rect.left - thumb_width);
    const int clamped_x = std::clamp(x - thumb_width / 2, rect.left, rect.left + travel);
    const double normalized = static_cast<double>(clamped_x - rect.left) / static_cast<double>(travel);
    return std::clamp(static_cast<int>(normalized * 24.0 + 0.5) - 12, -12, 12);
}

void update_equalizer_drag(HWND hwnd, int x, int y) {
    if (!g_skin_definition) {
        return;
    }
    g_equalizer.enabled = true;
    if (g_eq_drag_target == EqDragTarget::Preamp) {
        if (const auto preamp = g_skin_definition->equalizer.controls.find("preamp"); preamp != g_skin_definition->equalizer.controls.end()) {
            g_equalizer.preamp = slider_y_to_equalizer_value(y, preamp->second.position.top, preamp->second.position.bottom, 5);
        }
    } else if (g_eq_drag_target == EqDragTarget::Band && g_eq_drag_band >= 0 && g_eq_drag_band < kEqualizerBandCount) {
        const auto rect = eq_band_rect(g_eq_drag_band);
        g_equalizer.bands[static_cast<std::size_t>(g_eq_drag_band)] = slider_y_to_equalizer_value(y, rect.top, rect.bottom, 5);
        g_equalizer.preset_name = L"自定义";
    } else if (g_eq_drag_target == EqDragTarget::Balance) {
        if (const auto balance = g_skin_definition->equalizer.controls.find("balance"); balance != g_skin_definition->equalizer.controls.end()) {
            g_equalizer.balance = horizontal_slider_value_from_x(x, balance->second.position);
        }
    } else if (g_eq_drag_target == EqDragTarget::Surround) {
        if (const auto surround = g_skin_definition->equalizer.controls.find("surround"); surround != g_skin_definition->equalizer.controls.end()) {
            g_equalizer.surround = horizontal_slider_value_from_x(x, surround->second.position);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

std::filesystem::path canonical_playlist_key(const std::filesystem::path& path) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        canonical = std::filesystem::absolute(path, error);
    }
    if (error) {
        canonical = path;
    }
    return canonical;
}

bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool regular_audio_file(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && is_supported_audio_file(path);
}

std::optional<std::filesystem::path> choose_open_file(HWND hwnd, const wchar_t* title, const wchar_t* filter) {
    std::array<wchar_t, MAX_PATH * 4> file{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) {
        return std::nullopt;
    }
    return std::filesystem::path(file.data());
}

std::optional<std::filesystem::path> choose_save_file(HWND hwnd, const wchar_t* title, const wchar_t* filter, const wchar_t* default_ext) {
    std::array<wchar_t, MAX_PATH * 4> file{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrDefExt = default_ext;
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) {
        return std::nullopt;
    }
    return std::filesystem::path(file.data());
}

std::optional<std::filesystem::path> choose_folder(HWND hwnd) {
    BROWSEINFOW browse{};
    browse.hwndOwner = hwnd;
    browse.lpszTitle = L"添加文件夹";
    browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&browse);
    if (!pidl) {
        return std::nullopt;
    }
    std::array<wchar_t, MAX_PATH> buffer{};
    const BOOL ok = SHGetPathFromIDListW(pidl, buffer.data());
    CoTaskMemFree(pidl);
    if (!ok) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer.data());
}

std::vector<PlaylistTrack> load_playlist_file_tracks(const std::filesystem::path& path) {
    std::vector<PlaylistTrack> tracks;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return tracks;
    }
    std::string line;
    const auto base = path.parent_path();
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        const auto last = line.find_last_not_of(" \t");
        std::wstring wide = utf8_to_wide(std::string_view(line).substr(first, last - first + 1));
        if (wide.empty()) {
            wide = latin1_to_wide(std::string_view(line).substr(first, last - first + 1));
        }
        std::filesystem::path track_path(wide);
        if (track_path.is_relative()) {
            track_path = base / track_path;
        }
        if (!regular_audio_file(track_path)) {
            continue;
        }
        PlaylistTrack track{track_path, playlist_title_from_path(track_path)};
        populate_track_duration(track);
        tracks.push_back(std::move(track));
    }
    return tracks;
}

void save_playlist_file(const std::filesystem::path& path, const std::vector<PlaylistTrack>& tracks) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return;
    }
    output << "#EXTM3U\n";
    for (const auto& track : tracks) {
        output << wide_to_utf8(canonical_playlist_key(track.path).wstring()) << "\n";
    }
}

void save_playlist_file(const std::filesystem::path& path) {
    commit_current_playlist_to_library();
    save_playlist_file(path, g_playlist);
}

std::filesystem::path playlist_save_all_path(const std::filesystem::path& selected_path, const std::wstring& list_name, std::size_t index) {
    const auto directory = selected_path.parent_path();
    const auto extension = selected_path.extension().empty() ? std::filesystem::path(L".m3u8") : selected_path.extension();
    std::wstring stem = selected_path.stem().wstring();
    if (index != 0 || g_playlist_library.list_count() > 1) {
        stem += L"_";
        stem += list_name.empty() ? std::to_wstring(index + 1) : list_name;
    }
    return directory / (stem + extension.wstring());
}

void set_clipboard_text(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) {
        return;
    }
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        void* data = GlobalLock(memory);
        if (data) {
            std::memcpy(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

std::wstring clipboard_text(HWND hwnd) {
    std::wstring text;
    if (!OpenClipboard(hwnd)) {
        return text;
    }
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle) {
        if (auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            text = data;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return text;
}

void copy_current_track_to_clipboard(HWND hwnd) {
    if (g_playlist.empty() || g_audio.current_index < 0 || g_audio.current_index >= static_cast<int>(g_playlist.size())) {
        return;
    }
    set_clipboard_text(hwnd, canonical_playlist_key(g_playlist[static_cast<std::size_t>(g_audio.current_index)].path).wstring());
}

void paste_tracks_from_clipboard(HWND hwnd) {
    std::wistringstream input(clipboard_text(hwnd));
    std::wstring line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        const auto first = line.find_first_not_of(L" \t");
        if (first == std::wstring::npos) {
            continue;
        }
        const auto last = line.find_last_not_of(L" \t");
        append_track_path(hwnd, std::filesystem::path(line.substr(first, last - first + 1)));
    }
}

void show_message(HWND hwnd, const wchar_t* text) {
    MessageBoxW(hwnd, text, L"千千静听", MB_OK | MB_ICONINFORMATION);
}

void open_current_track_in_explorer(HWND hwnd) {
    if (g_playlist.empty() || g_audio.current_index < 0 || g_audio.current_index >= static_cast<int>(g_playlist.size())) {
        return;
    }
    const auto path = canonical_playlist_key(g_playlist[static_cast<std::size_t>(g_audio.current_index)].path).wstring();
    const std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(hwnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void append_playlist_menu_item(HMENU menu, UINT flags, UINT id, const wchar_t* text) {
    AppendMenuW(menu, flags, id, text);
}

void append_playlist_menu_for_action(HMENU menu, PlaylistToolbarAction action) {
    switch (action) {
    case PlaylistToolbarAction::Add:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistAddFile, L"文件");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistAddFolder, L"文件夹");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"本地搜索");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"网上搜索");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"添加 URL");
        break;
    case PlaylistToolbarAction::Delete:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistDeleteSelected, L"选中的文件");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistDeleteDuplicate, L"重复的文件");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistDeleteMissing, L"错误的文件");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_STRING, kPlaylistDeleteAll, L"全部删除");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistDeletePhysical, L"物理删除");
        break;
    case PlaylistToolbarAction::List:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListNew, L"新建列表");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListAppend, L"添加列表");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListOpen, L"打开列表");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListSave, L"保存列表");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListDelete, L"删除列表");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_STRING, kPlaylistListSaveAll, L"保存所有列表");
        append_playlist_menu_item(menu, MF_CHECKED | MF_GRAYED, kPlaylistListLibraryMode, L"媒体库模式");
        break;
    case PlaylistToolbarAction::Sort:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortTitle, L"按显示标题");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortFilename, L"按文件名");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortPath, L"按路径名");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortAlbum, L"按专辑名");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortRating, L"按星级");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortFileTime, L"按文件时间");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortTrackNo, L"按音轨序号");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortDuration, L"按播放长度");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_STRING, kPlaylistSortShuffle, L"随机乱序");
        break;
    case PlaylistToolbarAction::Find:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistFindLocate, L"快速定位");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"查找歌曲");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"查找下一个");
        break;
    case PlaylistToolbarAction::Edit:
        append_playlist_menu_item(menu, MF_STRING, kPlaylistEditCut, L"剪切");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistEditCopy, L"复制");
        append_playlist_menu_item(menu, MF_STRING, kPlaylistEditPaste, L"粘贴");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"移动到列表");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"复制到列表");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"全部选中");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"全部不选");
        append_playlist_menu_item(menu, MF_GRAYED, 0, L"反向选中");
        break;
    case PlaylistToolbarAction::Mode:
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_play_mode == PlaylistPlayMode::Single ? MF_CHECKED : 0), kPlaylistModeSingle, L"单曲播放");
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_play_mode == PlaylistPlayMode::SingleLoop ? MF_CHECKED : 0), kPlaylistModeSingleLoop, L"单曲循环");
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_play_mode == PlaylistPlayMode::Sequence ? MF_CHECKED : 0), kPlaylistModeSequence, L"顺序播放");
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_play_mode == PlaylistPlayMode::Loop ? MF_CHECKED : 0), kPlaylistModeLoop, L"循环播放");
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_play_mode == PlaylistPlayMode::Random ? MF_CHECKED : 0), kPlaylistModeRandom, L"随机播放");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_follow_cursor ? MF_CHECKED : 0), kPlaylistModeFollowCursor, L"播放跟随光标");
        append_playlist_menu_item(menu, MF_STRING | (g_playlist_auto_switch ? MF_CHECKED : 0), kPlaylistModeAutoSwitch, L"自动切换列表");
        break;
    case PlaylistToolbarAction::None:
        break;
    }
}

void sort_playlist_by_filetime(HWND hwnd) {
    const auto current_path = current_track_path_before_reorder();
    std::sort(g_playlist.begin(), g_playlist.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        std::error_code ea;
        std::error_code eb;
        const auto ta = std::filesystem::last_write_time(a.path, ea);
        const auto tb = std::filesystem::last_write_time(b.path, eb);
        if (ea || eb) {
            return a.path.wstring() < b.path.wstring();
        }
        return ta < tb;
    });
    commit_current_playlist_to_library();
    finish_playlist_reorder(hwnd, current_path);
}

void handle_playlist_menu_command(HWND hwnd, UINT command) {
    constexpr const wchar_t* audio_filter = L"音频文件\0*.mp3;*.flac;*.wav;*.wma;*.ape;*.ogg;*.m4a\0所有文件\0*.*\0";
    constexpr const wchar_t* list_filter = L"播放列表\0*.m3u;*.m3u8;*.txt\0所有文件\0*.*\0";
    switch (command) {
    case kPlaylistAddFile:
        if (const auto path = choose_open_file(hwnd, L"添加文件", audio_filter)) {
            append_track_path(hwnd, *path);
        }
        break;
    case kPlaylistAddFolder:
        if (const auto folder = choose_folder(hwnd)) {
            append_tracks(hwnd, load_directory_tracks_with_durations(*folder));
        }
        break;
    case kPlaylistDeleteSelected:
        remove_track_at(hwnd, g_audio.current_index);
        break;
    case kPlaylistDeleteDuplicate:
        remove_duplicate_tracks(hwnd);
        break;
    case kPlaylistDeleteMissing:
        remove_missing_tracks(hwnd);
        break;
    case kPlaylistDeleteAll:
        clear_playlist(hwnd);
        break;
    case kPlaylistListNew:
        commit_current_playlist_to_library();
        g_playlist_library.new_list();
        load_active_playlist_from_library(hwnd);
        save_app_config();
        break;
    case kPlaylistListDelete:
        commit_current_playlist_to_library();
        g_playlist_library.delete_active();
        load_active_playlist_from_library(hwnd);
        save_app_config();
        break;
    case kPlaylistDeletePhysical:
        if (!g_playlist.empty() && MessageBoxW(hwnd, L"确实要从硬盘删除当前文件吗？", L"千千静听", MB_YESNO | MB_ICONWARNING) == IDYES) {
            const int index = g_audio.current_index;
            const auto path = g_playlist[static_cast<std::size_t>(index)].path;
            if (index == g_audio.current_index) {
                close_audio();
            }
            std::error_code error;
            std::filesystem::remove(path, error);
            if (!error) {
                remove_track_at(hwnd, index);
            } else {
                show_message(hwnd, L"删除文件失败。");
                open_current_audio();
            }
        }
        break;
    case kPlaylistListAppend:
        if (const auto path = choose_open_file(hwnd, L"添加列表", list_filter)) {
            commit_current_playlist_to_library();
            g_playlist_library.add_list(load_playlist_file_tracks(*path), playlist_name_from_file(*path));
            load_active_playlist_from_library(hwnd);
            save_app_config();
        }
        break;
    case kPlaylistListOpen:
        if (const auto path = choose_open_file(hwnd, L"打开列表", list_filter)) {
            g_playlist = load_playlist_file_tracks(*path);
            g_playlist_name = playlist_name_from_file(*path);
            close_audio();
            g_audio.current_index = 0;
            commit_current_playlist_to_library();
            g_playlist_scroll = 0;
            open_current_audio();
            invalidate_panel_kind(PanelKind::Playlist);
            invalidate_playback_views(hwnd);
        }
        break;
    case kPlaylistListSave:
        if (const auto path = choose_save_file(hwnd, L"保存列表", list_filter, L"m3u8")) {
            save_playlist_file(*path);
        }
        break;
    case kPlaylistListSaveAll:
        if (const auto path = choose_save_file(hwnd, L"保存所有列表", list_filter, L"m3u8")) {
            commit_current_playlist_to_library();
            const auto& lists = g_playlist_library.lists();
            for (std::size_t i = 0; i < lists.size(); ++i) {
                save_playlist_file(playlist_save_all_path(*path, lists[i].name, i), lists[i].tracks);
            }
        }
        break;
    case kPlaylistSortTitle:
        sort_playlist_by_title(hwnd);
        break;
    case kPlaylistSortAlbum:
        sort_playlist_by_album(hwnd);
        break;
    case kPlaylistSortRating:
        sort_playlist_by_rating(hwnd);
        break;
    case kPlaylistSortTrackNo:
        sort_playlist_by_track_number(hwnd);
        break;
    case kPlaylistSortFilename:
        sort_playlist_by_filename(hwnd);
        break;
    case kPlaylistSortPath:
        sort_playlist_by_path(hwnd);
        break;
    case kPlaylistSortFileTime:
        sort_playlist_by_filetime(hwnd);
        break;
    case kPlaylistSortDuration:
        sort_playlist_by_duration(hwnd);
        break;
    case kPlaylistSortShuffle:
        shuffle_playlist(hwnd);
        break;
    case kPlaylistFindLocate:
        scroll_playlist_to_current();
        invalidate_panel_kind(PanelKind::Playlist);
        break;
    case kPlaylistEditCut:
        copy_current_track_to_clipboard(hwnd);
        remove_track_at(hwnd, g_audio.current_index);
        break;
    case kPlaylistEditCopy:
        copy_current_track_to_clipboard(hwnd);
        break;
    case kPlaylistEditPaste:
        paste_tracks_from_clipboard(hwnd);
        break;
    case kPlaylistModeSingle:
        g_playlist_play_mode = PlaylistPlayMode::Single;
        save_app_config();
        break;
    case kPlaylistModeSingleLoop:
        g_playlist_play_mode = PlaylistPlayMode::SingleLoop;
        save_app_config();
        break;
    case kPlaylistModeSequence:
        g_playlist_play_mode = PlaylistPlayMode::Sequence;
        save_app_config();
        break;
    case kPlaylistModeLoop:
        g_playlist_play_mode = PlaylistPlayMode::Loop;
        save_app_config();
        break;
    case kPlaylistModeRandom:
        g_playlist_play_mode = PlaylistPlayMode::Random;
        save_app_config();
        break;
    case kPlaylistModeFollowCursor:
        g_playlist_follow_cursor = !g_playlist_follow_cursor;
        save_app_config();
        break;
    case kPlaylistModeAutoSwitch:
        g_playlist_auto_switch = !g_playlist_auto_switch;
        save_app_config();
        break;
    default:
        break;
    }
}

void show_playlist_toolbar_menu(HWND hwnd, int x, int y, PlaylistToolbarAction action) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    append_playlist_menu_for_action(menu, action);
    POINT pt{x, y};
    ClientToScreen(hwnd, &pt);
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command != 0) {
        handle_playlist_menu_command(g_app.main_window ? g_app.main_window : hwnd, command);
    }
}
void show_equalizer_presets_menu(HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        return;
    }
    for (std::size_t i = 0; i < kEqualizerPresetMenuItems.size(); ++i) {
        if (i == 2 || i == 14) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        const UINT id = kEqPresetCommandBase + static_cast<UINT>(i);
        const bool checked = kEqualizerPresetMenuItems[i] == g_equalizer.preset_name;
        AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : 0), id, std::wstring(kEqualizerPresetMenuItems[i]).c_str());
    }
    POINT pt{x, y};
    ClientToScreen(hwnd, &pt);
    const UINT command = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command >= kEqPresetCommandBase && command < kEqPresetCommandBase + kEqualizerPresetMenuItems.size()) {
        const auto index = static_cast<std::size_t>(command - kEqPresetCommandBase);
        if (index < 13) {
            apply_equalizer_preset(g_equalizer, kEqualizerPresetMenuItems[index]);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
}

LRESULT CALLBACK panel_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_DROPFILES:
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
            handle_playlist_drop(hwnd, reinterpret_cast<HDROP>(wparam));
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return hit_test_border(hwnd, lparam);
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        if (auto* panel = find_panel(hwnd)) {
            const auto size = panel_window_size(panel->kind);
            if (panel_resize_rect(panel->kind)) {
                info->ptMinTrackSize.x = 230;
                info->ptMinTrackSize.y = 90;
            } else {
                info->ptMinTrackSize.x = size.width;
                info->ptMinTrackSize.y = size.height;
                info->ptMaxTrackSize.x = size.width;
                info->ptMaxTrackSize.y = size.height;
            }
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        g_app.interactive_move = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_app.interactive_move = false;
        if (auto* panel = find_panel(hwnd)) {
            snap_panel_if_close(*panel);
        }
        save_app_config();
        return 0;
    case WM_WINDOWPOSCHANGED: {
        if (auto* panel = find_panel(hwnd)) {
            RECT current{};
            GetWindowRect(hwnd, &current);
            const int dx = current.left - panel->last_rect.left;
            const int dy = current.top - panel->last_rect.top;
            if (!g_app.arranging) {
                g_app.arranging = true;
                move_dock_followers(hwnd, dx, dy);
                g_app.arranging = false;
                panel->last_rect = current;
                if (should_snap_after_position_change(g_app.interactive_move)) {
                    snap_panel_if_close(*panel);
                }
            } else {
                panel->last_rect = current;
            }
        }
        return 0;
    }
    case WM_PAINT: {
        if (auto* panel = find_panel(hwnd)) {
            paint_panel(hwnd, panel->kind, panel->title);
            return 0;
        }
        break;
    }
    case WM_SETCURSOR:
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
            POINT pt{};
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            RECT client{};
            GetClientRect(hwnd, &client);
            clamp_playlist_divider(client);
            if (hit_test_playlist_divider(pt.x, pt.y, client)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Equalizer) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            int band = -1;
            const auto target = eq_drag_target_at(x, y, band);
            if (target != EqDragTarget::None) {
                g_eq_drag_target = target;
                g_eq_drag_band = band;
                SetCapture(hwnd);
                update_equalizer_drag(hwnd, x, y);
                return 0;
            }
        }
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            RECT client{};
            GetClientRect(hwnd, &client);
            clamp_playlist_divider(client);
            if (begin_playlist_list_drag(hwnd, x, y)) {
                return 0;
            }
            if (handle_playlist_scrollbar_press(hwnd, x, y)) {
                return 0;
            }
            if (hit_test_playlist_divider(x, y, client)) {
                g_playlist_divider_dragging = true;
                SetCapture(hwnd);
                return 0;
            }
        }
        break;
    case WM_MOUSEMOVE:
        if (g_eq_drag_target != EqDragTarget::None) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Equalizer) {
                update_equalizer_drag(hwnd, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            }
        }
        if (g_playlist_list_dragging) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
                update_playlist_list_drag(hwnd, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
                return 0;
            }
        }
        if (g_playlist_scrollbar_dragging) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
                update_playlist_scrollbar_drag(hwnd, GET_Y_LPARAM(lparam));
                return 0;
            }
        }
        if (g_playlist_divider_dragging) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
                RECT client{};
                GetClientRect(hwnd, &client);
                g_playlist_divider_x = GET_X_LPARAM(lparam);
                clamp_playlist_divider(client);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }
        break;
    case WM_CAPTURECHANGED:
        g_playlist_divider_dragging = false;
        g_playlist_scrollbar_dragging = false;
        g_playlist_list_dragging = false;
        g_playlist_list_drag_index = -1;
        g_playlist_list_drag_target_index = -1;
        g_playlist_list_drag_mouse_x = 0;
        g_playlist_list_drag_mouse_y = 0;
        g_eq_drag_target = EqDragTarget::None;
        g_eq_drag_band = -1;
        break;
    case WM_MOUSEWHEEL:
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
            const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
            g_playlist_scroll -= (delta / WHEEL_DELTA) * 3;
            clamp_playlist_scroll_for_window(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (handle_main_shortcut(shortcut_for_key(static_cast<int>(wparam)))) {
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        if (g_eq_drag_target != EqDragTarget::None) {
            g_eq_drag_target = EqDragTarget::None;
            g_eq_drag_band = -1;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_playlist_list_dragging) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
                update_playlist_list_drag(hwnd, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            }
            finish_playlist_list_drag(hwnd, true);
            return 0;
        }
        if (g_playlist_scrollbar_dragging) {
            if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
                update_playlist_scrollbar_drag(hwnd, GET_Y_LPARAM(lparam));
            }
            g_playlist_scrollbar_dragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (g_playlist_divider_dragging) {
            g_playlist_divider_dragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        switch (title_button_from_lparam(hwnd, lparam)) {
        case TitleButton::Close:
            ShowWindow(hwnd, SW_HIDE);
            save_app_config();
            return 0;
        case TitleButton::Minimize:
            minimize_to_tray(g_app.main_window ? g_app.main_window : hwnd);
            return 0;
        case TitleButton::None:
            break;
        }
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Equalizer) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (g_skin_definition) {
                if (const auto control = hit_test_control(g_skin_definition->equalizer, x, y)) {
                    if (*control == "enabled") {
                        g_equalizer.enabled = !g_equalizer.enabled;
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    if (*control == "reset") {
                        reset_equalizer(g_equalizer);
                        InvalidateRect(hwnd, nullptr, FALSE);
                        return 0;
                    }
                    if (*control == "profile") {
                        show_equalizer_presets_menu(hwnd, x, y + 12);
                        return 0;
                    }
                }
            }
        }
        if (auto* panel = find_panel(hwnd); panel && panel->kind == PanelKind::Playlist) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (const auto action = playlist_toolbar_action_at(x, y); action != PlaylistToolbarAction::None) {
                show_playlist_toolbar_menu(hwnd, x, y + 10, action);
                return 0;
            }
            RECT client{};
            GetClientRect(hwnd, &client);
            const int visible_rows = playlist_visible_rows(client);
            clamp_playlist_divider(client);
            if (x < g_playlist_divider_x && y >= 56 && y < 56 + visible_rows * 16) {
                const int list_index = (y - 56) / 16;
                if (list_index >= 0 && list_index < static_cast<int>(g_playlist_library.list_count())) {
                    activate_playlist_list(g_app.main_window ? g_app.main_window : hwnd, static_cast<std::size_t>(list_index));
                    return 0;
                }
            }
            if (x >= g_playlist_divider_x + 4 && x < client.right - 13 && y >= 56 && y < 56 + visible_rows * 16) {
                const int row = (y - 56) / 16;
                const int index = g_playlist_scroll + row;
                if (index >= 0 && index < static_cast<int>(g_playlist.size())) {
                    play_track_at(g_app.main_window, index);
                    return 0;
                }
            }
        }
        break;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        save_app_config();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}


void draw_centered_font_line(HDC dc, const RECT& rect, std::wstring_view text, COLORREF c, int font_height, int weight = FW_BOLD) {
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(font_height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei");
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;
    RECT draw_rect = rect;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &draw_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (old_font) {
        SelectObject(dc, old_font);
    }
    if (font) {
        DeleteObject(font);
    }
}

std::uint32_t runtime_desktop_lyric_base_rgb() {
    return g_config.desklrc_base_color.value_or(desktop_lyric_base_rgb());
}

std::uint32_t runtime_desktop_lyric_progress_rgb() {
    return g_config.desklrc_progress_color.value_or(desktop_lyric_progress_rgb());
}

std::uint32_t runtime_desktop_lyric_shadow_rgb() {
    return g_config.desklrc_shadow_color.value_or(desktop_lyric_shadow_rgb());
}

std::uint32_t runtime_desktop_lyric_background_rgb() {
    return g_config.desklrc_bkgnd_color.value_or(desktop_lyric_background_rgb());
}

int runtime_desktop_lyric_background_alpha() {
    return std::clamp(g_config.desklrc_bkgnd_alpha.value_or(desktop_lyric_background_alpha()), 0, 255);
}

int runtime_desktop_lyric_font_height() {
    return std::clamp(g_config.desklrc_font_height.value_or(desktop_lyric_font_height()), 12, 96);
}

void draw_desktop_lyric_text(HDC dc, const RECT& rect, std::wstring_view text, double phase, int scroll_position_ms) {
    SetBkMode(dc, TRANSPARENT);
    HFONT font = CreateFontW(-runtime_desktop_lyric_font_height(), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"幼圆");
    HFONT old_font = font ? static_cast<HFONT>(SelectObject(dc, font)) : nullptr;

    SIZE text_size{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &text_size);
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    const int rect_width = std::max(1, static_cast<int>(rect.right - rect.left));
    const int rect_height = std::max(1, static_cast<int>(rect.bottom - rect.top));
    const int text_width = std::max(1, static_cast<int>(text_size.cx));
    const int text_left = text_width <= rect_width
        ? static_cast<int>(rect.left) + (rect_width - text_width) / 2
        : static_cast<int>(rect.left) + desktop_lyric_text_scroll_offset(text_width, rect_width, scroll_position_ms);
    const int text_top = static_cast<int>(rect.top) + std::max(0, (rect_height - static_cast<int>(metrics.tmHeight)) / 2);
    const int text_len = static_cast<int>(text.size());

    const int saved_clip = SaveDC(dc);
    IntersectClipRect(dc, rect.left, rect.top, rect.right, rect.bottom);
    auto draw_at = [&](COLORREF color, int dx, int dy) {
        SetTextColor(dc, color);
        ExtTextOutW(dc, text_left + dx, text_top + dy, ETO_CLIPPED, &rect, text.data(), text_len, nullptr);
    };

    draw_at(color_from_rgb24(runtime_desktop_lyric_shadow_rgb()), 2, 2);
    draw_at(color_from_rgb24(runtime_desktop_lyric_base_rgb()), 0, 0);

    const int fill_width = std::clamp(static_cast<int>(std::clamp(phase, 0.0, 1.0) * static_cast<double>(text_width)), 0, text_width);
    if (fill_width > 0) {
        const int saved_progress = SaveDC(dc);
        IntersectClipRect(dc, text_left, rect.top, text_left + fill_width, rect.bottom);
        draw_at(color_from_rgb24(runtime_desktop_lyric_progress_rgb()), 0, 0);
        RestoreDC(dc, saved_progress);
    }
    RestoreDC(dc, saved_clip);

    if (old_font) {
        SelectObject(dc, old_font);
    }
    if (font) {
        DeleteObject(font);
    }
}

void draw_desktop_lyric_content(HDC dc, const RECT& client, bool alpha_background = false) {
    fill_rect(dc, client, desktop_lyric_transparent_key());
    if (desktop_lyric_should_show_background(g_app.desktop_lyric_visible, g_app.desktop_lyric_hovered, g_app.desktop_lyric_locked)) {
        fill_rect(dc, client, color_from_rgb24(alpha_background ? runtime_desktop_lyric_background_rgb() : desktop_lyric_hover_background_rgb()));
    }
    const auto* track = current_track();
    RECT lyric_rect{client.left + 12, client.top + 4, client.right - 12, client.bottom - 4};
    if (!track) {
        draw_desktop_lyric_text(dc, lyric_rect, L"千千静听", 0.0, static_cast<int>(GetTickCount() & 0x7fffffff));
        return;
    }

    const auto lines = lyrics_for_track(*track);
    if (lines.empty()) {
        draw_desktop_lyric_text(dc, lyric_rect, track->title, 0.0, static_cast<int>(GetTickCount() & 0x7fffffff));
        return;
    }

    const int position = audio_position_ms();
    const auto upper = std::upper_bound(lines.begin(), lines.end(), position, [](int value, const LyricLine& line) {
        return value < line.time_ms;
    });
    const bool has_active = upper != lines.begin();
    const std::size_t active = has_active ? static_cast<std::size_t>(std::distance(lines.begin(), upper - 1)) : 0;
    const int next_time = active + 1 < lines.size() ? lines[active + 1].time_ms : lines[active].time_ms + 4000;
    const int span = std::max(1, next_time - lines[active].time_ms);
    const double phase = has_active ? std::clamp(static_cast<double>(position - lines[active].time_ms) / static_cast<double>(span), 0.0, 1.0) : 0.0;
    const int scroll_position = has_active ? std::max(0, position - lines[active].time_ms) : position;
    draw_desktop_lyric_text(dc, lyric_rect, lines[active].text, phase, scroll_position);
}

void apply_desktop_lyric_alpha(std::uint8_t* pixels, int width, int height) {
    const BYTE key_r = GetRValue(desktop_lyric_transparent_key());
    const BYTE key_g = GetGValue(desktop_lyric_transparent_key());
    const BYTE key_b = GetBValue(desktop_lyric_transparent_key());
    const auto background = runtime_desktop_lyric_background_rgb();
    const BYTE bg_r = static_cast<BYTE>((background >> 16) & 0xff);
    const BYTE bg_g = static_cast<BYTE>((background >> 8) & 0xff);
    const BYTE bg_b = static_cast<BYTE>(background & 0xff);
    const BYTE bg_alpha = static_cast<BYTE>(runtime_desktop_lyric_background_alpha());
    for (int i = 0; i < width * height; ++i) {
        std::uint8_t* p = pixels + i * 4;
        const bool transparent_key =
            (p[0] == key_b && p[1] == key_g && p[2] == key_r) ||
            (p[0] == key_r && p[1] == key_g && p[2] == key_b);
        const bool hover_background =
            (p[0] == bg_b && p[1] == bg_g && p[2] == bg_r) ||
            (p[0] == bg_r && p[1] == bg_g && p[2] == bg_b);
        const BYTE alpha = transparent_key ? 0 : (hover_background ? bg_alpha : 255);
        p[0] = static_cast<std::uint8_t>((static_cast<int>(p[0]) * alpha + 127) / 255);
        p[1] = static_cast<std::uint8_t>((static_cast<int>(p[1]) * alpha + 127) / 255);
        p[2] = static_cast<std::uint8_t>((static_cast<int>(p[2]) * alpha + 127) / 255);
        p[3] = alpha;
    }
}

void refresh_desktop_lyric_layered() {
    if (!g_app.desktop_lyric_window || !IsWindow(g_app.desktop_lyric_window)) {
        return;
    }
    RECT window_rect{};
    GetWindowRect(g_app.desktop_lyric_window, &window_rect);
    const int width = std::max(1L, window_rect.right - window_rect.left);
    const int height = std::max(1L, window_rect.bottom - window_rect.top);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP old_bitmap = bitmap ? static_cast<HBITMAP>(SelectObject(mem, bitmap)) : nullptr;
    RECT client{0, 0, width, height};
    if (bitmap && bits) {
        draw_desktop_lyric_content(mem, client, true);
        apply_desktop_lyric_alpha(static_cast<std::uint8_t*>(bits), width, height);
        POINT dst{window_rect.left, window_rect.top};
        POINT src{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(g_app.desktop_lyric_window, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);
    }
    if (old_bitmap) {
        SelectObject(mem, old_bitmap);
    }
    if (bitmap) {
        DeleteObject(bitmap);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void position_desktop_lyric_toolbar() {
    if (!g_app.desktop_lyric_window || !g_app.desktop_lyric_toolbar) {
        return;
    }
    RECT lyric_rect{};
    GetWindowRect(g_app.desktop_lyric_window, &lyric_rect);
    const auto bar = desktop_lyric_toolbar_rect(WindowRect{lyric_rect.left, lyric_rect.top, lyric_rect.right, lyric_rect.bottom});
    SetWindowPos(g_app.desktop_lyric_toolbar, HWND_TOPMOST, bar.left, bar.top, bar.width(), bar.height(), SWP_NOACTIVATE);
}

bool cursor_inside_window(HWND hwnd) {
    if (!hwnd || !IsWindowVisible(hwnd)) {
        return false;
    }
    POINT pt{};
    GetCursorPos(&pt);
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    return PtInRect(&rect, pt) != FALSE;
}

void set_desktop_lyric_hovered(bool hovered);

void hide_desktop_lyric_toolbar_if_idle() {
    if (!g_app.desktop_lyric_locked && (cursor_inside_window(g_app.desktop_lyric_window) || cursor_inside_window(g_app.desktop_lyric_toolbar))) {
        set_desktop_lyric_hovered(true);
        return;
    }
    set_desktop_lyric_hovered(false);
    if (g_app.desktop_lyric_toolbar) {
        ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
    }
}

void set_desktop_lyric_hovered(bool hovered) {
    const bool next = hovered && desktop_lyric_allows_hover_toolbar(g_app.desktop_lyric_locked);
    if (g_app.desktop_lyric_hovered == next) {
        return;
    }
    g_app.desktop_lyric_hovered = next;
    if (g_app.desktop_lyric_window) {
        refresh_desktop_lyric_layered();
    }
}

void show_desktop_lyric_toolbar() {
    set_desktop_lyric_hovered(true);
    if (!g_app.desktop_lyric_visible || !desktop_lyric_allows_hover_toolbar(g_app.desktop_lyric_locked) || !g_app.desktop_lyric_toolbar) {
        return;
    }
    position_desktop_lyric_toolbar();
    ShowWindow(g_app.desktop_lyric_toolbar, SW_SHOWNOACTIVATE);
}

void set_desktop_lyrics_visible(bool visible) {
    g_app.desktop_lyric_visible = visible;
    if (!visible) {
        g_app.desktop_lyric_hovered = false;
        if (g_app.desktop_lyric_toolbar) {
            ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
        }
        if (g_app.desktop_lyric_window) {
            ShowWindow(g_app.desktop_lyric_window, SW_HIDE);
        }
        save_app_config();
        return;
    }
    if (auto* lyric_panel = find_panel(PanelKind::Lyrics)) {
        ShowWindow(lyric_panel->hwnd, SW_HIDE);
    }
    if (g_app.desktop_lyric_window) {
        ShowWindow(g_app.desktop_lyric_window, SW_SHOWNOACTIVATE);
        refresh_desktop_lyric_layered();
    }
    save_app_config();
}

void toggle_desktop_lyrics() {
    set_desktop_lyrics_visible(!g_app.desktop_lyric_visible);
}

void set_desktop_lyrics_locked(bool locked) {
    if (!g_app.desktop_lyric_visible) {
        return;
    }
    g_app.desktop_lyric_locked = locked;
    if (locked) {
        g_app.desktop_lyric_hovered = false;
        if (g_app.desktop_lyric_toolbar) {
            ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
        }
    }
    if (g_app.desktop_lyric_window) {
        refresh_desktop_lyric_layered();
    }
}

void paint_desktop_lyric(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bitmap = CreateCompatibleBitmap(dc, client.right, client.bottom);
    HBITMAP old_bitmap = static_cast<HBITMAP>(SelectObject(mem, bitmap));
    draw_desktop_lyric_content(mem, client);
    BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void draw_toolbar_button(HDC dc, int left, std::wstring_view label, bool active = false) {
    RECT rect{left, 4, left + 18, 21};
    fill_rect(dc, rect, active ? RGB(20, 142, 185) : RGB(38, 95, 122));
    draw_rect_outline(dc, rect, RGB(116, 187, 213));
    draw_centered_font_line(dc, rect, label, RGB(235, 255, 255), 11, FW_BOLD);
}

bool draw_desktop_lyric_icon(HDC dc, const std::string& asset_name, int slot_left, int frame_index = 0) {
    const auto it = g_skin.desktop_lyric_controls.find(asset_name);
    if (it == g_skin.desktop_lyric_controls.end()) {
        return false;
    }
    BITMAP info{};
    if (GetObjectW(it->second, sizeof(info), &info) == 0) {
        return false;
    }
    constexpr int state_count = 4;
    const int frame_width = std::max(1, static_cast<int>(info.bmWidth) / state_count);
    const int x = slot_left + std::max(0, (20 - frame_width) / 2);
    const int y = 6;
    draw_state_bitmap_at(dc, it->second, SkinRect{x, y, x + frame_width, y + static_cast<int>(info.bmHeight)}, frame_index, state_count);
    return true;
}

bool draw_desktop_lyric_toolbar_icons(HDC dc) {
    bool drew = false;
    drew |= draw_desktop_lyric_icon(dc, "desklrc_prev.bmp", 22);
    drew |= draw_desktop_lyric_icon(dc, g_audio.playing ? "desklrc_pause.bmp" : "desklrc_play.bmp", 42);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_next.bmp", 62);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_lines.bmp", 82);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_list.bmp", 102);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_kalaok.bmp", 122);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_settings.bmp", 142);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_lock.bmp", 162, g_app.desktop_lyric_locked ? 1 : 0);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_return.bmp", 182);
    drew |= draw_desktop_lyric_icon(dc, "desklrc_close.bmp", 202);
    return drew;
}

void paint_desktop_lyric_toolbar(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT client{};
    GetClientRect(hwnd, &client);
    if (draw_bitmap_to_client(dc, g_skin.desktop_lyric_toolbar, client, std::nullopt, skin_transparent_key())) {
        if (!draw_desktop_lyric_toolbar_icons(dc)) {
            draw_toolbar_button(dc, 22, L"<");
            draw_toolbar_button(dc, 42, g_audio.playing ? L"||" : L">");
            draw_toolbar_button(dc, 62, L">");
            draw_toolbar_button(dc, 82, L"2");
            draw_toolbar_button(dc, 102, L"L");
            draw_toolbar_button(dc, 122, L"M");
            draw_toolbar_button(dc, 142, L"S");
            draw_toolbar_button(dc, 162, L"K", g_app.desktop_lyric_locked);
            draw_toolbar_button(dc, 182, L"R");
            draw_toolbar_button(dc, 202, L"X");
        }
    } else {
        fill_rect(dc, client, RGB(25, 73, 96));
        draw_rect_outline(dc, client, RGB(150, 220, 238));
        draw_toolbar_button(dc, 22, L"<");
        draw_toolbar_button(dc, 42, g_audio.playing ? L"||" : L">");
        draw_toolbar_button(dc, 62, L">");
        draw_toolbar_button(dc, 82, L"2");
        draw_toolbar_button(dc, 102, L"L");
        draw_toolbar_button(dc, 122, L"M");
        draw_toolbar_button(dc, 142, L"S");
        draw_toolbar_button(dc, 162, L"K", g_app.desktop_lyric_locked);
        draw_toolbar_button(dc, 182, L"R");
        draw_toolbar_button(dc, 202, L"X");
    }
    EndPaint(hwnd, &ps);
}

void handle_desktop_toolbar_action(HWND hwnd, DesktopLyricToolbarAction action) {
    HWND main = g_app.main_window ? g_app.main_window : hwnd;
    switch (action) {
    case DesktopLyricToolbarAction::Previous:
        previous_track(main);
        break;
    case DesktopLyricToolbarAction::PlayPause:
        if (g_audio.playing) {
            pause_audio(main);
        } else {
            play_audio(main);
        }
        break;
    case DesktopLyricToolbarAction::Next:
        next_track(main);
        break;
    case DesktopLyricToolbarAction::Lock:
        set_desktop_lyrics_locked(true);
        break;
    case DesktopLyricToolbarAction::ReturnToPlayer:
        restore_from_tray(main);
        break;
    case DesktopLyricToolbarAction::Close:
        set_desktop_lyrics_visible(false);
        break;
    case DesktopLyricToolbarAction::None:
    case DesktopLyricToolbarAction::Lines:
    case DesktopLyricToolbarAction::List:
    case DesktopLyricToolbarAction::Karaoke:
    case DesktopLyricToolbarAction::Settings:
        break;
    }
    if (g_app.desktop_lyric_toolbar) {
        InvalidateRect(g_app.desktop_lyric_toolbar, nullptr, FALSE);
    }
}

LRESULT desktop_lyric_resize_hit_test(HWND hwnd, LPARAM lparam) {
    if (!desktop_lyric_allows_resize(g_app.desktop_lyric_locked)) {
        return HTCLIENT;
    }
    POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    RECT rect{};
    GetWindowRect(hwnd, &rect);
    constexpr int grip = 8;
    const bool left = pt.x >= rect.left && pt.x < rect.left + grip;
    const bool right = pt.x < rect.right && pt.x >= rect.right - grip;
    const bool top = pt.y >= rect.top && pt.y < rect.top + grip;
    const bool bottom = pt.y < rect.bottom && pt.y >= rect.bottom - grip;
    if (top && left) {
        return HTTOPLEFT;
    }
    if (top && right) {
        return HTTOPRIGHT;
    }
    if (bottom && left) {
        return HTBOTTOMLEFT;
    }
    if (bottom && right) {
        return HTBOTTOMRIGHT;
    }
    if (left) {
        return HTLEFT;
    }
    if (right) {
        return HTRIGHT;
    }
    if (top) {
        return HTTOP;
    }
    if (bottom) {
        return HTBOTTOM;
    }
    return HTCLIENT;
}

LRESULT CALLBACK desktop_lyric_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return desktop_lyric_resize_hit_test(hwnd, lparam);
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = desktop_lyric_min_width();
        info->ptMinTrackSize.y = desktop_lyric_min_height();
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (desktop_lyric_allows_drag(g_app.desktop_lyric_locked)) {
            ReleaseCapture();
            SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, lparam);
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        g_app.desktop_lyric_arranging = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_app.desktop_lyric_arranging = false;
        save_desktop_lyric_rect();
        refresh_desktop_lyric_layered();
        position_desktop_lyric_toolbar();
        return 0;
    case WM_WINDOWPOSCHANGED:
        position_desktop_lyric_toolbar();
        return 0;
    case WM_SIZE:
        refresh_desktop_lyric_layered();
        position_desktop_lyric_toolbar();
        return 0;
    case WM_MOUSEMOVE:
        show_desktop_lyric_toolbar();
        if (!g_app.desktop_lyric_tracking) {
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            g_app.desktop_lyric_tracking = true;
        }
        return 0;
    case WM_MOUSELEAVE:
        g_app.desktop_lyric_tracking = false;
        hide_desktop_lyric_toolbar_if_idle();
        return 0;
    case WM_PAINT:
        paint_desktop_lyric(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        save_desktop_lyric_rect();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK desktop_lyric_toolbar_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_MOUSEMOVE:
        show_desktop_lyric_toolbar();
        if (!g_app.desktop_toolbar_tracking) {
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            g_app.desktop_toolbar_tracking = true;
        }
        return 0;
    case WM_MOUSELEAVE:
        g_app.desktop_toolbar_tracking = false;
        hide_desktop_lyric_toolbar_if_idle();
        return 0;
    case WM_LBUTTONDOWN:
        g_app.desktop_toolbar_pressed = desktop_lyric_toolbar_action_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (g_app.desktop_toolbar_pressed != DesktopLyricToolbarAction::None) {
            SetCapture(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd) {
            ReleaseCapture();
        }
        const auto action = desktop_lyric_toolbar_action_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        if (action != DesktopLyricToolbarAction::None && action == g_app.desktop_toolbar_pressed) {
            g_app.desktop_toolbar_pressed = DesktopLyricToolbarAction::None;
            handle_desktop_toolbar_action(hwnd, action);
            return 0;
        }
        g_app.desktop_toolbar_pressed = DesktopLyricToolbarAction::None;
        break;
    }
    case WM_PAINT:
        paint_desktop_lyric_toolbar(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void create_desktop_lyric_windows() {
    if (g_app.desktop_lyric_window && g_app.desktop_lyric_toolbar) {
        return;
    }
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int work_width = static_cast<int>(work.right - work.left);
    RECT initial_rect{};
    if (g_config.desklrc_wnd) {
        initial_rect = *g_config.desklrc_wnd;
    } else {
        const int x = static_cast<int>(work.left) + std::max(0, (work_width - kDesktopLyricWidth) / 2);
        const int y = static_cast<int>(work.bottom) - kDesktopLyricHeight - 120;
        initial_rect = RECT{x, y, x + kDesktopLyricWidth, y + kDesktopLyricHeight};
    }
    const int x = initial_rect.left;
    const int y = initial_rect.top;
    const int width = std::max(desktop_lyric_min_width(), static_cast<int>(initial_rect.right - initial_rect.left));
    const int height = std::max(desktop_lyric_min_height(), static_cast<int>(initial_rect.bottom - initial_rect.top));
    g_app.desktop_lyric_window = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        desktop_lyric_class_name().data(),
        L"桌面歌词",
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        x,
        y,
        width,
        height,
        g_app.main_window,
        nullptr,
        g_instance,
        nullptr);
    if (g_app.desktop_lyric_window) {
        refresh_desktop_lyric_layered();
    }
    const auto toolbar = desktop_lyric_toolbar_rect(WindowRect{x, y, x + width, y + height});
    g_app.desktop_lyric_toolbar = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        desktop_lyric_toolbar_class_name().data(),
        L"桌面歌词工具栏",
        WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        toolbar.left,
        toolbar.top,
        toolbar.width(),
        toolbar.height(),
        g_app.desktop_lyric_window,
        nullptr,
        g_instance,
        nullptr);
    if (g_app.desktop_lyric_toolbar) {
        SetLayeredWindowAttributes(g_app.desktop_lyric_toolbar, skin_transparent_key(), 255, LWA_COLORKEY);
    }
    if (g_app.desktop_lyric_window) {
        ShowWindow(g_app.desktop_lyric_window, g_app.desktop_lyric_visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    if (g_app.desktop_lyric_toolbar) {
        ShowWindow(g_app.desktop_lyric_toolbar, SW_HIDE);
    }
}

std::optional<RECT> configured_panel_rect(PanelKind kind) {
    switch (kind) {
    case PanelKind::Lyrics:
        return g_config.lyric_wnd;
    case PanelKind::Playlist:
        return g_config.playlist_wnd;
    case PanelKind::Equalizer:
        return g_config.equalizer_wnd;
    }
    return std::nullopt;
}

bool configured_panel_visible(PanelKind kind) {
    switch (kind) {
    case PanelKind::Lyrics:
        return g_config.lyric_visible.value_or(true);
    case PanelKind::Playlist:
        return g_config.playlist_visible.value_or(true);
    case PanelKind::Equalizer:
        return g_config.equalizer_visible.value_or(true);
    }
    return true;
}

bool rects_overlap(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

RECT rect_from_xy_size(int x, int y, SkinSize size) {
    return RECT{x, y, x + size.width, y + size.height};
}

void normalize_panel_startup_layout(const RECT& main_rect) {
    const auto lyrics_size = panel_window_size(PanelKind::Lyrics);
    const auto playlist_size = panel_window_size(PanelKind::Playlist);
    const auto equalizer_size = panel_window_size(PanelKind::Equalizer);
    const RECT equalizer_default = rect_from_xy_size(main_rect.left, main_rect.bottom, equalizer_size);
    const RECT playlist_default = rect_from_xy_size(main_rect.left, equalizer_default.bottom, playlist_size);
    const RECT lyrics_default = rect_from_xy_size(main_rect.left, playlist_default.bottom, lyrics_size);

    if (!g_config.equalizer_wnd) {
        g_config.equalizer_wnd = equalizer_default;
    }
    if (!g_config.playlist_wnd) {
        g_config.playlist_wnd = playlist_default;
    }
    if (!g_config.lyric_wnd) {
        g_config.lyric_wnd = lyrics_default;
    }

    const bool overlaps =
        rects_overlap(*g_config.equalizer_wnd, *g_config.playlist_wnd) ||
        rects_overlap(*g_config.equalizer_wnd, *g_config.lyric_wnd) ||
        rects_overlap(*g_config.playlist_wnd, *g_config.lyric_wnd);
    if (overlaps) {
        g_config.equalizer_wnd = equalizer_default;
        g_config.playlist_wnd = playlist_default;
        g_config.lyric_wnd = lyrics_default;
    }

    g_config.equalizer_visible = true;
    g_config.playlist_visible = true;
    const auto lyric_modes = normalize_lyric_mode_visibility(
        g_config.lyric_visible.value_or(true),
        g_app.desktop_lyric_visible,
        g_app.desktop_lyric_visible);
    g_config.lyric_visible = lyric_modes.lyric_window_visible;
    g_app.desktop_lyric_visible = lyric_modes.desktop_lyric_visible;
}

void create_panel(PanelKind kind, int x, int y, int w, int h, bool visible = true) {
    const DWORD ex_style = WS_EX_TOOLWINDOW;
    const DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    std::wstring title = panel_title(kind);
    HWND hwnd = CreateWindowExW(
        ex_style,
        panel_class_name().data(),
        title.c_str(),
        style,
        x,
        y,
        w,
        h,
        g_app.main_window,
        nullptr,
        g_instance,
        nullptr);
    PanelWindow panel{kind, hwnd, g_app.main_window, {}, std::move(title)};
    GetWindowRect(hwnd, &panel.last_rect);
    if (kind == PanelKind::Playlist) {
        DragAcceptFiles(hwnd, TRUE);
    }
    g_app.panels.push_back(std::move(panel));
    g_app.arranging = true;
    ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    g_app.arranging = false;
}

void create_panel_from_skin(PanelKind kind, int fallback_x, int fallback_y) {
    const auto size = panel_window_size(kind);
    int x = fallback_x;
    int y = fallback_y;
    if (const auto position = panel_window_position(kind)) {
        RECT main_rect{};
        GetWindowRect(g_app.main_window, &main_rect);
        x = main_rect.left + position->left;
        y = main_rect.top + position->top;
    }
    int width = size.width;
    int height = size.height;
    if (const auto rect = configured_panel_rect(kind)) {
        x = rect->left;
        y = rect->top;
        width = std::max(230, static_cast<int>(rect->right - rect->left));
        height = std::max(90, static_cast<int>(rect->bottom - rect->top));
    }
    create_panel(kind, x, y, width, height, configured_panel_visible(kind));
}

void load_playlist_from_saved_session(HWND hwnd, const PlaylistSessionRestore& session) {
    g_playlist_library = session.library;
    g_playlist = g_playlist_library.active_tracks();
    g_playlist_name = g_playlist_library.active_name();
    g_audio.current_index = g_playlist_library.active_track_index();
    g_playlist_scroll = 0;
    if (open_current_audio()) {
        const int resume_position = std::max(0, session.state.resume_position_ms);
        if (resume_position > 0) {
            mci_send(L"seek ttplayer_audio to " + std::to_wstring(resume_position));
        }
        if (session.state.resume_playing) {
            play_audio(hwnd);
        } else {
            invalidate_playback_views(hwnd);
        }
    } else {
        invalidate_playback_views(hwnd);
    }
}

void create_panels() {
    RECT main_rect{};
    GetWindowRect(g_app.main_window, &main_rect);
    normalize_panel_startup_layout(main_rect);
    create_panel_from_skin(PanelKind::Lyrics, main_rect.right, main_rect.top);
    create_panel_from_skin(PanelKind::Playlist, main_rect.left, main_rect.bottom);
    create_panel_from_skin(PanelKind::Equalizer, main_rect.right, main_rect.bottom);
}

LRESULT CALLBACK main_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_CREATE:
        g_app.main_window = hwnd;
        add_tray_icon(hwnd);
        if (g_loaded_playlist_session) {
            load_playlist_from_saved_session(hwnd, *g_loaded_playlist_session);
        } else {
            g_playlist = load_directory_tracks_with_durations(L"D:/BaiduNetdiskDownload/song/世界上排名前100的英文歌");
            commit_current_playlist_to_library();
            open_current_audio();
        }
        SetTimer(hwnd, kPlaybackTimerId, kPlaybackTimerMs, nullptr);
        GetWindowRect(hwnd, &g_app.last_main_rect);
        create_panels();
        create_desktop_lyric_windows();
        return 0;
    case WM_NCHITTEST:
        return hit_test_border(hwnd, lparam);
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto size = player_window_size();
        info->ptMinTrackSize.x = size.width;
        info->ptMinTrackSize.y = size.height;
        if (!g_skin_definition || !g_skin_definition->player.resize_rect) {
            info->ptMaxTrackSize.x = size.width;
            info->ptMaxTrackSize.y = size.height;
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        g_app.interactive_move = true;
        return 0;
    case WM_EXITSIZEMOVE:
        g_app.interactive_move = false;
        save_app_config();
        return 0;
    case WM_MOVING: {
        const auto* moving = reinterpret_cast<const RECT*>(lparam);
        const int dx = moving->left - g_app.last_main_rect.left;
        const int dy = moving->top - g_app.last_main_rect.top;
        if (!g_app.arranging) {
            g_app.arranging = true;
            move_dock_followers(hwnd, dx, dy);
            g_app.arranging = false;
        }
        g_app.last_main_rect = *moving;
        return FALSE;
    }
    case WM_WINDOWPOSCHANGED: {
        RECT current{};
        GetWindowRect(hwnd, &current);
        const int dx = current.left - g_app.last_main_rect.left;
        const int dy = current.top - g_app.last_main_rect.top;
        if (!g_app.arranging) {
            g_app.arranging = true;
            move_dock_followers(hwnd, dx, dy);
            g_app.arranging = false;
        }
        g_app.last_main_rect = current;
        return 0;
    }
    case kTrayCallbackMessage:
        if (LOWORD(lparam) == WM_LBUTTONDBLCLK || LOWORD(lparam) == WM_LBUTTONUP) {
            restore_from_tray(hwnd);
            return 0;
        }
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_CONTEXTMENU) {
            show_tray_menu(hwnd);
            return 0;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case kTrayMenuToggle:
            if (IsWindowVisible(hwnd)) {
                minimize_to_tray(hwnd);
            } else {
                restore_from_tray(hwnd);
            }
            return 0;
        case kTrayMenuPlayPause:
            if (g_audio.playing) {
                pause_audio(hwnd);
            } else {
                play_audio(hwnd);
            }
            return 0;
        case kTrayMenuDesktopLyrics:
            toggle_desktop_lyrics();
            return 0;
        case kTrayMenuLockDesktopLyrics:
            set_desktop_lyrics_locked(!g_app.desktop_lyric_locked);
            return 0;
        case kTrayMenuExit:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        default:
            break;
        }
        break;
    case WM_TIMER:
        if (wparam == kPlaybackTimerId) {
            maybe_advance_finished_track(hwnd);
            invalidate_playback_views(hwnd);
            if (g_app.desktop_lyric_visible && g_app.desktop_lyric_window) {
                refresh_desktop_lyric_layered();
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (handle_main_shortcut(shortcut_for_key(static_cast<int>(wparam)))) {
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        std::string hovered;
        if (g_skin_definition) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (const auto control = hit_test_control(g_skin_definition->player, x, y)) {
                hovered = *control;
            }
        }
        if (hovered != g_app.hovered_player_control) {
            g_app.hovered_player_control = std::move(hovered);
            TRACKMOUSEEVENT track{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&track);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        break;
    }
    case WM_MOUSELEAVE:
        if (!g_app.hovered_player_control.empty()) {
            g_app.hovered_player_control.clear();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP: {
        if (g_skin_definition) {
            const int x = GET_X_LPARAM(lparam);
            const int y = GET_Y_LPARAM(lparam);
            if (const auto control = hit_test_control(g_skin_definition->player, x, y)) {
                if (*control == "progress" || *control == "volume") {
                    handle_slider_click(hwnd, *control, x);
                } else {
                    handle_player_control(hwnd, *control);
                }
                return 0;
            }
        }
        switch (title_button_from_lparam(hwnd, lparam)) {
        case TitleButton::Close:
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        case TitleButton::Minimize:
            minimize_to_tray(g_app.main_window ? g_app.main_window : hwnd);
            return 0;
        case TitleButton::None:
            break;
        }
        if (handle_main_shortcut(main_shortcut_from_lparam(hwnd, lparam))) {
            return 0;
        }
        break;
    }
    case WM_PAINT:
        paint_main(hwnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        save_app_config();
        KillTimer(hwnd, kPlaybackTimerId);
        remove_tray_icon(hwnd);
        close_audio();
        destroy_skin_bitmaps();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void register_windows() {
    WNDCLASSEXW main_class{};
    main_class.cbSize = sizeof(main_class);
    main_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    main_class.lpfnWndProc = main_proc;
    main_class.hInstance = g_instance;
    main_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    main_class.hIcon = load_app_icon(32, 32);
    main_class.hIconSm = load_app_icon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    main_class.hbrBackground = nullptr;
    main_class.lpszClassName = main_class_name().data();
    RegisterClassExW(&main_class);

    WNDCLASSEXW panel_class = main_class;
    panel_class.lpfnWndProc = panel_proc;
    panel_class.hIcon = nullptr;
    panel_class.lpszClassName = panel_class_name().data();
    RegisterClassExW(&panel_class);

    WNDCLASSEXW desktop_lyric_class = main_class;
    desktop_lyric_class.lpfnWndProc = desktop_lyric_proc;
    desktop_lyric_class.hIcon = nullptr;
    desktop_lyric_class.lpszClassName = desktop_lyric_class_name().data();
    RegisterClassExW(&desktop_lyric_class);

    WNDCLASSEXW desktop_toolbar_class = main_class;
    desktop_toolbar_class.lpfnWndProc = desktop_lyric_toolbar_proc;
    desktop_toolbar_class.hIcon = nullptr;
    desktop_toolbar_class.lpszClassName = desktop_lyric_toolbar_class_name().data();
    RegisterClassExW(&desktop_toolbar_class);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    g_instance = instance;
    load_skin_bitmaps();
    load_app_config();
    apply_loaded_player_config();
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    register_windows();

    const DWORD ex_style = WS_EX_APPWINDOW;
    const DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    const auto main_size = player_window_size();
    int main_x = CW_USEDEFAULT;
    int main_y = CW_USEDEFAULT;
    int main_width = main_size.width;
    int main_height = main_size.height;
    if (g_config.player_wnd) {
        main_x = g_config.player_wnd->left;
        main_y = g_config.player_wnd->top;
        main_width = std::max(main_size.width, static_cast<int>(g_config.player_wnd->right - g_config.player_wnd->left));
        main_height = std::max(main_size.height, static_cast<int>(g_config.player_wnd->bottom - g_config.player_wnd->top));
    }
    HWND hwnd = CreateWindowExW(
        ex_style,
        main_class_name().data(),
        L"千千静听",
        style,
        main_x,
        main_y,
        main_width,
        main_height,
        nullptr,
        nullptr,
        instance,
        nullptr);

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, show_command);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

