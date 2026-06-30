#pragma once

#include <array>
#include <string_view>

constexpr int kEqualizerBandCount = 10;

struct EqualizerState {
    bool enabled = false;
    std::array<int, kEqualizerBandCount> bands{};
    int preamp = 0;
    int balance = 0;
    int surround = 0;
    std::wstring_view preset_name = L"自定义";
};

std::array<int, kEqualizerBandCount> equalizer_flat_bands();
void reset_equalizer(EqualizerState& state);
void apply_equalizer_preset(EqualizerState& state, std::wstring_view name);
int equalizer_value_to_slider_y(int value, int top, int bottom, int thumb_height);
int slider_y_to_equalizer_value(int y, int top, int bottom, int thumb_height);
int balance_slider_fill_width(int value, int width);
int surround_slider_fill_width(int value, int width);
