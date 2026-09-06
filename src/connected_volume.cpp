#include "connected_volume.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string_view>

namespace {

[[nodiscard]] std::string wideToUtf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }

    const int requiredBytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (requiredBytes <= 0) {
        throw std::runtime_error("Unable to read a connected drive name.");
    }

    std::string result(static_cast<std::size_t>(requiredBytes), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(),
                        requiredBytes, nullptr, nullptr);
    return result;
}

}  // namespace

std::vector<ConnectedVolume> findConnectedRemovableVolumes() {
    const DWORD driveMask = GetLogicalDrives();
    if (driveMask == 0) {
        throw std::runtime_error("Unable to list connected drives.");
    }

    std::vector<ConnectedVolume> volumes;
    for (int driveIndex = 0; driveIndex < 26; ++driveIndex) {
        if ((driveMask & (1UL << driveIndex)) == 0) {
            continue;
        }

        std::array<wchar_t, 4> root{static_cast<wchar_t>(L'A' + driveIndex), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root.data()) != DRIVE_REMOVABLE) {
            continue;
        }

        std::array<wchar_t, MAX_PATH + 1> label{};
        std::array<wchar_t, MAX_PATH + 1> fileSystem{};
        DWORD serial{};
        if (GetVolumeInformationW(root.data(), label.data(), static_cast<DWORD>(label.size()), &serial, nullptr,
                                  nullptr, fileSystem.data(), static_cast<DWORD>(fileSystem.size())) == FALSE) {
            continue;
        }

        ULARGE_INTEGER freeBytes{};
        ULARGE_INTEGER totalBytes{};
        if (GetDiskFreeSpaceExW(root.data(), nullptr, &totalBytes, &freeBytes) == FALSE) {
            continue;
        }

        volumes.push_back(ConnectedVolume{
            std::filesystem::path{root.data()},
            wideToUtf8(label.data()),
            wideToUtf8(fileSystem.data()),
            serial,
            totalBytes.QuadPart,
            freeBytes.QuadPart,
        });
    }

    std::ranges::sort(volumes, {}, &ConnectedVolume::root);
    return volumes;
}
