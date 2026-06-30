#include "lyric_mode.h"

LyricModeVisibility normalize_lyric_mode_visibility(bool lyric_window_visible, bool desktop_lyric_visible, bool prefer_desktop_lyrics) {
    if (lyric_window_visible && desktop_lyric_visible) {
        return prefer_desktop_lyrics ? LyricModeVisibility{false, true} : LyricModeVisibility{true, false};
    }
    return LyricModeVisibility{lyric_window_visible, desktop_lyric_visible};
}
