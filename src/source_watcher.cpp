#include "source_watcher.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace {

constexpr DWORD kChangeFilter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
                                FILE_NOTIFY_CHANGE_CREATION;

[[nodiscard]] std::wstring lowercase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return text;
}

}  // namespace

struct SourceWatcher::Registration {
    std::string sourceId;
    std::filesystem::path directory;
    std::optional<std::wstring> fileNameFilter;
    HANDLE handle{INVALID_HANDLE_VALUE};
    OVERLAPPED overlapped{};
    std::array<std::byte, 64 * 1024> buffer{};
    std::optional<std::chrono::steady_clock::time_point> lastChange;
};

SourceWatcher::SourceWatcher() = default;

SourceWatcher::~SourceWatcher() {
    stop();
}

void SourceWatcher::start(const std::vector<ManualSource>& sources, int debounceSeconds, ChangeCallback callback) {
    stop();
    if (debounceSeconds <= 0) {
        throw std::invalid_argument("Source watcher debounce must be positive.");
    }

    debounce_ = std::chrono::seconds{debounceSeconds};
    callback_ = std::move(callback);
    const HANDLE completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (completionPort == nullptr) {
        throw std::runtime_error("Unable to create the Source watcher.");
    }
    completionPort_ = completionPort;

    for (const ManualSource& source : sources) {
        auto registration = std::make_unique<Registration>();
        registration->sourceId = source.id;
        registration->directory = source.kind == ManualSourceKind::folder ? source.path : source.path.parent_path();
        if (source.kind == ManualSourceKind::file) {
            registration->fileNameFilter = lowercase(source.path.filename().native());
        }

        registration->handle = CreateFileW(
            registration->directory.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (registration->handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        if (CreateIoCompletionPort(registration->handle, completionPort, reinterpret_cast<ULONG_PTR>(registration.get()),
                                   0) == nullptr) {
            CloseHandle(registration->handle);
            registration->handle = INVALID_HANDLE_VALUE;
            continue;
        }
        registrations_.push_back(std::move(registration));
    }

    isRunning_ = true;
    for (const std::unique_ptr<Registration>& registration : registrations_) {
        issueRead(*registration);
    }
    thread_ = std::thread([this] { watchLoop(); });
}

void SourceWatcher::stop() {
    isRunning_ = false;
    for (const std::unique_ptr<Registration>& registration : registrations_) {
        if (registration->handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(registration->handle, &registration->overlapped);
        }
    }
    if (completionPort_ != nullptr) {
        PostQueuedCompletionStatus(static_cast<HANDLE>(completionPort_), 0, 0, nullptr);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    for (const std::unique_ptr<Registration>& registration : registrations_) {
        if (registration->handle != INVALID_HANDLE_VALUE) {
            CloseHandle(registration->handle);
        }
    }
    registrations_.clear();
    if (completionPort_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(completionPort_));
        completionPort_ = nullptr;
    }
    callback_ = {};
}

std::size_t SourceWatcher::watchedSourceCount() const noexcept {
    return registrations_.size();
}

void SourceWatcher::watchLoop() {
    while (isRunning_) {
        DWORD transferredBytes{};
        ULONG_PTR completionKey{};
        OVERLAPPED* overlapped = nullptr;
        const BOOL completed = GetQueuedCompletionStatus(static_cast<HANDLE>(completionPort_), &transferredBytes,
                                                         &completionKey, &overlapped, 250);
        if (!isRunning_) {
            break;
        }

        if (completed != FALSE && completionKey != 0 && overlapped != nullptr) {
            auto& registration = *reinterpret_cast<Registration*>(completionKey);
            bool isRelevant = transferredBytes == 0 || !registration.fileNameFilter.has_value();
            std::size_t offset = 0;
            while (!isRelevant && offset < transferredBytes) {
                const auto* change = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(registration.buffer.data() + offset);
                const std::wstring changedName(change->FileName, change->FileNameLength / sizeof(wchar_t));
                isRelevant = lowercase(changedName) == *registration.fileNameFilter;
                if (change->NextEntryOffset == 0) {
                    break;
                }
                offset += change->NextEntryOffset;
            }
            if (isRelevant) {
                registration.lastChange = std::chrono::steady_clock::now();
            }
            issueRead(registration);
        }

        const auto now = std::chrono::steady_clock::now();
        for (const std::unique_ptr<Registration>& registration : registrations_) {
            if (!registration->lastChange.has_value() || now - *registration->lastChange < debounce_) {
                continue;
            }
            registration->lastChange.reset();
            try {
                callback_(registration->sourceId);
            } catch (...) {
            }
        }
    }
}

void SourceWatcher::issueRead(Registration& registration) {
    if (!isRunning_) {
        return;
    }
    registration.overlapped = {};
    DWORD ignoredBytes{};
    const BOOL started = ReadDirectoryChangesW(
        registration.handle, registration.buffer.data(), static_cast<DWORD>(registration.buffer.size()),
        registration.fileNameFilter.has_value() ? FALSE : TRUE, kChangeFilter, &ignoredBytes, &registration.overlapped,
        nullptr);
    if (started == FALSE) {
        registration.lastChange = std::chrono::steady_clock::now();
    }
}
