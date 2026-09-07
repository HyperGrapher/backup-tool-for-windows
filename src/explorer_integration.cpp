#include "explorer_integration.hpp"

#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "explorer_overlay_contract.hpp"

namespace {

constexpr wchar_t kClassesRoot[] = L"Software\\Classes\\CLSID\\";
constexpr wchar_t kOverlayHandlersRoot[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\ShellIconOverlayIdentifiers\\";

class RegistryKey final {
public:
    explicit RegistryKey(HKEY key) : key_(key) {}

    ~RegistryKey() {
        if (key_ != nullptr) {
            RegCloseKey(key_);
        }
    }

    RegistryKey(const RegistryKey&) = delete;
    RegistryKey& operator=(const RegistryKey&) = delete;

    [[nodiscard]] HKEY get() const noexcept {
        return key_;
    }

private:
    HKEY key_{};
};

[[noreturn]] void throwWindowsError(const char* action, LSTATUS status) {
    throw std::runtime_error(std::string{"Unable to "} + action + ". Windows error " +
                             std::to_string(status) + '.');
}

void setRegistryString(HKEY parent, const std::wstring& subkey, const wchar_t* valueName,
                       const std::wstring& value) {
    HKEY rawKey = nullptr;
    const LSTATUS createStatus = RegCreateKeyExW(parent, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
                                                 nullptr, &rawKey, nullptr);
    if (createStatus != ERROR_SUCCESS) {
        throwWindowsError("register the Explorer watched-folder badge", createStatus);
    }
    const RegistryKey key{rawKey};
    const DWORD valueBytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS writeStatus = RegSetValueExW(key.get(), valueName, 0, REG_SZ,
                                               reinterpret_cast<const BYTE*>(value.c_str()), valueBytes);
    if (writeStatus != ERROR_SUCCESS) {
        throwWindowsError("write the Explorer watched-folder badge registration", writeStatus);
    }
}

[[nodiscard]] std::filesystem::path executablePath() {
    std::vector<wchar_t> path(512);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throwWindowsError("find the application executable", GetLastError());
        }
        if (static_cast<std::size_t>(length) < path.size() - 1) {
            return std::filesystem::path{std::wstring{path.data(), length}};
        }
        path.resize(path.size() * 2);
    }
}

[[nodiscard]] std::wstring registryString(HKEY parent, const std::wstring& subkey) {
    DWORD valueSize = 0;
    const LSTATUS sizeStatus = RegGetValueW(parent, subkey.c_str(), nullptr, RRF_RT_REG_SZ,
                                            nullptr, nullptr, &valueSize);
    if (sizeStatus == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (sizeStatus != ERROR_SUCCESS) {
        throwWindowsError("read the Explorer watched-folder badge registration", sizeStatus);
    }
    std::wstring value(valueSize / sizeof(wchar_t), L'\0');
    const LSTATUS readStatus = RegGetValueW(parent, subkey.c_str(), nullptr, RRF_RT_REG_SZ,
                                            nullptr, value.data(), &valueSize);
    if (readStatus != ERROR_SUCCESS) {
        throwWindowsError("read the Explorer watched-folder badge registration", readStatus);
    }
    value.resize(std::char_traits<wchar_t>::length(value.c_str()));
    return value;
}

void writeUint16(std::ofstream& output, std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void writeUint32(std::ofstream& output, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void createOverlayIcon(const std::filesystem::path& iconPath) {
    constexpr int kSize = 32;
    constexpr std::uint32_t kPixelBytes = kSize * kSize * 4;
    constexpr std::uint32_t kMaskBytes = (kSize / 8) * kSize;
    constexpr std::uint32_t kImageBytes = 40 + kPixelBytes + kMaskBytes;

    std::ofstream output{iconPath, std::ios::binary | std::ios::trunc};
    if (!output) {
        throw std::runtime_error("Unable to create the Explorer watched-folder badge icon.");
    }

    writeUint16(output, 0);
    writeUint16(output, 1);
    writeUint16(output, 1);
    output.put(static_cast<char>(kSize));
    output.put(static_cast<char>(kSize));
    output.put(0);
    output.put(0);
    writeUint16(output, 1);
    writeUint16(output, 32);
    writeUint32(output, kImageBytes);
    writeUint32(output, 22);

    writeUint32(output, 40);
    writeUint32(output, kSize);
    writeUint32(output, kSize * 2);
    writeUint16(output, 1);
    writeUint16(output, 32);
    writeUint32(output, 0);
    writeUint32(output, kPixelBytes);
    writeUint32(output, 0);
    writeUint32(output, 0);
    writeUint32(output, 0);
    writeUint32(output, 0);

    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int dx = x - 15;
            const int dy = y - 15;
            const int distanceSquared = dx * dx + dy * dy;
            if (distanceSquared <= 144) {
                const bool isBorder = distanceSquared >= 100;
                output.put(static_cast<char>(isBorder ? 30 : 72));
                output.put(static_cast<char>(isBorder ? 92 : 190));
                output.put(static_cast<char>(isBorder ? 18 : 52));
                output.put(static_cast<char>(255));
            } else {
                writeUint32(output, 0);
            }
        }
    }
    std::array<std::uint8_t, kMaskBytes> mask{};
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const int dx = x - 15;
            const int dy = y - 15;
            if (dx * dx + dy * dy > 144) {
                const std::size_t byteIndex = static_cast<std::size_t>(y * (kSize / 8) + x / 8);
                mask[byteIndex] |= static_cast<std::uint8_t>(0x80U >> (x % 8));
            }
        }
    }
    output.write(reinterpret_cast<const char*>(mask.data()), static_cast<std::streamsize>(mask.size()));
    output.flush();
    if (!output) {
        throw std::runtime_error("Unable to write the Explorer watched-folder badge icon.");
    }
}

[[nodiscard]] std::filesystem::path installOverlayDll(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path sourcePath =
        executablePath().parent_path() / ExplorerOverlayContract::kDllName;
    if (!std::filesystem::is_regular_file(sourcePath)) {
        throw std::runtime_error("The Explorer watched-folder badge component is missing.");
    }

    const auto writeStamp = std::filesystem::last_write_time(sourcePath).time_since_epoch().count();
    const std::filesystem::path shellDirectory = dataDirectory / L"shell";
    std::filesystem::create_directories(shellDirectory);
    const std::filesystem::path installedPath =
        shellDirectory / (L"BackItUpOverlay-" + std::to_wstring(writeStamp) + L".dll");
    if (!std::filesystem::exists(installedPath)) {
        std::filesystem::copy_file(sourcePath, installedPath);
    }
    return installedPath;
}

void registerMachineWide(const std::filesystem::path& installedDll) {
    const std::wstring classKey = std::wstring{kClassesRoot} + ExplorerOverlayContract::kClassId;
    setRegistryString(HKEY_LOCAL_MACHINE, classKey, nullptr, L"BackItUpTool watched-folder badge");
    setRegistryString(HKEY_LOCAL_MACHINE, classKey + L"\\InprocServer32", nullptr, installedDll.native());
    setRegistryString(HKEY_LOCAL_MACHINE, classKey + L"\\InprocServer32", L"ThreadingModel", L"Apartment");
    setRegistryString(HKEY_LOCAL_MACHINE,
                      std::wstring{kOverlayHandlersRoot} + ExplorerOverlayContract::kHandlerName,
                      nullptr, ExplorerOverlayContract::kClassId);
}

[[nodiscard]] bool isMachineRegistrationCurrent(const std::filesystem::path& installedDll) {
    const std::wstring serverKey = std::wstring{kClassesRoot} + ExplorerOverlayContract::kClassId +
                                   L"\\InprocServer32";
    const std::wstring handlerKey =
        std::wstring{kOverlayHandlersRoot} + ExplorerOverlayContract::kHandlerName;
    return registryString(HKEY_LOCAL_MACHINE, serverKey) == installedDll.native() &&
           registryString(HKEY_LOCAL_MACHINE, handlerKey) == ExplorerOverlayContract::kClassId;
}

void requestElevatedRegistration() {
    const std::wstring applicationPath = executablePath().native();
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = L"runas";
    launch.lpFile = applicationPath.c_str();
    launch.lpParameters = L"--register-explorer-overlay";
    launch.nShow = SW_HIDE;
    if (ShellExecuteExW(&launch) == FALSE) {
        throwWindowsError("request permission to install the Explorer watched-folder badge", GetLastError());
    }
    const HANDLE process = launch.hProcess;
    WaitForSingleObject(process, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
    if (exitCode != 0) {
        throw std::runtime_error("The Explorer watched-folder badge was not installed.");
    }
}

}  // namespace

void ensureExplorerIntegration(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path installedDll = installOverlayDll(dataDirectory);
    const std::filesystem::path iconPath = dataDirectory / ExplorerOverlayContract::kIconName;
    if (!std::filesystem::is_regular_file(iconPath)) {
        createOverlayIcon(iconPath);
    }

    if (!isMachineRegistrationCurrent(installedDll)) {
        requestElevatedRegistration();
    }
    refreshExplorerOverlays();
}

void registerExplorerIntegrationMachineWide(const std::filesystem::path& dataDirectory) {
    const std::filesystem::path installedDll = installOverlayDll(dataDirectory);
    const std::filesystem::path iconPath = dataDirectory / ExplorerOverlayContract::kIconName;
    if (!std::filesystem::is_regular_file(iconPath)) {
        createOverlayIcon(iconPath);
    }
    registerMachineWide(installedDll);
}

void refreshExplorerOverlays() {
    SHLoadNonloadedIconOverlayIdentifiers();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
