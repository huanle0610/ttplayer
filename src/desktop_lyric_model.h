#pragma once

#include "window_layout.h"

#include <cstdint>

enum class DesktopLyricToolbarAction {
    None,
    Previous,
    PlayPause,
    Next,
    Lines,
    List,
    Karaoke,
    Settings,
    Lock,
    ReturnToPlayer,
    Close,
};

WindowRect desktop_lyric_toolbar_rect(WindowRect lyric_rect);
DesktopLyricToolbarAction desktop_lyric_toolbar_action_at(int x, int y);
bool desktop_lyric_allows_hover_toolbar(bool locked);
bool desktop_lyric_allows_drag(bool locked);
std::uint32_t desktop_lyric_base_rgb();
std::uint32_t desktop_lyric_progress_rgb();
std::uint32_t desktop_lyric_shadow_rgb();
int desktop_lyric_font_height();
int desktop_lyric_text_scroll_offset(int text_width, int viewport_width, int position_ms);
bool desktop_lyric_allows_resize(bool locked);
int desktop_lyric_min_width();
int desktop_lyric_min_height();
bool desktop_lyric_should_show_background(bool visible, bool hovered, bool locked);
std::uint32_t desktop_lyric_background_rgb();
int desktop_lyric_background_alpha();
std::uint32_t desktop_lyric_hover_background_rgb();
