#pragma once

struct LyricModeVisibility {
    bool lyric_window_visible = false;
    bool desktop_lyric_visible = false;
};

LyricModeVisibility normalize_lyric_mode_visibility(bool lyric_window_visible, bool desktop_lyric_visible, bool prefer_desktop_lyrics);
