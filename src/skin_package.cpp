#include "skin_package.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <limits>

namespace {

constexpr std::uint32_t kEndOfCentralDirectory = 0x06054b50;
constexpr std::uint32_t kCentralDirectoryHeader = 0x02014b50;
constexpr std::uint32_t kLocalFileHeader = 0x04034b50;
constexpr std::uint16_t kMethodStored = 0;
constexpr std::uint16_t kMethodDeflated = 8;
constexpr USHORT kCompressionFormatDeflate = 0x0002;
constexpr USHORT kCompressionEngineMaximum = 0x0100;

extern "C" __declspec(dllimport) LONG NTAPI RtlGetCompressionWorkSpaceSize(
    USHORT CompressionFormatAndEngine,
    PULONG CompressBufferWorkSpaceSize,
    PULONG CompressFragmentWorkSpaceSize);

extern "C" __declspec(dllimport) LONG NTAPI RtlDecompressBufferEx(
    USHORT CompressionFormat,
    PUCHAR UncompressedBuffer,
    ULONG UncompressedBufferSize,
    PUCHAR CompressedBuffer,
    ULONG CompressedBufferSize,
    PULONG FinalUncompressedSize,
    PVOID WorkSpace);

std::uint16_t read_u16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<std::uint32_t>(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
}

std::optional<std::vector<std::uint8_t>> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file && !data.empty()) {
        return std::nullopt;
    }
    return data;
}

std::optional<std::size_t> find_eocd(const std::vector<std::uint8_t>& data) {
    if (data.size() < 22) {
        return std::nullopt;
    }
    const auto max_comment = std::min<std::size_t>(data.size() - 22, std::numeric_limits<std::uint16_t>::max());
    const auto min_offset = data.size() - 22 - max_comment;
    for (std::size_t offset = data.size() - 22;; --offset) {
        if (read_u32(data, offset) == kEndOfCentralDirectory) {
            return offset;
        }
        if (offset == min_offset) {
            break;
        }
    }
    return std::nullopt;
}

std::string normalize_name(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

std::filesystem::path package_cache_dir(const std::filesystem::path& path) {
    const auto canonical = std::filesystem::absolute(path).wstring();
    const auto hash = std::hash<std::wstring>{}(canonical);
    return std::filesystem::temp_directory_path() / "ttplayer_skin_cache" / std::to_wstring(hash);
}


std::optional<std::vector<std::uint8_t>> inflate_deflate(const std::vector<std::uint8_t>& compressed, std::uint32_t output_size) {
    std::vector<std::uint8_t> output(output_size);
    ULONG workspace_a = 0;
    ULONG workspace_b = 0;
    LONG status = RtlGetCompressionWorkSpaceSize(
        kCompressionFormatDeflate | kCompressionEngineMaximum,
        &workspace_a,
        &workspace_b);
    if (status < 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> workspace(workspace_b);
    ULONG final_size = 0;
    status = RtlDecompressBufferEx(
        kCompressionFormatDeflate | kCompressionEngineMaximum,
        output.data(),
        static_cast<ULONG>(output.size()),
        const_cast<PUCHAR>(compressed.data()),
        static_cast<ULONG>(compressed.size()),
        &final_size,
        workspace.data());
    if (status < 0 || final_size != output_size) {
        return std::nullopt;
    }
    return output;
}

} // namespace

std::optional<SkinPackage> SkinPackage::open(const std::filesystem::path& path) {
    const auto data = read_file(path);
    if (!data) {
        return std::nullopt;
    }
    const auto eocd = find_eocd(*data);
    if (!eocd) {
        return std::nullopt;
    }

    const auto central_count = read_u16(*data, *eocd + 10);
    const auto central_offset = read_u32(*data, *eocd + 16);
    if (central_offset >= data->size()) {
        return std::nullopt;
    }

    SkinPackage package;
    package.path_ = path;
    std::size_t offset = central_offset;
    for (std::uint16_t i = 0; i < central_count; ++i) {
        if (offset + 46 > data->size() || read_u32(*data, offset) != kCentralDirectoryHeader) {
            return std::nullopt;
        }
        const auto method = read_u16(*data, offset + 10);
        const auto compressed_size = read_u32(*data, offset + 20);
        const auto uncompressed_size = read_u32(*data, offset + 24);
        const auto name_len = read_u16(*data, offset + 28);
        const auto extra_len = read_u16(*data, offset + 30);
        const auto comment_len = read_u16(*data, offset + 32);
        const auto local_offset = read_u32(*data, offset + 42);
        const auto name_offset = offset + 46;
        if (name_offset + name_len > data->size()) {
            return std::nullopt;
        }
        std::string name(reinterpret_cast<const char*>(data->data() + name_offset), name_len);
        package.entries_[normalize_name(std::move(name))] = Entry{method, compressed_size, uncompressed_size, local_offset};
        offset = name_offset + name_len + extra_len + comment_len;
    }
    return package;
}

bool SkinPackage::contains(const std::string& name) const {
    return entries_.find(normalize_name(name)) != entries_.end();
}

std::optional<std::vector<std::uint8_t>> SkinPackage::read_binary(const std::string& name) const {
    const auto entry_it = entries_.find(normalize_name(name));
    if (entry_it == entries_.end()) {
        return std::nullopt;
    }
    const auto data = read_file(path_);
    if (!data) {
        return std::nullopt;
    }
    const auto& entry = entry_it->second;
    const auto offset = static_cast<std::size_t>(entry.local_header_offset);
    if (offset + 30 > data->size() || read_u32(*data, offset) != kLocalFileHeader) {
        return std::nullopt;
    }
    const auto name_len = read_u16(*data, offset + 26);
    const auto extra_len = read_u16(*data, offset + 28);
    const auto body_offset = offset + 30 + name_len + extra_len;
    if (body_offset + entry.compressed_size > data->size()) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> compressed(data->begin() + static_cast<std::ptrdiff_t>(body_offset),
                                         data->begin() + static_cast<std::ptrdiff_t>(body_offset + entry.compressed_size));
    if (entry.method == kMethodStored) {
        if (compressed.size() != entry.uncompressed_size) {
            return std::nullopt;
        }
        return compressed;
    }
    if (entry.method == kMethodDeflated) {
        if (const auto inflated = inflate_deflate(compressed, entry.uncompressed_size)) {
            return inflated;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> SkinPackage::materialize(const std::string& name) const {
    if (!contains(name)) {
        return std::nullopt;
    }
    const auto normalized = normalize_name(name);
    const auto cache_dir = package_cache_dir(path_);
    auto output = cache_dir / std::filesystem::path(normalized);
    std::error_code error;
    if (std::filesystem::exists(output, error)) {
        return output;
    }

    const auto bytes = read_binary(normalized);
    if (!bytes) {
        return std::nullopt;
    }
    std::filesystem::create_directories(output.parent_path(), error);
    if (error) {
        return std::nullopt;
    }
    std::ofstream file(output, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::nullopt;
    }
    file.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
    if (!file) {
        return std::nullopt;
    }
    return output;
}
std::optional<std::string> SkinPackage::read_text(const std::string& name) const {
    const auto bytes = read_binary(name);
    if (!bytes) {
        return std::nullopt;
    }
    return std::string(bytes->begin(), bytes->end());
}



