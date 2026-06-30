#include "skin_definition.h"

#include "skin_package.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

std::optional<int> parse_int(std::string_view text) {
    text = trim(text);
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::optional<SkinRect> parse_rect(std::string_view text) {
    std::array<int, 4> values{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const auto comma = text.find(',', start);
        const auto end = comma == std::string_view::npos ? text.size() : comma;
        const auto value = parse_int(text.substr(start, end - start));
        if (!value) {
            return std::nullopt;
        }
        values[index] = *value;
        start = end + 1;
        if (index < values.size() - 1 && comma == std::string_view::npos) {
            return std::nullopt;
        }
    }
    if (start < text.size() + 1) {
        const auto previous_comma = text.rfind(',');
        if (previous_comma != std::string_view::npos && previous_comma + 1 < text.size()) {
            return SkinRect{values[0], values[1], values[2], values[3]};
        }
    }
    return SkinRect{values[0], values[1], values[2], values[3]};
}

std::optional<std::string> attribute(std::string_view tag, std::string_view name) {
    std::string needle(name);
    needle += "=\"";
    const auto start = tag.find(needle);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto value_start = start + needle.size();
    const auto value_end = tag.find('"', value_start);
    if (value_end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(tag.substr(value_start, value_end - value_start));
}

std::optional<std::string_view> opening_tag(std::string_view xml, std::string_view name) {
    std::size_t start = 0;
    const std::string needle = "<" + std::string(name);
    while (true) {
        start = xml.find(needle, start);
        if (start == std::string_view::npos) {
            return std::nullopt;
        }
        const auto after_name = start + needle.size();
        if (after_name < xml.size()) {
            const char ch = xml[after_name];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == '>' || ch == '/') {
                break;
            }
        }
        start = after_name;
    }
    const auto end = xml.find('>', start);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return xml.substr(start, end - start + 1);
}

std::optional<std::string_view> element_body(std::string_view xml, std::string_view name) {
    std::string open = "<";
    open += name;
    const auto start = xml.find(open);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto body_start = xml.find('>', start);
    if (body_start == std::string_view::npos) {
        return std::nullopt;
    }
    std::string close = "</";
    close += name;
    close += ">";
    const auto body_end = xml.find(close, body_start + 1);
    if (body_end == std::string_view::npos) {
        return std::nullopt;
    }
    return xml.substr(body_start + 1, body_end - body_start - 1);
}

std::optional<SkinControlDefinition> parse_control(std::string_view body, std::string_view name) {
    const auto tag = opening_tag(body, name);
    if (!tag) {
        return std::nullopt;
    }
    const auto position_text = attribute(*tag, "position");
    if (!position_text) {
        return std::nullopt;
    }
    const auto position = parse_rect(*position_text);
    if (!position) {
        return std::nullopt;
    }
    SkinControlDefinition control;
    control.position = *position;
    if (const auto image = attribute(*tag, "image")) {
        control.image = *image;
    }
    if (const auto thumb_image = attribute(*tag, "thumb_image")) {
        control.thumb_image = *thumb_image;
    }
    if (const auto fill_image = attribute(*tag, "fill_image")) {
        control.fill_image = *fill_image;
    }
    return control;
}

std::unordered_map<std::string, SkinControlDefinition> parse_controls(std::string_view xml, std::string_view window_name) {
    std::unordered_map<std::string, SkinControlDefinition> controls;
    const auto body = element_body(xml, window_name);
    if (!body) {
        return controls;
    }
    constexpr std::array<std::string_view, 28> names{
        "play", "pause", "stop", "prev", "next", "mute", "open", "lyric", "equalizer",
        "playlist", "minimize", "minimode", "exit", "progress", "volume", "visual", "icon", "info",
        "status", "led", "toolbar", "enabled", "reset", "profile", "balance", "surround", "preamp", "eqfactor"
    };
    for (const auto name : names) {
        if (const auto control = parse_control(*body, name)) {
            controls.emplace(std::string(name), *control);
        }
    }
    return controls;
}
std::optional<SkinWindowDefinition> parse_window(std::string_view xml, std::string_view name) {
    const auto tag = opening_tag(xml, name);
    if (!tag) {
        return std::nullopt;
    }
    const auto image = attribute(*tag, "image");
    if (!image || image->empty()) {
        return std::nullopt;
    }

    SkinWindowDefinition window;
    window.image = *image;
    if (const auto position = attribute(*tag, "position")) {
        window.position = parse_rect(*position);
    }
    if (const auto resize_rect = attribute(*tag, "resize_rect")) {
        window.resize_rect = parse_rect(*resize_rect);
    }
    window.controls = parse_controls(xml, name);
    return window;
}

std::uint16_t u16_le(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t u32_le(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
}

std::optional<SkinSize> bmp_size(const std::vector<std::uint8_t>& data) {
    if (data.size() < 26 || data[0] != 'B' || data[1] != 'M') {
        return std::nullopt;
    }
    const auto dib_size = u32_le(data, 14);
    if (dib_size < 12) {
        return std::nullopt;
    }
    if (dib_size == 12) {
        return SkinSize{static_cast<int>(u16_le(data, 18)), static_cast<int>(u16_le(data, 20))};
    }
    if (data.size() < 26) {
        return std::nullopt;
    }
    return SkinSize{static_cast<int>(u32_le(data, 18)), static_cast<int>(u32_le(data, 22))};
}

bool contains_point(const SkinRect& rect, int x, int y) {
    return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}
bool apply_bitmap_size(SkinWindowDefinition& window, const SkinPackage& package) {
    const auto bytes = package.read_binary(window.image);
    if (!bytes) {
        return false;
    }
    const auto size = bmp_size(*bytes);
    if (!size) {
        return false;
    }
    window.size = *size;
    return true;
}

} // namespace

std::optional<SkinDefinition> parse_skin_definition(const std::string& xml) {
    auto player = parse_window(xml, "player_window");
    auto lyrics = parse_window(xml, "lyric_window");
    auto playlist = parse_window(xml, "playlist_window");
    auto equalizer = parse_window(xml, "equalizer_window");
    if (!player || !lyrics || !playlist || !equalizer) {
        return std::nullopt;
    }
    return SkinDefinition{*player, *lyrics, *playlist, *equalizer};
}

std::optional<std::string> hit_test_control(const SkinWindowDefinition& window, int x, int y) {
    constexpr std::array<std::string_view, 28> priority{
        "exit", "minimize", "minimode", "play", "pause", "stop", "prev", "next", "open",
        "mute", "lyric", "equalizer", "playlist", "progress", "volume", "visual", "icon", "info",
        "status", "led", "toolbar", "enabled", "reset", "profile", "balance", "surround", "preamp", "eqfactor"
    };
    for (const auto name : priority) {
        const auto it = window.controls.find(std::string(name));
        if (it != window.controls.end() && contains_point(it->second.position, x, y)) {
            return it->first;
        }
    }
    return std::nullopt;
}
std::unordered_set<std::string> control_asset_names(const SkinWindowDefinition& window) {
    std::unordered_set<std::string> names;
    for (const auto& [_, control] : window.controls) {
        if (!control.image.empty()) {
            names.insert(control.image);
        }
        if (!control.thumb_image.empty()) {
            names.insert(control.thumb_image);
        }
        if (!control.fill_image.empty()) {
            names.insert(control.fill_image);
        }
    }
    return names;
}
SkinRect slider_fill_rect(const SkinControlDefinition& control, double fraction) {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    SkinRect rect = control.position;
    const int width = control.position.right - control.position.left;
    rect.right = control.position.left + static_cast<int>(width * clamped);
    return rect;
}

SkinRect slider_thumb_rect(const SkinControlDefinition& control, SkinSize thumb_size, double fraction) {
    const double clamped = std::clamp(fraction, 0.0, 1.0);
    const int track_width = control.position.right - control.position.left;
    const int travel = std::max(0, track_width - thumb_size.width);
    const int left = control.position.left + static_cast<int>(travel * clamped);
    return SkinRect{left, control.position.top, left + thumb_size.width, control.position.top + thumb_size.height};
}
bool apply_bitmap_sizes(SkinDefinition& definition, const SkinPackage& package) {
    return apply_bitmap_size(definition.player, package)
        && apply_bitmap_size(definition.lyrics, package)
        && apply_bitmap_size(definition.playlist, package)
        && apply_bitmap_size(definition.equalizer, package);
}






