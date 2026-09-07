#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shlobj_core.h>

#include <algorithm>
#include <atomic>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "explorer_overlay_contract.hpp"

namespace {

constexpr CLSID kOverlayClassId{
    0x5a71b728,
    0x569e,
    0x4c74,
    {0xb3, 0x68, 0x1d, 0xf3, 0x5e, 0x42, 0x9b, 0xf2},
};

std::atomic_long objectCount{};
std::atomic_long serverLockCount{};

[[nodiscard]] bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path normalizedPath(const std::filesystem::path& path) {
    return std::filesystem::absolute(path).lexically_normal();
}

[[nodiscard]] bool pathsEqual(const std::filesystem::path& left, const std::filesystem::path& right) {
    const std::filesystem::path normalizedLeft = normalizedPath(left);
    const std::filesystem::path normalizedRight = normalizedPath(right);
    auto leftComponent = normalizedLeft.begin();
    auto rightComponent = normalizedRight.begin();
    while (leftComponent != normalizedLeft.end() && rightComponent != normalizedRight.end()) {
        if (!equalsIgnoreCase(leftComponent->native(), rightComponent->native())) {
            return false;
        }
        ++leftComponent;
        ++rightComponent;
    }
    return leftComponent == normalizedLeft.end() && rightComponent == normalizedRight.end();
}

[[nodiscard]] bool isInside(const std::filesystem::path& root, const std::filesystem::path& path) {
    const std::filesystem::path normalizedRoot = normalizedPath(root);
    const std::filesystem::path normalizedChild = normalizedPath(path);
    auto rootComponent = normalizedRoot.begin();
    auto childComponent = normalizedChild.begin();
    while (rootComponent != normalizedRoot.end() && childComponent != normalizedChild.end()) {
        if (!equalsIgnoreCase(rootComponent->native(), childComponent->native())) {
            return false;
        }
        ++rootComponent;
        ++childComponent;
    }
    return rootComponent == normalizedRoot.end() && childComponent != normalizedChild.end();
}

[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view text) {
    std::u8string bytes;
    bytes.reserve(text.size());
    for (const char byte : text) {
        bytes.push_back(static_cast<char8_t>(static_cast<unsigned char>(byte)));
    }
    return std::filesystem::path{bytes};
}

[[nodiscard]] std::filesystem::path applicationDataDirectory() {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &rawPath))) {
        return {};
    }
    const std::filesystem::path result = std::filesystem::path{rawPath} / L"BackItUpTool";
    CoTaskMemFree(rawPath);
    return result;
}

[[nodiscard]] bool hasWatchMarker(const std::filesystem::path& folder) {
    std::error_code error;
    return std::filesystem::is_regular_file(folder / L".backup-watch", error) && !error;
}

[[nodiscard]] bool hasWatchedAncestor(const std::filesystem::path& root,
                                      const std::filesystem::path& folder) {
    std::filesystem::path ancestor = folder.parent_path();
    while (isInside(root, ancestor)) {
        if (hasWatchMarker(ancestor)) {
            return true;
        }
        const std::filesystem::path parent = ancestor.parent_path();
        if (parent == ancestor) {
            break;
        }
        ancestor = parent;
    }
    return false;
}

class WatchedFolderIndex final {
public:
    [[nodiscard]] bool contains(const std::filesystem::path& folder) {
        std::scoped_lock lock(mutex_);
        refreshIfNeeded();
        if (std::ranges::any_of(manualFolders_, [&](const std::filesystem::path& manualFolder) {
                return pathsEqual(manualFolder, folder);
            })) {
            return true;
        }
        if (!hasWatchMarker(folder)) {
            return false;
        }
        return std::ranges::any_of(projectRoots_, [&](const std::filesystem::path& root) {
            return isInside(root, folder) && !hasWatchedAncestor(root, folder);
        });
    }

private:
    void refreshIfNeeded() {
        const std::filesystem::path configPath = applicationDataDirectory() / L"config.json";
        std::error_code writeTimeError;
        const auto writeTime = std::filesystem::last_write_time(configPath, writeTimeError);
        if (writeTimeError) {
            manualFolders_.clear();
            projectRoots_.clear();
            configWriteTime_.reset();
            return;
        }
        if (configWriteTime_.has_value() && *configWriteTime_ == writeTime) {
            return;
        }

        try {
            std::ifstream input{configPath, std::ios::binary};
            const nlohmann::json config = nlohmann::json::parse(input);
            std::vector<std::filesystem::path> manualFolders;
            std::vector<std::filesystem::path> projectRoots;
            for (const nlohmann::json& source : config.value("manualSources", nlohmann::json::array())) {
                if (source.value("kind", std::string{}) == "folder") {
                    manualFolders.push_back(pathFromUtf8(source.at("path").get<std::string>()));
                }
            }
            for (const nlohmann::json& root : config.value("projectsRoots", nlohmann::json::array())) {
                projectRoots.push_back(pathFromUtf8(root.at("path").get<std::string>()));
            }
            manualFolders_ = std::move(manualFolders);
            projectRoots_ = std::move(projectRoots);
            configWriteTime_ = writeTime;
        } catch (...) {
            manualFolders_.clear();
            projectRoots_.clear();
            configWriteTime_.reset();
        }
    }

    std::mutex mutex_;
    std::vector<std::filesystem::path> manualFolders_;
    std::vector<std::filesystem::path> projectRoots_;
    std::optional<std::filesystem::file_time_type> configWriteTime_;
};

WatchedFolderIndex watchedFolders;

class WatchedOverlay final : public IShellIconOverlayIdentifier {
public:
    WatchedOverlay() {
        ++objectCount;
    }

    ~WatchedOverlay() {
        --objectCount;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (IsEqualIID(interfaceId, IID_IUnknown) ||
            IsEqualIID(interfaceId, IID_IShellIconOverlayIdentifier)) {
            *result = static_cast<IShellIconOverlayIdentifier*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++referenceCount_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = static_cast<ULONG>(--referenceCount_);
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE IsMemberOf(LPCWSTR path, DWORD attributes) override {
        if (path == nullptr || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return S_FALSE;
        }
        try {
            return watchedFolders.contains(path) ? S_OK : S_FALSE;
        } catch (...) {
            return S_FALSE;
        }
    }

    HRESULT STDMETHODCALLTYPE GetOverlayInfo(LPWSTR iconFile, int maximumCharacters, int* iconIndex,
                                             DWORD* flags) override {
        if (iconFile == nullptr || maximumCharacters <= 0 || iconIndex == nullptr || flags == nullptr) {
            return E_INVALIDARG;
        }
        const std::wstring iconPath =
            (applicationDataDirectory() / ExplorerOverlayContract::kIconName).native();
        if (iconPath.size() + 1 > static_cast<std::size_t>(maximumCharacters)) {
            return E_FAIL;
        }
        std::copy(iconPath.begin(), iconPath.end(), iconFile);
        iconFile[iconPath.size()] = L'\0';
        *iconIndex = 0;
        *flags = ISIOI_ICONFILE | ISIOI_ICONINDEX;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPriority(int* priority) override {
        if (priority == nullptr) {
            return E_POINTER;
        }
        *priority = 0;
        return S_OK;
    }

private:
    std::atomic_ulong referenceCount_{1};
};

class OverlayClassFactory final : public IClassFactory {
public:
    OverlayClassFactory() {
        ++objectCount;
    }

    ~OverlayClassFactory() {
        --objectCount;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID interfaceId, void** result) override {
        if (result == nullptr) {
            return E_POINTER;
        }
        *result = nullptr;
        if (IsEqualIID(interfaceId, IID_IUnknown) || IsEqualIID(interfaceId, IID_IClassFactory)) {
            *result = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return static_cast<ULONG>(++referenceCount_);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = static_cast<ULONG>(--referenceCount_);
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID interfaceId, void** result) override {
        if (outer != nullptr) {
            return CLASS_E_NOAGGREGATION;
        }
        auto* overlay = new WatchedOverlay;
        const HRESULT queryResult = overlay->QueryInterface(interfaceId, result);
        overlay->Release();
        return queryResult;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL shouldLock) override {
        if (shouldLock != FALSE) {
            ++serverLockCount;
        } else {
            --serverLockCount;
        }
        return S_OK;
    }

private:
    std::atomic_ulong referenceCount_{1};
};

}  // namespace

STDAPI DllGetClassObject(REFCLSID classId, REFIID interfaceId, void** result) {
    if (!IsEqualCLSID(classId, kOverlayClassId)) {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    auto* factory = new OverlayClassFactory;
    const HRESULT queryResult = factory->QueryInterface(interfaceId, result);
    factory->Release();
    return queryResult;
}

STDAPI DllCanUnloadNow() {
    return objectCount == 0 && serverLockCount == 0 ? S_OK : S_FALSE;
}
