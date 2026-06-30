#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

std::optional<std::filesystem::path> find_preferred_skin_with_core_assets(
    const std::filesystem::path& skin_dir,
    std::wstring_view preferred_name_fragment);
