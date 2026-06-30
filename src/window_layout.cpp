#include "window_layout.h"

#include <algorithm>
#include <array>

int WindowRect::width() const {
    return right - left;
}

int WindowRect::height() const {
    return bottom - top;
}

WindowRect WindowRect::moved_by(int dx, int dy) const {
    return WindowRect{left + dx, top + dy, right + dx, bottom + dy};
}


std::vector<std::uintptr_t> dock_followers_for_drag(std::uintptr_t source, const std::vector<DockNode>& nodes) {
    const auto source_it = std::find_if(nodes.begin(), nodes.end(), [source](const DockNode& node) {
        return node.id == source;
    });
    if (source_it == nodes.end() || source_it->attached_to != 0) {
        return {};
    }

    std::vector<std::uintptr_t> followers;
    std::vector<std::uintptr_t> pending{source};
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        for (const auto& node : nodes) {
            if (node.attached_to == current && std::find(followers.begin(), followers.end(), node.id) == followers.end()) {
                followers.push_back(node.id);
                pending.push_back(node.id);
            }
        }
    }
    return followers;
}

bool should_snap_after_position_change(bool interactive_move) {
    return !interactive_move;
}
bool edges_near(int a, int b, int threshold) {
    const int delta = a > b ? a - b : b - a;
    return delta <= (threshold < 0 ? 0 : threshold);
}

std::vector<StretchSegment> resize_segments_for_skin(WindowRect source_bounds, WindowRect dest_bounds, WindowRect stretch_rect) {
    const int source_left_width = stretch_rect.left - source_bounds.left;
    const int source_right_width = source_bounds.right - stretch_rect.right;
    const int source_top_height = stretch_rect.top - source_bounds.top;
    const int source_bottom_height = source_bounds.bottom - stretch_rect.bottom;

    const int dest_left = dest_bounds.left + source_left_width;
    const int dest_right = dest_bounds.right - source_right_width;
    const int dest_top = dest_bounds.top + source_top_height;
    const int dest_bottom = dest_bounds.bottom - source_bottom_height;

    const std::array<int, 4> sx{source_bounds.left, stretch_rect.left, stretch_rect.right, source_bounds.right};
    const std::array<int, 4> sy{source_bounds.top, stretch_rect.top, stretch_rect.bottom, source_bounds.bottom};
    const std::array<int, 4> dx{dest_bounds.left, dest_left, std::max(dest_left, dest_right), dest_bounds.right};
    const std::array<int, 4> dy{dest_bounds.top, dest_top, std::max(dest_top, dest_bottom), dest_bounds.bottom};

    std::vector<StretchSegment> segments;
    segments.reserve(9);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            segments.push_back(StretchSegment{
                WindowRect{sx[col], sy[row], sx[col + 1], sy[row + 1]},
                WindowRect{dx[col], dy[row], dx[col + 1], dy[row + 1]},
            });
        }
    }
    return segments;
}
SnapResult snap_rect_to_neighbors(WindowRect moving, const std::vector<WindowRect>& neighbors, int threshold) {
    WindowRect snapped = moving;
    bool attached = false;
    const int width = moving.width();
    const int height = moving.height();

    for (const auto& neighbor : neighbors) {
        if (edges_near(moving.left, neighbor.right, threshold)) {
            snapped.left = neighbor.right;
            snapped.right = snapped.left + width;
            if (edges_near(moving.top, neighbor.top, threshold)) {
                snapped.top = neighbor.top;
                snapped.bottom = snapped.top + height;
            } else if (edges_near(moving.bottom, neighbor.bottom, threshold)) {
                snapped.bottom = neighbor.bottom;
                snapped.top = snapped.bottom - height;
            }
            attached = true;
        } else if (edges_near(moving.right, neighbor.left, threshold)) {
            snapped.right = neighbor.left;
            snapped.left = snapped.right - width;
            if (edges_near(moving.top, neighbor.top, threshold)) {
                snapped.top = neighbor.top;
                snapped.bottom = snapped.top + height;
            } else if (edges_near(moving.bottom, neighbor.bottom, threshold)) {
                snapped.bottom = neighbor.bottom;
                snapped.top = snapped.bottom - height;
            }
            attached = true;
        }

        if (edges_near(moving.top, neighbor.bottom, threshold)) {
            snapped.top = neighbor.bottom;
            snapped.bottom = snapped.top + height;
            if (edges_near(moving.left, neighbor.left, threshold)) {
                snapped.left = neighbor.left;
                snapped.right = snapped.left + width;
            } else if (edges_near(moving.right, neighbor.right, threshold)) {
                snapped.right = neighbor.right;
                snapped.left = snapped.right - width;
            }
            attached = true;
        } else if (edges_near(moving.bottom, neighbor.top, threshold)) {
            snapped.bottom = neighbor.top;
            snapped.top = snapped.bottom - height;
            if (edges_near(moving.left, neighbor.left, threshold)) {
                snapped.left = neighbor.left;
                snapped.right = snapped.left + width;
            } else if (edges_near(moving.right, neighbor.right, threshold)) {
                snapped.right = neighbor.right;
                snapped.left = snapped.right - width;
            }
            attached = true;
        } else if (edges_near(moving.top, neighbor.top, threshold)) {
            snapped.top = neighbor.top;
            snapped.bottom = snapped.top + height;
            attached = true;
        }
    }

    return SnapResult{snapped, attached};
}


PlaylistScrollbarGeometry playlist_scrollbar_geometry(int client_width, int client_height, int total_rows, int visible_rows, int scroll) {
    PlaylistScrollbarGeometry geometry{};
    if (total_rows <= visible_rows || visible_rows <= 0 || client_width <= 0 || client_height <= 0) {
        return geometry;
    }
    const int rail_left = std::max(0, client_width - 12);
    const int rail_top = 52;
    const int rail_bottom = std::max(rail_top + 30, client_height - 4);
    constexpr int rail_width = 8;
    constexpr int button_height = 7;
    geometry.up_button = WindowRect{rail_left, rail_top, rail_left + rail_width, rail_top + button_height};
    geometry.down_button = WindowRect{rail_left, rail_bottom - button_height, rail_left + rail_width, rail_bottom};
    geometry.track = WindowRect{rail_left, geometry.up_button.bottom, rail_left + rail_width, geometry.down_button.top};
    const int track_height = std::max(1, geometry.track.height());
    const int min_thumb_height = std::min(20, track_height);
    const int thumb_height = std::clamp(track_height * visible_rows / total_rows, min_thumb_height, track_height);
    const int max_scroll = std::max(1, total_rows - visible_rows);
    const int clamped_scroll = std::clamp(scroll, 0, max_scroll);
    const int thumb_top = geometry.track.top + (track_height - thumb_height) * clamped_scroll / max_scroll;
    geometry.thumb = WindowRect{rail_left, thumb_top, rail_left + rail_width, thumb_top + thumb_height};
    geometry.visible = true;
    return geometry;
}

bool point_in_window_rect(const WindowRect& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

PlaylistScrollbarPart playlist_scrollbar_part_at(const PlaylistScrollbarGeometry& geometry, int x, int y) {
    if (!geometry.visible) {
        return PlaylistScrollbarPart::None;
    }
    if (point_in_window_rect(geometry.up_button, x, y)) {
        return PlaylistScrollbarPart::UpButton;
    }
    if (point_in_window_rect(geometry.down_button, x, y)) {
        return PlaylistScrollbarPart::DownButton;
    }
    if (point_in_window_rect(geometry.thumb, x, y)) {
        return PlaylistScrollbarPart::Thumb;
    }
    if (point_in_window_rect(geometry.track, x, y)) {
        return y < geometry.thumb.top ? PlaylistScrollbarPart::TrackAboveThumb : PlaylistScrollbarPart::TrackBelowThumb;
    }
    return PlaylistScrollbarPart::None;
}

int playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart part, int current_scroll, int total_rows, int visible_rows) {
    const int max_scroll = std::max(0, total_rows - std::max(1, visible_rows));
    int next = current_scroll;
    switch (part) {
    case PlaylistScrollbarPart::UpButton:
        --next;
        break;
    case PlaylistScrollbarPart::DownButton:
        ++next;
        break;
    case PlaylistScrollbarPart::TrackAboveThumb:
        next -= std::max(1, visible_rows);
        break;
    case PlaylistScrollbarPart::TrackBelowThumb:
        next += std::max(1, visible_rows);
        break;
    case PlaylistScrollbarPart::None:
    case PlaylistScrollbarPart::Thumb:
        break;
    }
    return std::clamp(next, 0, max_scroll);
}

int playlist_scroll_from_thumb_top(const PlaylistScrollbarGeometry& geometry, int thumb_top, int total_rows, int visible_rows) {
    const int max_scroll = std::max(0, total_rows - std::max(1, visible_rows));
    const int travel = std::max(1, geometry.track.height() - geometry.thumb.height());
    const int clamped_top = std::clamp(thumb_top, geometry.track.top, geometry.track.top + travel);
    return std::clamp((clamped_top - geometry.track.top) * max_scroll / travel, 0, max_scroll);
}
TitleButton title_button_at(int client_width, int title_height, int x, int y) {
    if (client_width < 48 || y < 0 || y >= title_height) {
        return TitleButton::None;
    }
    const int close_left = client_width - 22;
    const int close_right = client_width - 6;
    const int minimize_left = client_width - 44;
    const int minimize_right = client_width - 28;
    if (x >= close_left && x < close_right) {
        return TitleButton::Close;
    }
    if (x >= minimize_left && x < minimize_right) {
        return TitleButton::Minimize;
    }
    return TitleButton::None;
}


MainShortcut main_shortcut_at(int client_width, int client_height, int x, int y) {
    if (client_width < 130 || client_height < 36) {
        return MainShortcut::None;
    }
    const int top = client_height - 28;
    const int bottom = client_height - 8;
    if (y < top || y >= bottom) {
        return MainShortcut::None;
    }
    const int start_x = client_width - 153;
    const int button_width = 22;
    const int gap = 6;
    const int pitch = button_width + gap;
    if (x < start_x) {
        return MainShortcut::None;
    }
    const int offset = x - start_x;
    const int index = offset / pitch;
    if (index < 0 || index > 3 || offset % pitch >= button_width) {
        return MainShortcut::None;
    }
    switch (index) {
    case 0:
        return MainShortcut::Lyrics;
    case 1:
        return MainShortcut::Playlist;
    case 2:
        return MainShortcut::Equalizer;
    case 3:
        return MainShortcut::ResetLayout;
    default:
        return MainShortcut::None;
    }
}


MainShortcut shortcut_for_key(int virtual_key) {
    switch (virtual_key) {
    case 'L':
        return MainShortcut::Lyrics;
    case 'P':
        return MainShortcut::Playlist;
    case 'E':
        return MainShortcut::Equalizer;
    case 'R':
        return MainShortcut::ResetLayout;
    default:
        return MainShortcut::None;
    }
}


SkinPaintMode paint_mode_for_skin_bitmap(bool has_skin_bitmap) {
    return has_skin_bitmap ? SkinPaintMode::SkinBitmap : SkinPaintMode::FallbackChrome;
}

bool should_draw_fallback_panel_content(SkinPaintMode mode) {
    return mode == SkinPaintMode::FallbackChrome;
}

bool should_overlay_player_controls(SkinPaintMode mode) {
    return mode == SkinPaintMode::FallbackChrome;
}


