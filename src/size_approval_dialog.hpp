#pragma once

#include <vector>

#include "backup_engine.hpp"

class Fl_Widget;
class Fl_Window;

enum class SizeApprovalResult {
    decideLater,
    ignorePermanently,
    alwaysAllow,
};

class SizeApprovalDialog final {
public:
    explicit SizeApprovalDialog(const std::vector<SizeWarning>& warnings);

    [[nodiscard]] SizeApprovalResult show();

private:
    const std::vector<SizeWarning>& warnings_;
    SizeApprovalResult result_{SizeApprovalResult::decideLater};
    Fl_Window* window_{};

    static void ignorePermanentlyCallback(Fl_Widget*, void* context);
    static void alwaysAllowCallback(Fl_Widget*, void* context);
    static void decideLaterCallback(Fl_Widget*, void* context);
};
