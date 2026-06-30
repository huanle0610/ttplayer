#include "equalizer_model.h"

#include <algorithm>
#include <array>

namespace {

struct EqualizerPreset {
    std::wstring_view name;
    std::array<int, kEqualizerBandCount> bands;
};

constexpr std::array<int, kEqualizerBandCount> kFlatBands{};

constexpr std::array<EqualizerPreset, 13> kPresets{{
    {L"推荐配置", {8, 4, 0, -4, -12, -12, -4, 0, 6, 10}},
    {L"流行音乐", {6, 4, 0, -4, -6, -6, -4, 0, 4, 4}},
    {L"摇滚", {-4, 0, 4, 8, -4, -4, 0, 0, 8, 8}},
    {L"金属乐", {-12, 0, 0, 0, 0, 0, 8, 0, 8, 0}},
    {L"舞曲", {-4, 6, 8, 4, -4, -4, 0, 0, 8, 8}},
    {L"电子乐", {-12, 4, 8, -4, -4, -6, 0, 0, 12, 12}},
    {L"乡村音乐", {-4, 0, 0, 4, 4, 0, 0, 0, 8, 8}},
    {L"爵士乐", {0, 0, 0, 8, 8, 8, 0, 4, 6, 8}},
    {L"古典乐", {0, 12, 12, 8, 0, 0, 0, 0, 4, 4}},
    {L"布鲁斯", {-4, 0, 4, 4, 0, 0, 0, 0, -4, -6}},
    {L"怀旧音乐", {-6, 0, 4, 4, 0, 0, 0, 0, -6, -12}},
    {L"歌剧", {0, 0, 0, 8, 10, 6, 12, 6, 0, 0}},
    {L"语音", {-6, 0, 4, 4, 0, 0, 0, 0, -6, -12}},
}};

} // namespace

std::array<int, kEqualizerBandCount> equalizer_flat_bands() {
    return kFlatBands;
}

void reset_equalizer(EqualizerState& state) {
    state.bands = kFlatBands;
    state.preamp = 0;
    state.balance = 0;
    state.surround = 0;
    state.preset_name = L"自定义";
}

void apply_equalizer_preset(EqualizerState& state, std::wstring_view name) {
    if (name == L"自定义") {
        reset_equalizer(state);
        return;
    }
    for (const auto& preset : kPresets) {
        if (preset.name == name) {
            state.bands = preset.bands;
            state.preset_name = preset.name;
            state.enabled = true;
            return;
        }
    }
}

int equalizer_value_to_slider_y(int value, int top, int bottom, int thumb_height) {
    const int clamped = std::clamp(value, -12, 12);
    const int travel = std::max(1, bottom - top - thumb_height);
    const double normalized = static_cast<double>(12 - clamped) / 24.0;
    return top + static_cast<int>(normalized * travel);
}

int slider_y_to_equalizer_value(int y, int top, int bottom, int thumb_height) {
    const int travel = std::max(1, bottom - top - thumb_height);
    const int clamped_y = std::clamp(y, top, top + travel);
    const double normalized = static_cast<double>(clamped_y - top) / static_cast<double>(travel);
    return std::clamp(12 - static_cast<int>(normalized * 24.0 + 0.5), -12, 12);
}

int balance_slider_fill_width(int value, int width) {
    const int clamped_width = std::max(0, width);
    const int clamped_value = std::clamp(value, -12, 12);
    const double normalized = static_cast<double>(clamped_value + 12) / 24.0;
    return std::clamp(static_cast<int>(normalized * clamped_width + 0.5), 0, clamped_width);
}

int surround_slider_fill_width(int value, int width) {
    const int clamped_width = std::max(0, width);
    const int clamped_value = std::clamp(value, 0, 12);
    const double normalized = static_cast<double>(clamped_value) / 12.0;
    return std::clamp(static_cast<int>(normalized * clamped_width + 0.5), 0, clamped_width);
}
