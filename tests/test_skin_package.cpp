#include "skin_definition.h"
#include "lyric_mode.h"
#include "desktop_lyric_model.h"
#include "equalizer_model.h"
#include "skin_package.h"
#include "skin_selection.h"
#include "playlist_model.h"
#include "window_layout.h"

#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << "\n";
        std::exit(1);
    }
}

std::filesystem::path cpp_root_from_test_binary(char** argv) {
    auto current = std::filesystem::absolute(argv[0]).parent_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src")
            && std::filesystem::exists(current / "resources")) {
            return std::filesystem::weakly_canonical(current);
        }
        if (std::filesystem::exists(current / "cpp" / "CMakeLists.txt")) {
            return std::filesystem::weakly_canonical(current / "cpp");
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
}


void verify_equalizer_model() {
    EqualizerState state{};
    apply_equalizer_preset(state, L"推荐配置");
    require(state.enabled, "expected applying preset to enable equalizer");
    require(state.bands[0] == 8 && state.bands[4] == -12 && state.bands[9] == 10,
            "expected recommended preset to match sampled TTPlayer values");
    require(state.preset_name == L"推荐配置", "expected preset name to update");

    apply_equalizer_preset(state, L"语音");
    require(state.bands[0] == -6 && state.bands[2] == 4 && state.bands[9] == -12,
            "expected speech preset to be available from original menu");

    reset_equalizer(state);
    require(!state.enabled || state.bands == equalizer_flat_bands(), "expected reset to restore flat band values");
    require(state.bands == equalizer_flat_bands(), "expected reset bands to be flat");
    require(state.preamp == 0 && state.balance == 0 && state.surround == 0, "expected reset to clear auxiliary values");

    const int top = 33;
    const int bottom = 93;
    const int thumb = 5;
    const int high_y = equalizer_value_to_slider_y(12, top, bottom, thumb);
    const int low_y = equalizer_value_to_slider_y(-12, top, bottom, thumb);
    require(high_y < low_y, "expected higher EQ value to map upward");
    require(slider_y_to_equalizer_value(high_y, top, bottom, thumb) == 12, "expected top slider position to map to +12");
    require(slider_y_to_equalizer_value(low_y, top, bottom, thumb) == -12, "expected bottom slider position to map to -12");
    require(balance_slider_fill_width(0, 60) == 30, "expected centered balance to fill half the rail");
    require(balance_slider_fill_width(12, 60) == 60, "expected right balance to fill the full rail");
    require(balance_slider_fill_width(-12, 60) == 0, "expected left balance to clear the rail");
    require(surround_slider_fill_width(0, 60) == 0, "expected disabled surround to clear the rail");
    require(surround_slider_fill_width(12, 60) == 60, "expected max surround to fill the rail");
}

void verify_default_skin_prefers_tt07(const std::filesystem::path& skin_dir) {
    const auto selected = find_preferred_skin_with_core_assets(skin_dir, L"TT-07");
    require(selected.has_value(), "expected to find preferred TT-07 skin package");
    require(selected->filename().wstring().find(L"TT-07") != std::wstring::npos, "expected default skin package filename to contain TT-07");
}

std::optional<std::filesystem::path> find_sample_skin(const std::filesystem::path& skin_dir) {
    for (const auto& entry : std::filesystem::directory_iterator(skin_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".skn") {
            continue;
        }
        const auto skin = SkinPackage::open(entry.path());
        if (!skin || !skin->contains("Skin.xml")) {
            continue;
        }
        const auto xml = skin->read_text("Skin.xml");
        if (!xml) {
            continue;
        }
        auto definition = parse_skin_definition(*xml);
        if (definition
            && skin->contains(definition->player.image)
            && skin->contains(definition->lyrics.image)
            && skin->contains(definition->playlist.image)
            && skin->contains(definition->equalizer.image)) {
            return entry.path();
        }
    }
    return std::nullopt;
}

void verify_fixture_control_parsing() {
    const std::string fixture_xml = R"(<skin>
        <player_window image="player_skin.bmp">
            <play position="78, 170, 107, 203" image="play.bmp" />
            <exit position="162, 4, 179, 23" image="close.bmp" />
            <progress position="24, 246, 162, 255" thumb_image="progress_thumb.bmp" fill_image="progress_fill.bmp" />
            <icon position="16, 20, 32, 36" image="TTPlayer.ico" />
            <info position="36, 18, 167, 37" color="#ffffff" font="Tahoma" font_size="13" align="left" />
            <status position="124, 94, 186, 112" color="#ffffff" font="Tahoma" font_size="13" align="right" />
            <led position="66, 40, 188, 56" image="number.bmp" align="right" />
            <visual position="16, 57, 187, 88" />
        </player_window>
        <lyric_window position="187, 0, 483, 89" resize_rect="119, 49, 174, 52" image="lyric_skin.bmp" />
        <playlist_window position="-371, 0, 0, 273" resize_rect="158, 155, 199, 159" image="playlist_skin.bmp">
            <toolbar position="3, 28, 241, 42" image="playlist_toolbar.bmp" />
        </playlist_window>
        <equalizer_window position="187, 89, 483, 272" image="equalizer_skin.bmp">
            <enabled position="17, 107, 47, 118" image="eq_enabled.bmp" />
            <reset position="58, 107, 100, 118" image="reset.bmp" />
            <profile position="111, 107, 165, 118" image="eq_profile.bmp" />
        </equalizer_window>
    </skin>)";

    auto definition = parse_skin_definition(fixture_xml);
    require(definition.has_value(), "expected fixture Skin.xml to parse");
    require(definition->player.controls.contains("play"), "expected fixture play control metadata");
    require(definition->player.controls.at("play").position.left == 78, "expected fixture play control left coordinate");
    require(definition->player.controls.at("play").image == "play.bmp", "expected fixture play control image");
    require(definition->player.controls.contains("exit"), "expected fixture exit control metadata");
    require(definition->player.controls.at("exit").position.right == 179, "expected fixture exit control right coordinate");
    require(definition->player.controls.contains("progress"), "expected fixture progress control metadata");
    require(definition->player.controls.at("progress").fill_image == "progress_fill.bmp", "expected fixture progress fill image");
    require(definition->player.controls.contains("icon"), "expected fixture icon control metadata");
    require(definition->player.controls.at("icon").image == "TTPlayer.ico", "expected fixture icon image");
    require(definition->player.controls.contains("info"), "expected fixture info text metadata");
    require(definition->player.controls.contains("status"), "expected fixture status text metadata");
    require(definition->player.controls.at("status").position.left == 124, "expected fixture status left coordinate");
    require(definition->player.controls.contains("led"), "expected fixture led metadata");
    require(definition->player.controls.at("led").image == "number.bmp", "expected fixture led image");
    require(definition->player.controls.contains("visual"), "expected fixture visual metadata");
    require(definition->playlist.controls.contains("toolbar"), "expected fixture playlist toolbar metadata");
    require(definition->playlist.controls.at("toolbar").image == "playlist_toolbar.bmp", "expected fixture playlist toolbar image");
    const auto play_hit = hit_test_control(definition->player, 80, 180);
    require(play_hit.has_value() && *play_hit == "play", "expected play hit test from fixture coordinates");
    const auto exit_hit = hit_test_control(definition->player, 170, 12);
    require(exit_hit.has_value() && *exit_hit == "exit", "expected exit hit test from fixture coordinates");
    const auto progress_hit = hit_test_control(definition->player, 30, 250);
    require(progress_hit.has_value() && *progress_hit == "progress", "expected progress hit test from fixture coordinates");
    require(!hit_test_control(definition->player, 2, 2).has_value(), "expected empty player area to miss controls");
    const auto assets = control_asset_names(definition->player);
    require(assets.contains("play.bmp"), "expected play image in control asset list");
    require(assets.contains("close.bmp"), "expected close image in control asset list");
    require(assets.contains("progress_thumb.bmp"), "expected progress thumb image in control asset list");
    require(assets.contains("progress_fill.bmp"), "expected progress fill image in control asset list");
    require(assets.contains("TTPlayer.ico"), "expected icon image in control asset list");
    require(assets.contains("number.bmp"), "expected led image in control asset list");
    require(!assets.contains(""), "expected empty image names to be ignored");
    const auto fill_rect = slider_fill_rect(definition->player.controls.at("progress"), 0.5);
    require(fill_rect.left == 24 && fill_rect.right == 93, "expected half progress fill rect");
    require(fill_rect.top == 246 && fill_rect.bottom == 255, "expected progress fill to preserve vertical bounds");
    const auto thumb_rect = slider_thumb_rect(definition->player.controls.at("progress"), SkinSize{10, 8}, 0.5);
    require(thumb_rect.left == 88 && thumb_rect.right == 98, "expected centered half progress thumb rect");
    require(thumb_rect.top == 246 && thumb_rect.bottom == 254, "expected progress thumb to align to track top");
}


void verify_skin_resize_segments_preserve_fixed_edges() {
    const WindowRect source{0, 0, 313, 200};
    const WindowRect dest{0, 0, 640, 480};
    const WindowRect stretch{158, 155, 199, 159};
    const auto segments = resize_segments_for_skin(source, dest, stretch);
    require(segments.size() == 9, "expected skin resize to use nine segments");
    require(segments[0].source.right == 158 && segments[0].dest.right == 158, "expected left edge to keep original width");
    require(segments[1].source.left == 158 && segments[1].source.right == 199, "expected center source to come from resize rect width");
    require(segments[1].dest.left == 158 && segments[1].dest.right == 526, "expected center dest to absorb horizontal growth");
    require(segments[3].source.top == 155 && segments[3].source.bottom == 159, "expected middle source to come from resize rect height");
    require(segments[3].dest.top == 155 && segments[3].dest.bottom == 439, "expected middle dest to absorb vertical growth");
    require(segments[8].source.left == 199 && segments[8].source.top == 159, "expected bottom-right source to stay fixed");
    require(segments[8].dest.left == 526 && segments[8].dest.top == 439, "expected bottom-right dest to stay pinned to resized corner");
}


void verify_playlist_toolbar_hit_testing() {
    require(playlist_toolbar_action_at(18, 34) == PlaylistToolbarAction::Add, "expected plus toolbar hit");
    require(playlist_toolbar_action_at(52, 34) == PlaylistToolbarAction::Delete, "expected minus toolbar hit");
    require(playlist_toolbar_action_at(85, 34) == PlaylistToolbarAction::List, "expected list toolbar hit");
    require(playlist_toolbar_action_at(118, 34) == PlaylistToolbarAction::Sort, "expected sort toolbar hit");
    require(playlist_toolbar_action_at(151, 34) == PlaylistToolbarAction::Find, "expected find toolbar hit");
    require(playlist_toolbar_action_at(186, 34) == PlaylistToolbarAction::Edit, "expected edit toolbar hit");
    require(playlist_toolbar_action_at(221, 34) == PlaylistToolbarAction::Mode, "expected mode toolbar hit");
    require(playlist_toolbar_action_at(18, 20) == PlaylistToolbarAction::None, "expected toolbar miss above icons");
}

void verify_playlist_drop_mode() {
    require(playlist_drop_mode_at(20, 80, 200, 68) == PlaylistDropMode::CreateList, "expected drop on left list pane to create a new list");
    require(playlist_drop_mode_at(120, 56, 200, 68) == PlaylistDropMode::AppendToList, "expected drop on first playlist row to append");
    require(playlist_drop_mode_at(120, 183, 200, 68) == PlaylistDropMode::AppendToList, "expected drop on last visible playlist row to append");
    require(playlist_drop_mode_at(120, 184, 200, 68) == PlaylistDropMode::CreateList, "expected drop below visible rows to create list");
    require(playlist_drop_mode_at(120, 34, 200, 68) == PlaylistDropMode::CreateList, "expected drop on toolbar to create list");
}


void verify_playlist_library_keeps_lists_separate() {
    PlaylistLibrary library;
    require(library.list_count() == 1, "expected playlist library to start with one list");
    require(library.active_name() == L"100", "expected default playlist name to match classic skin label");

    std::vector<PlaylistTrack> first_tracks{{L"C:/Music/one.mp3", L"one", 0}};
    library.replace_active(std::move(first_tracks), L"100");
    library.add_list({{L"C:/Music/two.mp3", L"two", 0}}, L"添加列表");

    require(library.list_count() == 2, "expected added playlist file to become a separate list");
    require(library.active_index() == 1, "expected added list to become active");
    require(library.active_tracks().size() == 1, "expected active added list to own its tracks");
    require(library.active_tracks()[0].title == L"two", "expected added list track to be active");

    library.switch_to(0);
    require(library.active_name() == L"100", "expected switch back to first list");
    require(library.active_tracks().size() == 1, "expected first list contents to be preserved");
    require(library.active_tracks()[0].title == L"one", "expected first list not to absorb added list tracks");
}


void verify_playlist_library_fills_blank_track_titles_from_file_name() {
    require(playlist_title_from_path(L"C:/Music/01.wma") == L"01", "expected numeric-only filenames to stay visible");

    PlaylistLibrary library;
    library.replace_active({{L"C:/Music/01.wma", L"", 0}}, L"100");
    require(library.active_tracks()[0].title == L"01", "expected blank imported title to fall back to filename");

    library.add_list({{L"C:/Music/45.第四册 - Lesson [23].wma", L"", 0}}, L"新概念英语第四册");
    require(library.active_tracks()[0].title == L"Lesson [23] - 第四册", "expected blank added-list title to fall back to normalized filename");
}
void verify_playlist_library_delete_and_new_are_list_operations() {
    PlaylistLibrary library;
    library.replace_active({{L"C:/Music/one.mp3", L"one", 0}}, L"100");
    library.new_list();

    require(library.list_count() == 2, "expected new list to add an empty list");
    require(library.active_tracks().empty(), "expected new list to be empty");
    require(library.active_name() == L"101", "expected generated list name to advance from numeric default");

    library.delete_active();
    require(library.list_count() == 1, "expected deleting active list to remove the list, not clear all lists");
    require(library.active_name() == L"100", "expected previous list to become active after delete");
    require(library.active_tracks().size() == 1, "expected previous list tracks to survive delete");

    library.delete_active();
    require(library.list_count() == 1, "expected library to keep one empty list when deleting the last list");
    require(library.active_tracks().empty(), "expected deleting the final list to clear it");
    require(library.active_name() == L"100", "expected final list name to remain stable");
}



void verify_playlist_library_reorders_lists_without_losing_active_list() {
    PlaylistLibrary library;
    library.replace_active({{L"C:/Music/one.mp3", L"one", 0}}, L"100", 0);
    library.add_list({{L"C:/Music/two.mp3", L"two", 0}}, L"Second", 0);
    library.add_list({{L"C:/Music/three.mp3", L"three", 0}}, L"Third", 0);
    require(library.active_name() == L"Third", "expected third list active before reorder");

    require(library.move_list(2, 0), "expected moving third list to first position to succeed");
    require(library.active_index() == 0, "expected active list index to follow moved list");
    require(library.active_name() == L"Third", "expected active list identity to survive reorder");
    require(library.lists()[1].name == L"100", "expected original first list to shift right");

    require(library.move_list(1, 2), "expected moving shifted first list to last position to succeed");
    require(library.active_index() == 0, "expected active list index to remain first after moving another list");
    require(library.lists()[2].name == L"100", "expected moved list to land at last position");
    require(!library.move_list(4, 0), "expected invalid source reorder to fail");
}


void verify_playlist_list_drag_feedback_points_at_target_row() {
    const auto downward = playlist_list_drag_feedback_at(0, 20, 91, 180, 68, 4);
    require(downward.has_value(), "expected drag feedback over a list row");
    require(downward->target_index == 2, "expected y position to target the third list row");
    require(downward->highlight_top == 88, "expected target highlight to align with row top");
    require(downward->insert_y == 104, "expected downward drag insert line at target row bottom");
    require(downward->ghost_top == 84, "expected ghost text to follow the mouse, centered near the cursor");
    require(downward->ghost_bottom == 100, "expected ghost text to keep one list-row height");

    const auto upward = playlist_list_drag_feedback_at(3, 20, 57, 180, 68, 4);
    require(upward.has_value(), "expected upward drag feedback over first row");
    require(upward->target_index == 0, "expected y position to target the first list row");
    require(upward->insert_y == 56, "expected upward drag insert line at target row top");

    const auto outside = playlist_list_drag_feedback_at(1, 80, 57, 180, 68, 4);
    require(!outside.has_value(), "expected no list drag feedback outside left list pane");
}
void verify_playlist_library_tracks_active_song_per_list() {
    PlaylistLibrary library;
    library.replace_active({{L"C:/Music/one.mp3", L"one", 0}, {L"C:/Music/two.mp3", L"two", 0}}, L"100", 1);
    library.add_list({{L"C:/Music/three.mp3", L"three", 0}}, L"101", 0);
    library.set_active_track_index(0);

    library.switch_to(0);
    require(library.active_track_index() == 1, "expected first list to remember its selected song");
    library.switch_to(1);
    require(library.active_track_index() == 0, "expected second list to remember its selected song");
}

void verify_playlist_session_xml_round_trips_lists_and_resume_state() {
    PlaylistLibrary library;
    library.replace_active({{L"C:/Music/one & two.mp3", L"one < two", 1234}, {L"C:/Music/three.mp3", L"three", 5678}}, L"100", 1);
    library.add_list({{L"D:/Song/four.mp3", L"four", 9000}}, L"Favorites", 0);

    PlaylistSessionState state;
    state.active_list_index = library.active_index();
    state.resume_position_ms = 45678;
    state.resume_playing = true;

    const auto xml = serialize_playlist_session(library, state);
    auto restored = parse_playlist_session(xml);

    require(restored.has_value(), "expected playlist session xml to parse");
    require(restored->library.list_count() == 2, "expected restored session to keep all lists");
    require(restored->library.active_index() == 1, "expected restored session to keep active list");
    require(restored->library.active_name() == L"Favorites", "expected restored active list name");
    require(restored->library.active_tracks().size() == 1, "expected restored active list tracks");
    require(restored->library.active_tracks()[0].path.wstring() == L"D:/Song/four.mp3", "expected restored track path");
    restored->library.switch_to(0);
    require(restored->library.active_track_index() == 1, "expected restored first list current song");
    require(restored->library.active_tracks()[0].title == L"one < two", "expected XML text escaping to round trip");
    require(restored->state.resume_position_ms == 45678, "expected restored playback position");
    require(restored->state.resume_playing, "expected restored playing flag");
}

void verify_playlist_scrollbar_hit_testing_and_scroll() {
    const auto geometry = playlist_scrollbar_geometry(313, 200, 30, 8, 10);
    require(geometry.visible, "expected playlist scrollbar when rows overflow");
    require(geometry.up_button.left == 301 && geometry.up_button.top == 52, "expected TT-07 playlist scrollbar up button position");
    require(geometry.down_button.top == 189 && geometry.down_button.bottom == 196, "expected TT-07 playlist scrollbar down button position");
    require(geometry.thumb.height() >= 20, "expected playlist scrollbar thumb minimum height");
    require(playlist_scrollbar_part_at(geometry, 304, 54) == PlaylistScrollbarPart::UpButton, "expected up button hit");
    require(playlist_scrollbar_part_at(geometry, 304, 193) == PlaylistScrollbarPart::DownButton, "expected down button hit");
    require(playlist_scrollbar_part_at(geometry, 304, geometry.thumb.top + 1) == PlaylistScrollbarPart::Thumb, "expected thumb hit");
    require(playlist_scrollbar_part_at(geometry, 304, geometry.thumb.top - 1) == PlaylistScrollbarPart::TrackAboveThumb, "expected track above thumb hit");
    require(playlist_scrollbar_part_at(geometry, 304, geometry.thumb.bottom + 1) == PlaylistScrollbarPart::TrackBelowThumb, "expected track below thumb hit");
    require(playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart::UpButton, 10, 30, 8) == 9, "expected up button to scroll one row");
    require(playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart::DownButton, 10, 30, 8) == 11, "expected down button to scroll one row");
    require(playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart::TrackAboveThumb, 10, 30, 8) == 2, "expected track above to page up");
    require(playlist_scroll_after_scrollbar_click(PlaylistScrollbarPart::TrackBelowThumb, 10, 30, 8) == 18, "expected track below to page down");
    require(playlist_scroll_from_thumb_top(geometry, geometry.track.top, 30, 8) == 0, "expected thumb at track top to map to first row");
    require(playlist_scroll_from_thumb_top(geometry, geometry.track.bottom - geometry.thumb.height(), 30, 8) == 22, "expected thumb at track bottom to map to max row");
    require(!playlist_scrollbar_geometry(313, 200, 6, 8, 0).visible, "expected no scrollbar when all rows fit");
}

void verify_lyric_content_rect_expands_with_window() {
    const SkinRect skin_rect{5, 32, 307, 194};
    const auto normal = lyric_content_rect(skin_rect, 313, 200);
    require(normal.bottom == 196, "expected lyric rect to use available normal panel height");
    const auto tall = lyric_content_rect(skin_rect, 313, 360);
    require(tall.bottom == 356, "expected lyric rect to expand when lyric window is taller");
    require(tall.right == 307, "expected lyric rect to keep skin right edge on normal width");
    const auto wide = lyric_content_rect(skin_rect, 520, 360);
    require(wide.right == 514, "expected lyric rect to expand when lyric window is wider");
}

void verify_playlist_layout_matches_tt07() {
    require(playlist_visible_row_count(200) == 8, "expected TT-07 playlist first screen to show 8 rows");
    require(playlist_visible_row_count(216) == 9, "expected TT-07 playlist rows to advance every 16 pixels");
    require(playlist_duration_text(283000) == L"4:43", "expected playlist duration text to use m:ss");
    require(playlist_duration_text(0).empty(), "expected missing duration to draw no text");
}
void verify_playlist_title_uses_artist_first_for_hyphenated_files() {
    const auto title = playlist_title_from_path(L"01.Don't Cry-Guns N' Roses.mp3");
    require(title == L"Guns N' Roses - Don't Cry", "expected TT-07 title format to match original artist-first display");
}
void verify_playlist_directory_loading_filters_and_sorts_audio() {
    const auto dir = std::filesystem::temp_directory_path() / "ttplayer_playlist_model_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "10.Tenth Song.mp3").put('\0');
    std::ofstream(dir / "02.Second Song-The Band.mp3").put('\0');
    std::ofstream(dir / "01.First Song.flac").put('\0');
    std::ofstream(dir / "01.First Song.lrc").put('\0');

    const auto tracks = load_playlist_from_directory(dir);
    require(tracks.size() == 3, "expected only audio files in playlist");
    require(tracks[0].title == L"First Song", "expected first numeric track first");
    require(tracks[1].title == L"The Band - Second Song", "expected hyphen title to be normalized artist-first");
    require(tracks[2].title == L"Tenth Song", "expected numeric sort before lexicographic sort");

    std::filesystem::remove_all(dir);
}
void verify_window_layout_snapping() {
    const WindowRect main{100, 100, 413, 226};
    const WindowRect lyrics{410, 102, 723, 302};

    const auto result = snap_rect_to_neighbors(lyrics, std::vector<WindowRect>{main}, 12);

    require(result.attached, "expected panel near main edge to attach");
    require(result.rect.left == 413, "expected panel left to snap to main right edge");
    require(result.rect.top == 100, "expected panel top to align with main top edge");
    require(result.rect.width() == 313, "expected snapped panel to preserve width");
    require(result.rect.height() == 200, "expected snapped panel to preserve height");
}

void verify_vertical_stack_aligns_panel_edges() {
    const WindowRect lyrics{413, 100, 726, 300};
    const WindowRect equalizer{416, 297, 729, 423};

    const auto result = snap_rect_to_neighbors(equalizer, std::vector<WindowRect>{lyrics}, 12);

    require(result.attached, "expected panel under another panel to attach");
    require(result.rect.left == 413, "expected vertical stack to align left edges");
    require(result.rect.top == 300, "expected panel to snap directly below neighbor");
}

void verify_horizontal_attach_aligns_panel_edges() {
    const WindowRect main{100, 100, 413, 226};
    const WindowRect lyrics{410, 103, 723, 303};

    const auto result = snap_rect_to_neighbors(lyrics, std::vector<WindowRect>{main}, 12);

    require(result.attached, "expected panel beside main to attach");
    require(result.rect.left == 413, "expected panel to snap to main right edge");
    require(result.rect.top == 100, "expected horizontal attach to align top edges");
}

void verify_window_layout_can_snap_to_neighbor_panel() {
    const WindowRect main{100, 100, 413, 226};
    const WindowRect lyrics{413, 100, 726, 300};
    const WindowRect equalizer{415, 297, 728, 423};

    const auto result = snap_rect_to_neighbors(equalizer, std::vector<WindowRect>{main, lyrics}, 12);

    require(result.attached, "expected equalizer near lyrics to attach");
    require(result.rect.left == 413, "expected equalizer to align with lyrics left edge");
    require(result.rect.top == 300, "expected equalizer to snap under lyrics");
}


void verify_lyric_window_and_desktop_lyrics_are_exclusive() {
    const auto startup = normalize_lyric_mode_visibility(true, true, true);
    require(!startup.lyric_window_visible, "expected startup to hide lyric window when desktop lyrics are enabled");
    require(startup.desktop_lyric_visible, "expected startup to keep desktop lyrics enabled");

    const auto open_lyric_window = normalize_lyric_mode_visibility(true, true, false);
    require(open_lyric_window.lyric_window_visible, "expected opening lyric window to keep lyric window visible");
    require(!open_lyric_window.desktop_lyric_visible, "expected opening lyric window to hide desktop lyrics");

    const auto neither = normalize_lyric_mode_visibility(false, false, true);
    require(!neither.lyric_window_visible && !neither.desktop_lyric_visible, "expected both lyric modes to remain hidden when both are off");
}
void verify_desktop_lyric_toolbar_layout_and_locking() {
    const WindowRect lyric{430, 950, 980, 1039};
    const auto bar = desktop_lyric_toolbar_rect(lyric);
    require(bar.left == 589 && bar.top == 925, "expected desktop lyric toolbar centered above lyric window");
    require(bar.width() == 231 && bar.height() == 25, "expected desktop lyric toolbar to match original size");
    require(desktop_lyric_toolbar_action_at(32, 12) == DesktopLyricToolbarAction::Previous, "expected previous button hit");
    require(desktop_lyric_toolbar_action_at(52, 12) == DesktopLyricToolbarAction::PlayPause, "expected play/pause button hit");
    require(desktop_lyric_toolbar_action_at(72, 12) == DesktopLyricToolbarAction::Next, "expected next button hit");
    require(desktop_lyric_toolbar_action_at(152, 12) == DesktopLyricToolbarAction::Settings, "expected settings button hit");
    require(desktop_lyric_toolbar_action_at(172, 12) == DesktopLyricToolbarAction::Lock, "expected lock button hit");
    require(desktop_lyric_toolbar_action_at(212, 12) == DesktopLyricToolbarAction::Close, "expected close button hit");
    require(desktop_lyric_toolbar_action_at(172, 30) == DesktopLyricToolbarAction::None, "expected toolbar miss below bar");
    require(desktop_lyric_allows_hover_toolbar(false), "expected unlocked desktop lyric to show hover toolbar");
    require(desktop_lyric_allows_drag(false), "expected unlocked desktop lyric to allow dragging");
    require(!desktop_lyric_allows_hover_toolbar(true), "expected locked desktop lyric to suppress hover toolbar");
    require(!desktop_lyric_allows_drag(true), "expected locked desktop lyric to suppress dragging");
    require(desktop_lyric_base_rgb() == 0x00ffff, "expected desktop lyric base color from TTPlayer.xml");
    require(desktop_lyric_progress_rgb() == 0xff0000, "expected desktop lyric progress color from TTPlayer.xml");
    require(desktop_lyric_shadow_rgb() == 0x141414, "expected desktop lyric shadow color from TTPlayer.xml");
    require(desktop_lyric_font_height() == 40, "expected desktop lyric font height from TTPlayer.xml");
    require(desktop_lyric_text_scroll_offset(360, 500, 9000) == 0, "expected fitting desktop lyric text to stay centered");
    require(desktop_lyric_text_scroll_offset(900, 500, 0) == 0, "expected long desktop lyric text to start at the left edge");
    require(desktop_lyric_text_scroll_offset(900, 500, 9000) < 0, "expected long desktop lyric text to scroll instead of ellipsizing");
    require(desktop_lyric_text_scroll_offset(900, 500, 9000) >= -520, "expected desktop lyric scroll to keep text recoverable");
    require(desktop_lyric_allows_resize(false), "expected unlocked desktop lyric to allow resizing");
    require(!desktop_lyric_allows_resize(true), "expected locked desktop lyric to suppress resizing");
    require(desktop_lyric_min_width() == 220 && desktop_lyric_min_height() == 54, "expected desktop lyric resize minimums");
    require(desktop_lyric_should_show_background(true, true, false), "expected unlocked hovered desktop lyric to show background");
    require(!desktop_lyric_should_show_background(true, false, false), "expected desktop lyric background hidden when not hovered");
    require(!desktop_lyric_should_show_background(true, true, true), "expected locked desktop lyric to suppress hover background");
    require(!desktop_lyric_should_show_background(false, true, false), "expected hidden desktop lyric to suppress hover background");
    require(desktop_lyric_background_rgb() == 0xffffff, "expected desktop lyric background color from TTPlayer.xml");
    require(desktop_lyric_background_alpha() == 50, "expected desktop lyric background alpha from TTPlayer.xml");
    require(desktop_lyric_hover_background_rgb() == 0x323232, "expected hover background to use DeskLrc alpha over dark desktop");
}
void verify_title_button_hit_testing() {
    require(title_button_at(313, 25, 300, 12) == TitleButton::Close, "expected title close button hit");
    require(title_button_at(313, 25, 279, 12) == TitleButton::Minimize, "expected title minimize button hit");
    require(title_button_at(313, 25, 220, 12) == TitleButton::None, "expected ordinary title drag area to miss buttons");
    require(title_button_at(313, 25, 300, 31) == TitleButton::None, "expected client area below title to miss buttons");
}

void verify_main_shortcut_hit_testing() {
    require(main_shortcut_at(313, 126, 166, 106) == MainShortcut::Lyrics, "expected lyrics shortcut hit");
    require(main_shortcut_at(313, 126, 194, 106) == MainShortcut::Playlist, "expected playlist shortcut hit");
    require(main_shortcut_at(313, 126, 222, 106) == MainShortcut::Equalizer, "expected equalizer shortcut hit");
    require(main_shortcut_at(313, 126, 250, 106) == MainShortcut::ResetLayout, "expected reset layout shortcut hit");
    require(main_shortcut_at(313, 126, 166, 82) == MainShortcut::None, "expected area above shortcuts to miss");
    require(main_shortcut_at(313, 126, 132, 106) == MainShortcut::None, "expected empty bottom area to miss");
}

void verify_skin_bitmap_disables_player_control_overlay() {
    require(!should_overlay_player_controls(SkinPaintMode::SkinBitmap), "expected TT-07 bitmap mode to avoid double drawing player controls");
    require(should_overlay_player_controls(SkinPaintMode::FallbackChrome), "expected fallback mode to draw synthetic player controls");
}

void verify_skin_bitmap_disables_fallback_panel_content() {
    require(!should_draw_fallback_panel_content(SkinPaintMode::SkinBitmap), "expected real skin bitmap to suppress fallback panel content");
    require(should_draw_fallback_panel_content(SkinPaintMode::FallbackChrome), "expected fallback mode to draw fallback panel content");
}

void verify_skin_bitmap_disables_fallback_chrome() {
    require(paint_mode_for_skin_bitmap(true) == SkinPaintMode::SkinBitmap, "expected real skin bitmap to be painted without fallback chrome");
    require(paint_mode_for_skin_bitmap(false) == SkinPaintMode::FallbackChrome, "expected missing skin bitmap to use fallback chrome");
}

void verify_keyboard_shortcut_mapping() {
    require(shortcut_for_key('L') == MainShortcut::Lyrics, "expected L to toggle lyrics");
    require(shortcut_for_key('P') == MainShortcut::Playlist, "expected P to toggle playlist");
    require(shortcut_for_key('E') == MainShortcut::Equalizer, "expected E to toggle equalizer");
    require(shortcut_for_key('R') == MainShortcut::ResetLayout, "expected R to reset layout");
    require(shortcut_for_key('X') == MainShortcut::None, "expected unrelated key to miss shortcuts");
}


void verify_only_main_drag_moves_attached_windows() {
    const std::vector<DockNode> nodes{
        DockNode{1, 0},
        DockNode{2, 1},
        DockNode{3, 1},
        DockNode{4, 2},
        DockNode{5, 0},
    };
    const auto main_followers = dock_followers_for_drag(1, nodes);
    require(main_followers.size() == 3, "expected main drag to move attached windows");
    require(std::find(main_followers.begin(), main_followers.end(), static_cast<std::uintptr_t>(2)) != main_followers.end(), "expected main drag to include direct attached panel");
    require(std::find(main_followers.begin(), main_followers.end(), static_cast<std::uintptr_t>(3)) != main_followers.end(), "expected main drag to include attached sibling panel");
    require(std::find(main_followers.begin(), main_followers.end(), static_cast<std::uintptr_t>(4)) != main_followers.end(), "expected main drag to include nested attached panel");
    require(std::find(main_followers.begin(), main_followers.end(), static_cast<std::uintptr_t>(5)) == main_followers.end(), "expected detached panel to stay outside main drag followers");

    const auto panel_followers = dock_followers_for_drag(3, nodes);
    require(panel_followers.empty(), "expected panel drag to move only the panel itself");
}

void verify_interactive_drag_defers_snap_until_release() {
    require(!should_snap_after_position_change(true), "expected live dragging to avoid sticky resnap");
    require(should_snap_after_position_change(false), "expected noninteractive move or release to allow snapping");
}
void verify_window_layout_leaves_distant_panels_detached() {
    const WindowRect main{100, 100, 413, 226};
    const WindowRect playlist{50, 360, 363, 560};

    const auto result = snap_rect_to_neighbors(playlist, std::vector<WindowRect>{main}, 12);

    require(!result.attached, "expected distant panel to stay detached");
    require(result.rect.left == playlist.left && result.rect.top == playlist.top, "expected detached panel to stay put");
}

void verify_packaged_default_skin_contract(const std::filesystem::path& cpp_root) {
    const auto skin_dir = packaged_skin_dir(cpp_root);
    const auto default_skin = skin_dir / default_skin_filename();
    require(default_skin.filename().wstring() == L"3、TT-07_随身听 (蓝+黑).skn", "expected packaged default skin filename to be TT-07 blue-black");
    require(std::filesystem::exists(default_skin), "expected packaged TT-07 blue-black skin to exist in repository skins directory");

    const auto selected = find_default_packaged_skin(skin_dir);
    require(selected.has_value(), "expected default packaged skin selector to find TT-07 blue-black skin");
    require(selected->filename() == default_skin.filename(), "expected default packaged skin selector to choose TT-07 blue-black exactly");
}
void verify_cpp_packaging_files(const std::filesystem::path& cpp_root) {
    require(std::filesystem::exists(cpp_root / "resources" / "ttplayer.ico"), "expected original TTPlayer icon resource");
    require(std::filesystem::exists(cpp_root / "resources" / "app.rc"), "expected Win32 resource script");
    require(std::filesystem::exists(cpp_root / "installer" / "TTPlayerClassic.wxs"), "expected WiX package source");
    require(std::filesystem::exists(cpp_root / "installer" / "TTPlayerClassic.wixproj"), "expected WiX project file");
}
} // namespace

int main(int, char** argv) {
    verify_fixture_control_parsing();
    verify_skin_resize_segments_preserve_fixed_edges();
    verify_playlist_toolbar_hit_testing();
    verify_playlist_drop_mode();
    verify_lyric_content_rect_expands_with_window();
    verify_playlist_library_keeps_lists_separate();
    verify_playlist_library_fills_blank_track_titles_from_file_name();
    verify_playlist_library_delete_and_new_are_list_operations();
    verify_playlist_library_reorders_lists_without_losing_active_list();
    verify_playlist_list_drag_feedback_points_at_target_row();
    verify_playlist_library_tracks_active_song_per_list();
    verify_playlist_session_xml_round_trips_lists_and_resume_state();
    verify_playlist_layout_matches_tt07();
    verify_playlist_scrollbar_hit_testing_and_scroll();
    verify_playlist_title_uses_artist_first_for_hyphenated_files();
    verify_playlist_directory_loading_filters_and_sorts_audio();
    verify_window_layout_snapping();
    verify_vertical_stack_aligns_panel_edges();
    verify_horizontal_attach_aligns_panel_edges();
    verify_window_layout_can_snap_to_neighbor_panel();
    verify_lyric_window_and_desktop_lyrics_are_exclusive();
    verify_desktop_lyric_toolbar_layout_and_locking();
    verify_title_button_hit_testing();
    verify_main_shortcut_hit_testing();
    verify_skin_bitmap_disables_fallback_chrome();
    verify_skin_bitmap_disables_fallback_panel_content();
    verify_skin_bitmap_disables_player_control_overlay();
    verify_keyboard_shortcut_mapping();
    verify_only_main_drag_moves_attached_windows();
    verify_interactive_drag_defers_snap_until_release();
    verify_window_layout_leaves_distant_panels_detached();

    const auto cpp_root = cpp_root_from_test_binary(argv);
    verify_cpp_packaging_files(cpp_root);
    verify_packaged_default_skin_contract(cpp_root);
    const auto skin_dir = packaged_skin_dir(cpp_root);
    verify_default_skin_prefers_tt07(skin_dir);
    const auto package_path = find_sample_skin(skin_dir);
    require(package_path.has_value(), "expected to find a sample .skn with core skin assets");

    const auto skin = SkinPackage::open(*package_path);
    require(skin.has_value(), "expected sample .skn package to open");
    require(skin->contains("Skin.xml"), "expected package to contain Skin.xml");
    require(skin->contains("player_skin.bmp"), "expected package to contain player_skin.bmp");
    require(skin->contains("playlist_skin.bmp"), "expected package to contain playlist_skin.bmp");

    const auto xml = skin->read_text("Skin.xml");
    require(xml.has_value(), "expected Skin.xml text to be readable");
    require(xml->find("player_window") != std::string::npos, "expected Skin.xml to describe player_window");

    auto definition = parse_skin_definition(*xml);
    require(definition.has_value(), "expected Skin.xml to parse into window metadata");
    require(definition->player.image == "player_skin.bmp", "expected player image from Skin.xml");
    require(definition->lyrics.image == "lyric_skin.bmp", "expected lyric image from Skin.xml");
    require(definition->playlist.image == "playlist_skin.bmp", "expected playlist image from Skin.xml");
    require(definition->equalizer.image == "eq_skin.bmp", "expected TT-07 equalizer image from Skin.xml");
    require(definition->player.controls.contains("play"), "expected player play control metadata");
    require(definition->player.controls.contains("exit"), "expected player exit control metadata");
    require(definition->player.controls.contains("progress"), "expected progress control metadata");
    require(apply_bitmap_sizes(*definition, *skin), "expected bitmap sizes to apply from skin package");
    require(definition->player.size.width > 250 && definition->player.size.height > 90, "expected player size from bitmap metadata");
    require(definition->lyrics.position.has_value(), "expected lyric window position from Skin.xml");
    require(definition->playlist.resize_rect.has_value(), "expected playlist resize_rect from Skin.xml");

    const auto player = skin->read_binary("player_skin.bmp");
    require(player.has_value(), "expected player_skin.bmp bytes to be readable");
    require(player->size() > 2, "expected player_skin.bmp to have bytes");
    require((*player)[0] == 'B' && (*player)[1] == 'M', "expected player_skin.bmp to be a BMP file");

    const auto materialized = skin->materialize("player_skin.bmp");
    require(materialized.has_value(), "expected player_skin.bmp to materialize to a path");
    require(std::filesystem::exists(*materialized), "expected materialized player_skin.bmp path to exist");

    return 0;
}














