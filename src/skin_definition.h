#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <unordered_map>

class SkinPackage;

struct SkinSize {
    int width{};
    int height{};
};

struct SkinRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

struct SkinControlDefinition {
    SkinRect position;
    std::string image;
    std::string thumb_image;
    std::string fill_image;
};

struct SkinWindowDefinition {
    std::string image;
    SkinSize size;
    std::optional<SkinRect> position;
    std::optional<SkinRect> resize_rect;
    std::unordered_map<std::string, SkinControlDefinition> controls;
};

struct SkinDefinition {
    SkinWindowDefinition player;
    SkinWindowDefinition lyrics;
    SkinWindowDefinition playlist;
    SkinWindowDefinition equalizer;
};

std::optional<SkinDefinition> parse_skin_definition(const std::string& xml);
bool apply_bitmap_sizes(SkinDefinition& definition, const SkinPackage& package);
std::optional<std::string> hit_test_control(const SkinWindowDefinition& window, int x, int y);
std::unordered_set<std::string> control_asset_names(const SkinWindowDefinition& window);
SkinRect slider_fill_rect(const SkinControlDefinition& control, double fraction);
SkinRect slider_thumb_rect(const SkinControlDefinition& control, SkinSize thumb_size, double fraction);




