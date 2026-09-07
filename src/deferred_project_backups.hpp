#pragma once

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "backup_engine.hpp"

// Deferral belongs to this session, not the saved include/ignore policy. Routes stay dirty.
class DeferredProjectBackups final {
public:
    void defer(const std::string& sourceId) { sourceIds_.insert(sourceId); }
    void clear() { sourceIds_.clear(); }
    [[nodiscard]] bool empty() const { return sourceIds_.empty(); }
    [[nodiscard]] bool contains(const std::string& sourceId) const { return sourceIds_.contains(sourceId); }
    void removeFrom(std::vector<BackupPlan>& plans) const {
        std::erase_if(plans, [&](const BackupPlan& plan) {
            return plan.isProjectsSource && contains(plan.sourceId);
        });
    }

private:
    std::unordered_set<std::string> sourceIds_;
};
