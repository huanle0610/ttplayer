#pragma once

#include "skin_definition.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>


enum class PlaylistToolbarAction {
    None,
    Add,
    Delete,
    List,
    Sort,
    Find,
    Edit,
    Mode,
};

enum class PlaylistDropMode {
    CreateList,
    AppendToList,
};

struct PlaylistListDragFeedback {
    int source_index = -1;
    int target_index = -1;
    int highlight_top = 0;
    int highlight_bottom = 0;
    int insert_y = 0;
    int ghost_top = 0;
    int ghost_bottom = 0;
};

struct PlaylistTrack {
    std::filesystem::path path;
    std::wstring title;
    int duration_ms = 0;
};

struct PlaylistDocument {
    std::wstring name;
    std::vector<PlaylistTrack> tracks;
    int current_index = 0;
};

class PlaylistLibrary {
public:
    PlaylistLibrary();

    std::size_t list_count() const;
    std::size_t active_index() const;
    const std::wstring& active_name() const;
    const std::vector<PlaylistTrack>& active_tracks() const;
    std::vector<PlaylistTrack>& active_tracks();
    const std::vector<PlaylistDocument>& lists() const;
    int active_track_index() const;

    void replace_active(std::vector<PlaylistTrack> tracks, std::wstring name = L"", int current_index = 0);
    void set_active_tracks(std::vector<PlaylistTrack> tracks);
    void set_active_name(std::wstring name);
    void set_active_track_index(int index);
    void add_list(std::vector<PlaylistTrack> tracks, std::wstring name = L"", int current_index = 0);
    void new_list();
    void delete_active();
    bool switch_to(std::size_t index);
    bool move_list(std::size_t from, std::size_t to);

private:
    std::wstring next_default_name() const;

    std::vector<PlaylistDocument> lists_;
    std::size_t active_index_ = 0;
};


struct PlaylistSessionState {
    std::size_t active_list_index = 0;
    int resume_position_ms = 0;
    bool resume_playing = false;
};

struct PlaylistSessionRestore {
    PlaylistLibrary library;
    PlaylistSessionState state;
};


std::string serialize_playlist_session(const PlaylistLibrary& library, const PlaylistSessionState& state);
std::optional<PlaylistSessionRestore> parse_playlist_session(std::string_view xml);

bool is_supported_audio_file(const std::filesystem::path& path);
std::wstring playlist_title_from_path(const std::filesystem::path& path);
int playlist_visible_row_count(int client_height);
std::wstring playlist_duration_text(int duration_ms);
PlaylistToolbarAction playlist_toolbar_action_at(int x, int y);
PlaylistDropMode playlist_drop_mode_at(int x, int y, int client_height, int divider_x);
std::optional<PlaylistListDragFeedback> playlist_list_drag_feedback_at(int source_index, int x, int y, int client_height, int divider_x, std::size_t list_count);
SkinRect lyric_content_rect(SkinRect skin_rect, int client_width, int client_height);
std::vector<PlaylistTrack> load_playlist_from_directory(const std::filesystem::path& directory);





