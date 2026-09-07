#include "startup_registration.hpp"

#define NOMINMAX
#include <windows.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"BackItUpTool";

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

[[noreturn]] void throwRegistryError(const char* action, LSTATUS status) {
    throw std::runtime_error(std::string{"Unable to "} + action + ". Windows error " +
                             std::to_string(status) + '.');
}

[[nodiscard]] std::wstring executablePath() {
    std::vector<wchar_t> path(512);
    while (true) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) {
            throwRegistryError("find the application executable", GetLastError());
        }
        if (static_cast<std::size_t>(length) < path.size() - 1) {
            return std::wstring{path.data(), length};
        }
        path.resize(path.size() * 2);
    }
}

}  // namespace

bool isLaunchAtStartupEnabled() {
    DWORD valueType = 0;
    DWORD valueSize = 0;
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kRunKeyPath, kRunValueName,
                                        RRF_RT_REG_SZ, &valueType, nullptr, &valueSize);
    if (status == ERROR_FILE_NOT_FOUND) {
        return false;
    }
    if (status != ERROR_SUCCESS) {
        throwRegistryError("read the Windows startup setting", status);
    }
    return valueSize > sizeof(wchar_t);
}

void setLaunchAtStartupEnabled(bool isEnabled) {
    HKEY rawKey = nullptr;
    const LSTATUS openStatus = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0,
                                               KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &rawKey, nullptr);
    if (openStatus != ERROR_SUCCESS) {
        throwRegistryError("open the Windows startup settings", openStatus);
    }
    const RegistryKey key{rawKey};

    if (!isEnabled) {
        const LSTATUS deleteStatus = RegDeleteValueW(key.get(), kRunValueName);
        if (deleteStatus != ERROR_SUCCESS && deleteStatus != ERROR_FILE_NOT_FOUND) {
            throwRegistryError("disable launch at Windows startup", deleteStatus);
        }
        return;
    }

    const std::wstring command = L"\"" + executablePath() + L"\"";
    const DWORD commandBytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS writeStatus = RegSetValueExW(key.get(), kRunValueName, 0, REG_SZ,
                                               reinterpret_cast<const BYTE*>(command.c_str()), commandBytes);
    if (writeStatus != ERROR_SUCCESS) {
        throwRegistryError("enable launch at Windows startup", writeStatus);
    }
}
