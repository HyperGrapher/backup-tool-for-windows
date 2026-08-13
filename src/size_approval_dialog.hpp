#pragma once

#include <vector>

#include "backup_engine.hpp"

class Fl_Widget;
class Fl_Window;

enum class SizeApprovalResult {
    approveOnce,
    approveProjectsAlways,
    skip,
};

class SizeApprovalDialog final {
public:
    explicit SizeApprovalDialog(const std::vector<SizeWarning>& warnings);

    [[nodiscard]] SizeApprovalResult show();

private:
    const std::vector<SizeWarning>& warnings_;
    SizeApprovalResult result_{SizeApprovalResult::skip};
    Fl_Window* window_{};

    static void approveOnceCallback(Fl_Widget*, void* context);
    static void approveAlwaysCallback(Fl_Widget*, void* context);
    static void skipCallback(Fl_Widget*, void* context);
};
