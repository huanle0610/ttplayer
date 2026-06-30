#pragma once

#include <cstdint>
#include <optional>
#include <vector>

struct WindowRect {
    int left{};
    int top{};
    int right{};
    int bottom{};

    int width() const;
    int height() const;
    WindowRect moved_by(int dx, int dy) const;
};

enum class SkinPaintMode {
    SkinBitmap,
    FallbackChrome,
};

enum class MainShortcut {
    None,
    Lyrics,
    Playlist,
    Equalizer,
    ResetLayout,
};

enum class TitleButton {
    None,
    Minimize,
    Close,
};


enum class PlaylistScrollbarPart {
    None,
    UpButton,
    DownButton,
    TrackAboveThumb,
    TrackBelowThumb,
    Thumb,
};

struct PlaylistScrollbarGeometry {
    WindowRect up_button;
    WindowRect down_button;
    WindowRect track;
    WindowRect thumb;
    bool visible{};
};

struct SnapResult {
    WindowRect rect;
    bool attached{};
};

struct DockNode {
    std::uintptr_t id{};
    std::uintptr_t attached_to{};
};

struct StretchSegment {
    WindowRect source;
    WindowRect dest;
};

SnapResult snap_rect_to_neighbors(WindowRect moving, const std::vector<WindowRect>& neighbors, int threshold);
std::vector<StretchSegment> resize_segments_for_skin(WindowRect source_bounds, WindowRect dest_bounds, WindowRect stretch_rect);
std::vector<std::uintptr_t> dock_followers_for_drag(std::uintptr_t source, const std::vector<DockNode>& nodes);
bool should_snap_after_position_change(bool interactive_move);
bool edges_near(int a, int b, int threshold);
TitleButton title_button_at(int client_width, int title_height, int x, int y);
PlaylistScrollbarGeometry playlist_scrollbar_geometry(int client_width, int client_height, int total_rows, int visible_rows, int scroll);
PlaylistScrollbarPart playlist_scrollbar_part_at(const PlaylistScrollbarGeometry& geometry, int x, int y);
int playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart part, int current_scroll, int total_rows, int visible_rows);
int playlist_scroll_from_thumb_top(const PlaylistScrollbarGeometry& geometry, int thumb_top, int total_rows, int visible_rows);
MainShortcut main_shortcut_at(int client_width, int client_height, int x, int y);
MainShortcut shortcut_for_key(int virtual_key);
SkinPaintMode paint_mode_for_skin_bitmap(bool has_skin_bitmap);
bool should_draw_fallback_panel_content(SkinPaintMode mode);
bool should_overlay_player_controls(SkinPaintMode mode);



