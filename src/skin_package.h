#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class SkinPackage {
public:
    static std::optional<SkinPackage> open(const std::filesystem::path& path);

    bool contains(const std::string& name) const;
    std::optional<std::vector<std::uint8_t>> read_binary(const std::string& name) const;
    std::optional<std::string> read_text(const std::string& name) const;
    std::optional<std::filesystem::path> materialize(const std::string& name) const;

private:
    struct Entry {
        std::uint16_t method{};
        std::uint32_t compressed_size{};
        std::uint32_t uncompressed_size{};
        std::uint32_t local_header_offset{};
    };

    std::filesystem::path path_;
    std::unordered_map<std::string, Entry> entries_;
};
