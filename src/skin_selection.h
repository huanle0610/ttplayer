#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

std::wstring_view default_skin_filename();
std::filesystem::path packaged_skin_dir(const std::filesystem::path& app_root);
std::optional<std::filesystem::path> find_default_packaged_skin(const std::filesystem::path& skin_dir);

std::optional<std::filesystem::path> find_preferred_skin_with_core_assets(
    const std::filesystem::path& skin_dir,
    std::wstring_view preferred_name_fragment);
