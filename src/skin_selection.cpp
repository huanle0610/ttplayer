#include "skin_selection.h"

#include "skin_package.h"
#include "skin_definition.h"

#include <vector>

namespace {

bool has_core_assets(const std::filesystem::path& path) {
    const auto skin = SkinPackage::open(path);
    if (!skin || !skin->contains("Skin.xml")) {
        return false;
    }
    const auto xml = skin->read_text("Skin.xml");
    if (!xml) {
        return false;
    }
    auto definition = parse_skin_definition(*xml);
    return definition.has_value() && apply_bitmap_sizes(*definition, *skin);
}

} // namespace

std::wstring_view default_skin_filename() {
    return L"3、TT-07_随身听 (蓝+黑).skn";
}

std::filesystem::path packaged_skin_dir(const std::filesystem::path& app_root) {
    return app_root / "skins";
}

std::optional<std::filesystem::path> find_default_packaged_skin(const std::filesystem::path& skin_dir) {
    const auto skin_path = skin_dir / default_skin_filename();
    if (!has_core_assets(skin_path)) {
        return std::nullopt;
    }
    return skin_path;
}
std::optional<std::filesystem::path> find_preferred_skin_with_core_assets(
    const std::filesystem::path& skin_dir,
    std::wstring_view preferred_name_fragment) {
    std::error_code error;
    if (!std::filesystem::exists(skin_dir, error)) {
        return std::nullopt;
    }

    std::optional<std::filesystem::path> fallback;
    for (const auto& entry : std::filesystem::directory_iterator(skin_dir, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != L".skn") {
            continue;
        }
        if (!has_core_assets(entry.path())) {
            continue;
        }
        if (!fallback) {
            fallback = entry.path();
        }
        if (entry.path().filename().wstring().find(preferred_name_fragment) != std::wstring::npos) {
            return entry.path();
        }
    }
    return fallback;
}


