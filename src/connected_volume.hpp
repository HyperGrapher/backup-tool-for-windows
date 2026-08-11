#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct ConnectedVolume {
    std::filesystem::path root;
    std::string label;
    std::string fileSystem;
    std::uint32_t serial{};
    std::uint64_t totalBytes{};
    std::uint64_t freeBytes{};

    bool operator==(const ConnectedVolume&) const = default;
};

[[nodiscard]] std::vector<ConnectedVolume> findConnectedRemovableVolumes();
[[nodiscard]] std::optional<ConnectedVolume> findConnectedRemovableVolume(std::uint32_t serial);
