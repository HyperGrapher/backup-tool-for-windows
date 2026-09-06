#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "backup_config.hpp"

struct SourceWatchTarget {
    using ChangeFilter = std::function<bool(const std::filesystem::path& relativePath)>;

    std::string sourceId;
    std::filesystem::path directory;
    bool isRecursive{true};
    ChangeFilter isRelevantChange;
};

[[nodiscard]] SourceWatchTarget makeSourceWatchTarget(const ManualSource& source);

class SourceWatcher final {
public:
    using ChangeCallback = std::function<void(const std::string& sourceId)>;

    SourceWatcher();
    ~SourceWatcher();

    SourceWatcher(const SourceWatcher&) = delete;
    SourceWatcher& operator=(const SourceWatcher&) = delete;

    void start(const std::vector<ManualSource>& sources, int debounceSeconds, ChangeCallback callback);
    void start(const std::vector<SourceWatchTarget>& targets, int debounceSeconds, ChangeCallback callback);
    void stop();

    [[nodiscard]] std::size_t watchedSourceCount() const noexcept;

private:
    struct Registration;

    void watchLoop();
    [[nodiscard]] bool openRegistration(Registration& registration);
    [[nodiscard]] bool issueRead(Registration& registration);
    void closeRegistration(Registration& registration);

    void* completionPort_{};
    std::vector<std::unique_ptr<Registration>> registrations_;
    std::chrono::seconds debounce_{8};
    ChangeCallback callback_;
    std::thread thread_;
    std::atomic_bool isRunning_{};
    std::atomic_size_t activeRegistrationCount_{};
};
