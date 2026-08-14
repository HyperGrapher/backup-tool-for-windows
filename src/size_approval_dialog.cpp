#include "size_approval_dialog.hpp"

#define NOMINMAX
#include <windows.h>

#include <iomanip>
#include <algorithm>
#include <sstream>
#include <string>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/platform.H>

#include "ui_theme.hpp"

namespace {

[[nodiscard]] std::string formatMiB(std::uint64_t bytes) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1)
         << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB";
    return text.str();
}

void styleButton(Fl_Button& button, Fl_Color color, Fl_Color labelColor = UiTheme::kText) {
    button.box(FL_BORDER_BOX);
    button.down_box(FL_BORDER_BOX);
    button.color(color);
    button.selection_color(UiTheme::kSelection);
    button.labelcolor(labelColor);
    button.labelsize(12);
}

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, int size, Fl_Color color,
                 Fl_Font font = FL_HELVETICA) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return label;
}

}  // namespace

SizeApprovalDialog::SizeApprovalDialog(const std::vector<SizeWarning>& warnings) : warnings_(warnings) {}

SizeApprovalResult SizeApprovalDialog::show() {
    Fl_Double_Window window(720, 430, "BackItUpTool - approval needed");
    window.color(UiTheme::kBackground);
    window.begin();
    addLabel(24, 18, 670, 30, "Large backup items need your approval", 20, UiTheme::kText, FL_HELVETICA_BOLD);
    addLabel(24, 52, 670, 42, "The backup is paused until you choose an option below.", 12, UiTheme::kMutedText);

    auto* browser = new Fl_Browser(24, 102, 672, 235);
    browser->box(FL_BORDER_BOX);
    browser->color(UiTheme::kCard);
    browser->textcolor(UiTheme::kText);
    browser->selection_color(UiTheme::kSelection);
    browser->textsize(12);
    for (const SizeWarning& warning : warnings_) {
        const std::string sourceText = warning.isProject ? "Project" : "File";
        browser->add((sourceText + ": " + warning.sourcePath.string()).c_str());
        for (const LargeEligibleFile& largeFile : warning.largeFiles) {
            browser->add(("  Large file: " + largeFile.relativePath.string() + " (" +
                          formatMiB(largeFile.sizeBytes) + ")").c_str());
        }
        if (warning.isProject) {
            browser->add(("  Eligible project total: " + formatMiB(warning.eligibleSizeBytes) +
                          " (limit 150.0 MiB)").c_str());
        }
    }

    auto* approveButton = new Fl_Button(24, 355, 190, 42, "Approve once");
    styleButton(*approveButton, UiTheme::kPrimary, UiTheme::kPrimaryText);
    approveButton->callback(approveOnceCallback, this);
    auto* alwaysButton = new Fl_Button(226, 355, 230, 42, "Always allow Projects");
    styleButton(*alwaysButton, UiTheme::kCard);
    alwaysButton->callback(approveAlwaysCallback, this);
    const bool hasProjectWarning = std::ranges::any_of(warnings_, [](const SizeWarning& warning) {
        return warning.isProject;
    });
    if (!hasProjectWarning) {
        alwaysButton->deactivate();
    }
    auto* skipButton = new Fl_Button(468, 355, 228, 42, "Skip until it changes");
    styleButton(*skipButton, UiTheme::kCard);
    skipButton->callback(skipCallback, this);
    window.end();
    window_ = &window;
    window.set_modal();
    window.show();
    const HWND nativeWindow = fl_xid(&window);
    SetWindowPos(nativeWindow, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(nativeWindow);
    while (window.shown()) {
        Fl::wait(0.05);
    }
    return result_;
}

void SizeApprovalDialog::approveOnceCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::approveOnce;
    dialog->window_->hide();
}

void SizeApprovalDialog::approveAlwaysCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::approveProjectsAlways;
    dialog->window_->hide();
}

void SizeApprovalDialog::skipCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::skip;
    dialog->window_->hide();
}
