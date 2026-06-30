#include "desktop_lyric_model.h"

#include <algorithm>
#include <array>

WindowRect desktop_lyric_toolbar_rect(WindowRect lyric_rect) {
    constexpr int toolbar_width = 231;
    constexpr int toolbar_height = 25;
    const int left = lyric_rect.left + (lyric_rect.width() - toolbar_width) / 2;
    const int top = lyric_rect.top - toolbar_height;
    return WindowRect{left, top, left + toolbar_width, top + toolbar_height};
}

DesktopLyricToolbarAction desktop_lyric_toolbar_action_at(int x, int y) {
    if (y < 0 || y >= 25) {
        return DesktopLyricToolbarAction::None;
    }
    struct Button {
        int left;
        int right;
        DesktopLyricToolbarAction action;
    };
    constexpr std::array<Button, 10> buttons{{
        {22, 42, DesktopLyricToolbarAction::Previous},
        {42, 62, DesktopLyricToolbarAction::PlayPause},
        {62, 82, DesktopLyricToolbarAction::Next},
        {82, 102, DesktopLyricToolbarAction::Lines},
        {102, 122, DesktopLyricToolbarAction::List},
        {122, 142, DesktopLyricToolbarAction::Karaoke},
        {142, 162, DesktopLyricToolbarAction::Settings},
        {162, 182, DesktopLyricToolbarAction::Lock},
        {182, 202, DesktopLyricToolbarAction::ReturnToPlayer},
        {202, 222, DesktopLyricToolbarAction::Close},
    }};
    for (const auto& button : buttons) {
        if (x >= button.left && x < button.right) {
            return button.action;
        }
    }
    return DesktopLyricToolbarAction::None;
}

bool desktop_lyric_allows_hover_toolbar(bool locked) {
    return !locked;
}

bool desktop_lyric_allows_drag(bool locked) {
    return !locked;
}

std::uint32_t desktop_lyric_base_rgb() {
    return 0x00ffff;
}

std::uint32_t desktop_lyric_progress_rgb() {
    return 0xff0000;
}

std::uint32_t desktop_lyric_shadow_rgb() {
    return 0x141414;
}

int desktop_lyric_font_height() {
    return 40;
}

int desktop_lyric_text_scroll_offset(int text_width, int viewport_width, int position_ms) {
    if (text_width <= 0 || viewport_width <= 0 || text_width <= viewport_width) {
        return 0;
    }
    const int overflow = text_width - viewport_width;
    const int hold_ms = 1200;
    if (position_ms <= hold_ms) {
        return 0;
    }
    constexpr int pixels_per_second = 45;
    const int travel = overflow + std::min(120, viewport_width / 4);
    const int return_hold_px = 80;
    const int scrolled = ((position_ms - hold_ms) * pixels_per_second / 1000) % (travel + return_hold_px);
    if (scrolled > travel) {
        return 0;
    }
    return -std::min(scrolled, travel);
}

bool desktop_lyric_allows_resize(bool locked) {
    return !locked;
}

int desktop_lyric_min_width() {
    return 220;
}

int desktop_lyric_min_height() {
    return 54;
}

bool desktop_lyric_should_show_background(bool visible, bool hovered, bool locked) {
    return visible && hovered && !locked;
}

std::uint32_t desktop_lyric_background_rgb() {
    return 0xffffff;
}

int desktop_lyric_background_alpha() {
    return 50;
}

std::uint32_t desktop_lyric_hover_background_rgb() {
    const int alpha = std::clamp(desktop_lyric_background_alpha(), 0, 255);
    const auto source = desktop_lyric_background_rgb();
    const int red = static_cast<int>((source >> 16) & 0xff) * alpha / 255;
    const int green = static_cast<int>((source >> 8) & 0xff) * alpha / 255;
    const int blue = static_cast<int>(source & 0xff) * alpha / 255;
    return static_cast<std::uint32_t>((red << 16) | (green << 8) | blue);
}
