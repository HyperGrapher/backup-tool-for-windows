#include "source_watcher.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <optional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
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

struct LinkedFileState {
    std::filesystem::file_time_type modified;
    std::uintmax_t size{};
    bool operator==(const LinkedFileState&) const = default;
};
using LinkedSnapshot = std::map<std::filesystem::path, LinkedFileState>;

[[nodiscard]] std::vector<std::filesystem::path> findSymbolicLinks(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> links;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator{
             root, std::filesystem::directory_options::skip_permission_denied, error}, end;
         !error && iterator != end; iterator.increment(error)) {
        if (iterator->is_symlink(error) && !error) {
            links.push_back(iterator->path());
            iterator.disable_recursion_pending();
        }
    }
    return links;
}

void snapshotLinkedPath(const std::filesystem::path& path, const std::filesystem::path& sourceRoot,
                        const SourceWatchTarget::ChangeFilter& filter, std::set<std::filesystem::path>& visited,
                        LinkedSnapshot& snapshot) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error || !visited.insert(canonical).second) {
        return;
    }
    if (std::filesystem::is_directory(path, error) && !error) {
        for (std::filesystem::directory_iterator iterator{
                 path, std::filesystem::directory_options::skip_permission_denied, error}, end;
             !error && iterator != end; iterator.increment(error)) {
            snapshotLinkedPath(iterator->path(), sourceRoot, filter, visited, snapshot);
        }
        return;
    }
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return;
    }
    const auto relative = path.lexically_relative(sourceRoot);
    if (filter && !filter(relative)) {
        return;
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (!error) {
        snapshot.emplace(relative, LinkedFileState{modified, size});
    }
}

[[nodiscard]] LinkedSnapshot snapshotLinks(const std::vector<std::filesystem::path>& links,
                                           const std::filesystem::path& sourceRoot,
                                           const SourceWatchTarget::ChangeFilter& filter) {
    LinkedSnapshot snapshot;
    for (const auto& link : links) {
        std::set<std::filesystem::path> visited;
        std::error_code error;
        visited.insert(std::filesystem::weakly_canonical(sourceRoot, error));
        snapshotLinkedPath(link, sourceRoot, filter, visited, snapshot);
    }
    return snapshot;
}

}  // namespace

struct SourceWatcher::Registration {
    std::string sourceId;
    std::filesystem::path directory;
    bool isRecursive{true};
    SourceWatchTarget::ChangeFilter isRelevantChange;
    bool followSymbolicLinks{};
    std::vector<std::filesystem::path> symbolicLinks;
    LinkedSnapshot linkedSnapshot;
    std::chrono::steady_clock::time_point nextLinkCheck;
    HANDLE handle{INVALID_HANDLE_VALUE};
    OVERLAPPED overlapped{};
    std::array<std::byte, 64 * 1024> buffer{};
    std::optional<std::chrono::steady_clock::time_point> lastChange;
};

SourceWatcher::SourceWatcher() = default;

SourceWatcher::~SourceWatcher() {
    stop();
}

SourceWatchTarget makeSourceWatchTarget(const ManualSource& source) {
    SourceWatchTarget target;
    target.sourceId = source.id;
    target.directory = source.kind == ManualSourceKind::folder ? source.path : source.path.parent_path();
    target.isRecursive = source.kind == ManualSourceKind::folder;
    target.followSymbolicLinks = target.isRecursive && source.followSymbolicLinks;
    if (source.kind == ManualSourceKind::file) {
        const std::wstring watchedFileName = lowercase(source.path.filename().native());
        target.isRelevantChange = [watchedFileName](const std::filesystem::path& relativePath) {
            return lowercase(relativePath.filename().native()) == watchedFileName;
        };
    }
    return target;
}

void SourceWatcher::start(const std::vector<ManualSource>& sources, int debounceSeconds, ChangeCallback callback) {
    std::vector<SourceWatchTarget> targets;
    targets.reserve(sources.size());
    for (const ManualSource& source : sources) {
        targets.push_back(makeSourceWatchTarget(source));
    }
    start(targets, debounceSeconds, std::move(callback));
}

void SourceWatcher::start(const std::vector<SourceWatchTarget>& targets, int debounceSeconds,
                          ChangeCallback callback) {
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

    for (const SourceWatchTarget& target : targets) {
        auto registration = std::make_unique<Registration>();
        registration->sourceId = target.sourceId;
        registration->directory = target.directory;
        registration->isRecursive = target.isRecursive;
        registration->isRelevantChange = target.isRelevantChange;
        registration->followSymbolicLinks = target.followSymbolicLinks;
        if (target.followSymbolicLinks) {
            registration->symbolicLinks = findSymbolicLinks(target.directory);
            registration->linkedSnapshot = snapshotLinks(registration->symbolicLinks, target.directory,
                                                         target.isRelevantChange);
            registration->nextLinkCheck = std::chrono::steady_clock::now() + std::chrono::seconds{2};
        }

        registrations_.push_back(std::move(registration));
    }

    isRunning_ = true;
    for (const std::unique_ptr<Registration>& registration : registrations_) {
        if (openRegistration(*registration) && !issueRead(*registration)) {
            closeRegistration(*registration);
        }
    }
    thread_ = std::thread([this] { watchLoop(); });
}

void SourceWatcher::stop() {
    isRunning_ = false;
    if (completionPort_ != nullptr) {
        PostQueuedCompletionStatus(static_cast<HANDLE>(completionPort_), 0, 0, nullptr);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    for (const std::unique_ptr<Registration>& registration : registrations_) {
        closeRegistration(*registration);
    }
    registrations_.clear();
    if (completionPort_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(completionPort_));
        completionPort_ = nullptr;
    }
    callback_ = {};
    activeRegistrationCount_ = 0;
}

std::size_t SourceWatcher::watchedSourceCount() const noexcept {
    return activeRegistrationCount_;
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

        if (completionKey != 0 && overlapped != nullptr) {
            auto& registration = *reinterpret_cast<Registration*>(completionKey);
            if (completed == FALSE) {
                closeRegistration(registration);
                continue;
            }
            bool isRelevant = transferredBytes == 0;
            std::size_t offset = 0;
            while (!isRelevant && offset < transferredBytes) {
                const auto* change = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(registration.buffer.data() + offset);
                const std::wstring changedName(change->FileName, change->FileNameLength / sizeof(wchar_t));
                try {
                    isRelevant = !registration.isRelevantChange ||
                                 registration.isRelevantChange(std::filesystem::path{changedName});
                } catch (...) {
                    isRelevant = true;
                }
                if (change->NextEntryOffset == 0) {
                    break;
                }
                offset += change->NextEntryOffset;
            }
            if (registration.followSymbolicLinks) {
                registration.symbolicLinks = findSymbolicLinks(registration.directory);
            }
            if (isRelevant) {
                registration.lastChange = std::chrono::steady_clock::now();
            }
            if (!issueRead(registration)) {
                closeRegistration(registration);
            }
        }

        const auto now = std::chrono::steady_clock::now();
        for (const std::unique_ptr<Registration>& registration : registrations_) {
            // Windows directory notifications do not follow links to external targets.
            if (registration->followSymbolicLinks && now >= registration->nextLinkCheck) {
                try {
                    auto snapshot = snapshotLinks(registration->symbolicLinks, registration->directory,
                                                  registration->isRelevantChange);
                    if (snapshot != registration->linkedSnapshot) {
                        registration->lastChange = now;
                        registration->linkedSnapshot = std::move(snapshot);
                    }
                } catch (...) {
                    registration->lastChange = now;
                }
                registration->nextLinkCheck = now + std::chrono::seconds{2};
            }
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

bool SourceWatcher::openRegistration(Registration& registration) {
    if (!isRunning_ || registration.handle != INVALID_HANDLE_VALUE) {
        return registration.handle != INVALID_HANDLE_VALUE;
    }
    registration.handle = CreateFileW(
        registration.directory.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (registration.handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (CreateIoCompletionPort(registration.handle, static_cast<HANDLE>(completionPort_),
                               reinterpret_cast<ULONG_PTR>(&registration), 0) == nullptr) {
        CloseHandle(registration.handle);
        registration.handle = INVALID_HANDLE_VALUE;
        return false;
    }
    ++activeRegistrationCount_;
    return true;
}

bool SourceWatcher::issueRead(Registration& registration) {
    if (!isRunning_) {
        return false;
    }
    registration.overlapped = {};
    DWORD ignoredBytes{};
    const BOOL started = ReadDirectoryChangesW(
        registration.handle, registration.buffer.data(), static_cast<DWORD>(registration.buffer.size()),
        registration.isRecursive ? TRUE : FALSE, kChangeFilter, &ignoredBytes, &registration.overlapped,
        nullptr);
    return started != FALSE;
}

void SourceWatcher::closeRegistration(Registration& registration) {
    if (registration.handle == INVALID_HANDLE_VALUE) {
        return;
    }
    CancelIoEx(registration.handle, &registration.overlapped);
    CloseHandle(registration.handle);
    registration.handle = INVALID_HANDLE_VALUE;
    --activeRegistrationCount_;
}
