#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "playlist_model.h"

#include <algorithm>
#include <charconv>
#include <codecvt>
#include <cwctype>
#include <locale>
#include <optional>
#include <sstream>
#include <string_view>

namespace {

std::wstring lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return text;
}

std::optional<int> numeric_prefix(const std::wstring& name) {
    int value = 0;
    bool saw_digit = false;
    for (wchar_t c : name) {
        if (!std::iswdigit(c)) {
            break;
        }
        saw_digit = true;
        value = value * 10 + (c - L'0');
    }
    if (!saw_digit) {
        return std::nullopt;
    }
    return value;
}

std::wstring trim(std::wstring text) {
    const auto first = std::find_if_not(text.begin(), text.end(), [](wchar_t c) { return std::iswspace(c); });
    const auto last = std::find_if_not(text.rbegin(), text.rend(), [](wchar_t c) { return std::iswspace(c); }).base();
    if (first >= last) {
        return L"";
    }
    return std::wstring(first, last);
}

std::wstring strip_numeric_prefix(std::wstring stem) {
    std::size_t i = 0;
    while (i < stem.size() && std::iswdigit(stem[i])) {
        ++i;
    }
    while (i < stem.size() && (stem[i] == L'.' || stem[i] == L' ' || stem[i] == L'-' || stem[i] == L'_' || stem[i] == L'、')) {
        ++i;
    }
    auto stripped = stem.substr(i);
    return trim(stripped).empty() ? trim(stem) : stripped;
}


int clamp_track_index(int index, std::size_t count) {
    if (count == 0) {
        return 0;
    }
    return std::clamp(index, 0, static_cast<int>(count) - 1);
}

std::string utf8_from_wide(const std::wstring& value) {
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.to_bytes(value);
    } catch (...) {
        std::string fallback;
        fallback.reserve(value.size());
        for (wchar_t c : value) {
            fallback.push_back(c >= 0 && c <= 0x7f ? static_cast<char>(c) : '?');
        }
        return fallback;
    }
}

std::wstring wide_from_utf8(std::string_view value) {
    try {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
        return converter.from_bytes(value.data(), value.data() + value.size());
    } catch (...) {
        return std::wstring(value.begin(), value.end());
    }
}

std::string xml_escape(std::string_view value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(c); break;
        }
    }
    return escaped;
}

std::string xml_escape_wide(const std::wstring& value) {
    return xml_escape(utf8_from_wide(value));
}

std::string xml_escape_path(const std::filesystem::path& path) {
    return xml_escape_wide(path.wstring());
}

std::string xml_unescape(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] == '&') {
            if (value.substr(i, 5) == "&amp;") { result.push_back('&'); i += 5; continue; }
            if (value.substr(i, 4) == "&lt;") { result.push_back('<'); i += 4; continue; }
            if (value.substr(i, 4) == "&gt;") { result.push_back('>'); i += 4; continue; }
            if (value.substr(i, 6) == "&quot;") { result.push_back('"'); i += 6; continue; }
            if (value.substr(i, 6) == "&apos;") { result.push_back('\''); i += 6; continue; }
        }
        result.push_back(value[i]);
        ++i;
    }
    return result;
}

std::optional<std::string> tag_attr(std::string_view tag, std::string_view name) {
    const std::string needle = std::string(name) + "=\"";
    const auto start = tag.find(needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_start = start + needle.size();
    const auto value_end = tag.find('"', value_start);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return xml_unescape(tag.substr(value_start, value_end - value_start));
}

int int_attr(std::string_view tag, std::string_view name, int fallback) {
    const auto value = tag_attr(tag, name);
    if (!value) {
        return fallback;
    }
    int parsed = fallback;
    const auto* first = value->data();
    const auto* last = value->data() + value->size();
    const auto result = std::from_chars(first, last, parsed);
    return result.ec == std::errc{} && result.ptr == last ? parsed : fallback;
}

std::size_t size_attr(std::string_view tag, std::string_view name, std::size_t fallback) {
    const int parsed = int_attr(tag, name, static_cast<int>(fallback));
    return parsed < 0 ? fallback : static_cast<std::size_t>(parsed);
}

bool bool_attr(std::string_view tag, std::string_view name, bool fallback) {
    return int_attr(tag, name, fallback ? 1 : 0) != 0;
}
void fill_blank_track_titles(std::vector<PlaylistTrack>& tracks) {
    for (auto& track : tracks) {
        if (track.title.empty()) {
            track.title = playlist_title_from_path(track.path);
        }
    }
}

} // namespace


PlaylistLibrary::PlaylistLibrary() : lists_{{L"100", {}, 0}} {}

std::size_t PlaylistLibrary::list_count() const {
    return lists_.size();
}

std::size_t PlaylistLibrary::active_index() const {
    return active_index_;
}

const std::wstring& PlaylistLibrary::active_name() const {
    return lists_[active_index_].name;
}

const std::vector<PlaylistTrack>& PlaylistLibrary::active_tracks() const {
    return lists_[active_index_].tracks;
}

std::vector<PlaylistTrack>& PlaylistLibrary::active_tracks() {
    return lists_[active_index_].tracks;
}

const std::vector<PlaylistDocument>& PlaylistLibrary::lists() const {
    return lists_;
}

int PlaylistLibrary::active_track_index() const {
    return lists_[active_index_].current_index;
}

void PlaylistLibrary::replace_active(std::vector<PlaylistTrack> tracks, std::wstring name, int current_index) {
    fill_blank_track_titles(tracks);
    lists_[active_index_].tracks = std::move(tracks);
    if (!name.empty()) {
        lists_[active_index_].name = std::move(name);
    }
    lists_[active_index_].current_index = clamp_track_index(current_index, lists_[active_index_].tracks.size());
}

void PlaylistLibrary::set_active_tracks(std::vector<PlaylistTrack> tracks) {
    fill_blank_track_titles(tracks);
    lists_[active_index_].tracks = std::move(tracks);
    lists_[active_index_].current_index = clamp_track_index(lists_[active_index_].current_index, lists_[active_index_].tracks.size());
}

void PlaylistLibrary::set_active_name(std::wstring name) {
    if (!name.empty()) {
        lists_[active_index_].name = std::move(name);
    }
}

void PlaylistLibrary::set_active_track_index(int index) {
    lists_[active_index_].current_index = clamp_track_index(index, lists_[active_index_].tracks.size());
}

void PlaylistLibrary::add_list(std::vector<PlaylistTrack> tracks, std::wstring name, int current_index) {
    fill_blank_track_titles(tracks);
    if (name.empty()) {
        name = next_default_name();
    }
    const int clamped_index = clamp_track_index(current_index, tracks.size());
    lists_.push_back(PlaylistDocument{std::move(name), std::move(tracks), clamped_index});
    active_index_ = lists_.size() - 1;
}

void PlaylistLibrary::new_list() {
    add_list({}, next_default_name(), 0);
}

void PlaylistLibrary::delete_active() {
    if (lists_.size() <= 1) {
        lists_[0].tracks.clear();
        lists_[0].current_index = 0;
        active_index_ = 0;
        return;
    }
    lists_.erase(lists_.begin() + static_cast<std::ptrdiff_t>(active_index_));
    if (active_index_ >= lists_.size()) {
        active_index_ = lists_.size() - 1;
    }
}

bool PlaylistLibrary::switch_to(std::size_t index) {
    if (index >= lists_.size()) {
        return false;
    }
    active_index_ = index;
    return true;
}


bool PlaylistLibrary::move_list(std::size_t from, std::size_t to) {
    if (from >= lists_.size() || to >= lists_.size()) {
        return false;
    }
    if (from == to) {
        return true;
    }
    auto moved = std::move(lists_[from]);
    lists_.erase(lists_.begin() + static_cast<std::ptrdiff_t>(from));
    lists_.insert(lists_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
    if (active_index_ == from) {
        active_index_ = to;
    } else if (from < active_index_ && active_index_ <= to) {
        --active_index_;
    } else if (to <= active_index_ && active_index_ < from) {
        ++active_index_;
    }
    return true;
}

std::wstring PlaylistLibrary::next_default_name() const {
    int next = 100;
    for (const auto& list : lists_) {
        try {
            std::size_t consumed = 0;
            const int value = std::stoi(list.name, &consumed);
            if (consumed == list.name.size()) {
                next = std::max(next, value + 1);
            }
        } catch (...) {
        }
    }
    return std::to_wstring(next);
}


std::string serialize_playlist_session(const PlaylistLibrary& library, const PlaylistSessionState& state) {
    std::ostringstream out;
    const std::size_t active = std::min(state.active_list_index, library.list_count() == 0 ? 0 : library.list_count() - 1);
    out << "<PlaylistSession ActiveList=\"" << active << "\" Position=\"" << std::max(0, state.resume_position_ms)
        << "\" Playing=\"" << (state.resume_playing ? 1 : 0) << "\">\n";
    const auto& lists = library.lists();
    for (const auto& list : lists) {
        out << "  <List Name=\"" << xml_escape_wide(list.name) << "\" CurrentIndex=\""
            << clamp_track_index(list.current_index, list.tracks.size()) << "\">\n";
        for (const auto& track : list.tracks) {
            out << "    <Track Path=\"" << xml_escape_path(track.path) << "\" Title=\"" << xml_escape_wide(track.title)
                << "\" Duration=\"" << std::max(0, track.duration_ms) << "\" />\n";
        }
        out << "  </List>\n";
    }
    out << "</PlaylistSession>\n";
    return out.str();
}

std::optional<PlaylistSessionRestore> parse_playlist_session(std::string_view xml) {
    const auto root_start = xml.find("<PlaylistSession");
    if (root_start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto root_tag_end = xml.find('>', root_start);
    if (root_tag_end == std::string_view::npos) {
        return std::nullopt;
    }
    const auto root_tag = xml.substr(root_start, root_tag_end - root_start + 1);
    PlaylistSessionRestore restore;
    restore.state.active_list_index = size_attr(root_tag, "ActiveList", 0);
    restore.state.resume_position_ms = std::max(0, int_attr(root_tag, "Position", 0));
    restore.state.resume_playing = bool_attr(root_tag, "Playing", false);

    bool saw_list = false;
    std::size_t search = root_tag_end + 1;
    while (true) {
        const auto list_start = xml.find("<List", search);
        if (list_start == std::string_view::npos) {
            break;
        }
        const auto list_tag_end = xml.find('>', list_start);
        const auto list_end = xml.find("</List>", list_tag_end == std::string_view::npos ? list_start : list_tag_end);
        if (list_tag_end == std::string_view::npos || list_end == std::string_view::npos) {
            break;
        }
        const auto list_tag = xml.substr(list_start, list_tag_end - list_start + 1);
        const auto name = wide_from_utf8(tag_attr(list_tag, "Name").value_or(""));
        const int current_index = int_attr(list_tag, "CurrentIndex", 0);
        std::vector<PlaylistTrack> tracks;
        std::size_t track_search = list_tag_end + 1;
        while (track_search < list_end) {
            const auto track_start = xml.find("<Track", track_search);
            if (track_start == std::string_view::npos || track_start >= list_end) {
                break;
            }
            const auto track_end = xml.find("/>", track_start);
            if (track_end == std::string_view::npos || track_end >= list_end) {
                break;
            }
            const auto track_tag = xml.substr(track_start, track_end - track_start + 2);
            const auto path = wide_from_utf8(tag_attr(track_tag, "Path").value_or(""));
            if (!path.empty() && is_supported_audio_file(std::filesystem::path(path))) {
                PlaylistTrack track{std::filesystem::path(path), wide_from_utf8(tag_attr(track_tag, "Title").value_or("")), int_attr(track_tag, "Duration", 0)};
                if (track.title.empty()) {
                    track.title = playlist_title_from_path(track.path);
                }
                tracks.push_back(std::move(track));
            }
            track_search = track_end + 2;
        }
        if (!saw_list) {
            restore.library.replace_active(std::move(tracks), name.empty() ? L"100" : name, current_index);
            saw_list = true;
        } else {
            restore.library.add_list(std::move(tracks), name, current_index);
        }
        search = list_end + 7;
    }
    if (!saw_list) {
        return std::nullopt;
    }
    if (!restore.library.switch_to(restore.state.active_list_index)) {
        restore.library.switch_to(0);
        restore.state.active_list_index = 0;
    }
    return restore;
}

bool is_supported_audio_file(const std::filesystem::path& path) {
    const auto ext = lower(path.extension().wstring());
    return ext == L".mp3" || ext == L".flac" || ext == L".wav" || ext == L".wma" || ext == L".ape" || ext == L".ogg" || ext == L".m4a";
}

std::wstring playlist_title_from_path(const std::filesystem::path& path) {
    auto title = strip_numeric_prefix(path.stem().wstring());
    for (auto& c : title) {
        if (c == L'－' || c == L'–' || c == L'—') {
            c = L'-';
        }
    }
    const auto dash = title.find(L'-');
    if (dash != std::wstring::npos) {
        const auto left = trim(title.substr(0, dash));
        const auto right = trim(title.substr(dash + 1));
        if (!left.empty() && !right.empty()) {
            return right + L" - " + left;
        }
    }
    return trim(title);
}

std::vector<PlaylistTrack> load_playlist_from_directory(const std::filesystem::path& directory) {
    std::vector<PlaylistTrack> tracks;
    std::error_code error;
    if (!std::filesystem::exists(directory, error)) {
        return tracks;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        if (error || !entry.is_regular_file() || !is_supported_audio_file(entry.path())) {
            continue;
        }
        tracks.push_back(PlaylistTrack{entry.path(), playlist_title_from_path(entry.path())});
    }
    std::sort(tracks.begin(), tracks.end(), [](const PlaylistTrack& a, const PlaylistTrack& b) {
        const auto an = numeric_prefix(a.path.filename().wstring());
        const auto bn = numeric_prefix(b.path.filename().wstring());
        if (an && bn && *an != *bn) {
            return *an < *bn;
        }
        if (an.has_value() != bn.has_value()) {
            return an.has_value();
        }
        return lower(a.path.filename().wstring()) < lower(b.path.filename().wstring());
    });
    return tracks;
}
int playlist_visible_row_count(int client_height) {
    constexpr int list_top = 56;
    constexpr int bottom_padding = 15;
    constexpr int row_height = 16;
    return std::max(0, (client_height - list_top - bottom_padding) / row_height);
}

std::wstring playlist_duration_text(int duration_ms) {
    if (duration_ms <= 0) {
        return L"";
    }
    const int total_seconds = duration_ms / 1000;
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    std::wstring text = std::to_wstring(minutes) + L":";
    if (seconds < 10) {
        text += L"0";
    }
    text += std::to_wstring(seconds);
    return text;
}
PlaylistToolbarAction playlist_toolbar_action_at(int x, int y) {
    if (y < 28 || y >= 43) {
        return PlaylistToolbarAction::None;
    }
    struct Hit {
        int left;
        int right;
        PlaylistToolbarAction action;
    };
    constexpr Hit hits[] = {
        {3, 35, PlaylistToolbarAction::Add},
        {35, 68, PlaylistToolbarAction::Delete},
        {68, 101, PlaylistToolbarAction::List},
        {101, 134, PlaylistToolbarAction::Sort},
        {134, 168, PlaylistToolbarAction::Find},
        {168, 202, PlaylistToolbarAction::Edit},
        {202, 241, PlaylistToolbarAction::Mode},
    };
    for (const auto& hit : hits) {
        if (x >= hit.left && x < hit.right) {
            return hit.action;
        }
    }
    return PlaylistToolbarAction::None;
}

std::optional<PlaylistListDragFeedback> playlist_list_drag_feedback_at(int source_index, int x, int y, int client_height, int divider_x, std::size_t list_count) {
    constexpr int list_top = 56;
    constexpr int row_height = 16;
    if (source_index < 0 || source_index >= static_cast<int>(list_count) || x < 0 || x >= divider_x || y < list_top) {
        return std::nullopt;
    }
    const int visible_rows = playlist_visible_row_count(client_height);
    const int visible_count = std::min(static_cast<int>(list_count), visible_rows);
    const int list_bottom = list_top + visible_count * row_height;
    if (y >= list_bottom) {
        return std::nullopt;
    }
    const int target_index = (y - list_top) / row_height;
    if (target_index < 0 || target_index >= static_cast<int>(list_count)) {
        return std::nullopt;
    }
    const int row_top = list_top + target_index * row_height;
    PlaylistListDragFeedback feedback;
    feedback.source_index = source_index;
    feedback.target_index = target_index;
    feedback.highlight_top = row_top;
    feedback.highlight_bottom = row_top + row_height;
    feedback.insert_y = target_index > source_index ? feedback.highlight_bottom : feedback.highlight_top;
    feedback.ghost_top = std::clamp(y - row_height / 2 + 1, list_top, list_bottom - row_height);
    feedback.ghost_bottom = feedback.ghost_top + row_height;
    return feedback;
}
PlaylistDropMode playlist_drop_mode_at(int x, int y, int client_height, int divider_x) {
    constexpr int list_top = 56;
    constexpr int row_height = 16;
    if (x < divider_x) {
        return PlaylistDropMode::CreateList;
    }
    const int visible_rows = playlist_visible_row_count(client_height);
    const int list_bottom = list_top + visible_rows * row_height;
    if (y >= list_top && y < list_bottom) {
        return PlaylistDropMode::AppendToList;
    }
    return PlaylistDropMode::CreateList;
}


SkinRect lyric_content_rect(SkinRect skin_rect, int client_width, int client_height) {
    SkinRect rect = skin_rect;
    rect.right = std::max(rect.left + 40, client_width - 6);
    rect.bottom = std::max(rect.top + 16, client_height - 4);
    return rect;
}

