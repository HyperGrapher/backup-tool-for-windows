#include "size_approval_dialog.hpp"

#define NOMINMAX
#include <windows.h>

#include <iomanip>
#include <sstream>
#include <string>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl.H>
#include <FL/Fl_Browser.H>
#include <FL/platform.H>

#include "ui_theme.hpp"
#include "ui_controls.hpp"
#include "ui_helpers.hpp"
#include "window_theme.hpp"

namespace {

[[nodiscard]] std::string formatMiB(std::uint64_t bytes) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1)
         << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MiB";
    return text.str();
}

void styleButton(Fl_Button& button, Fl_Color color, Fl_Color pressedColor,
                 Fl_Color labelColor = UiTheme::kText) {
    button.box(FL_FLAT_BOX);
    button.down_box(FL_FLAT_BOX);
    button.color(color);
    button.down_color(pressedColor);
    button.selection_color(UiTheme::kSelection);
    button.labelcolor(labelColor);
    button.labelfont(UiTheme::kUiFont);
    button.labelsize(12);
}

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, int size, Fl_Color color,
                 Fl_Font font = UiTheme::kUiFont) {
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
    Fl_Double_Window window(720, 470, "BackItUpTool - approval needed");
    window.callback(decideLaterCallback, this);
    window.color(UiTheme::kBackground);
    window.begin();
    addLabel(24, 18, 670, 30, "Choose a policy for this project", 18, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addLabel(24, 52, 670, 42,
             "This choice applies to future backups of this project. Decide later keeps this project pending while other backups continue.", 12,
             UiTheme::kSecondaryText);

    auto* browser = new Fl_Browser(24, 102, 672, 235);
    browser->box(FL_BORDER_BOX);
    browser->color(UiTheme::kSurface);
    browser->textcolor(UiTheme::kText);
    browser->selection_color(UiTheme::kSelection);
    browser->textsize(12);
    browser->textfont(UiTheme::kMonoFont);
    for (const SizeWarning& warning : warnings_) {
        browser->add(("Project: " + Ui::pathText(warning.sourcePath)).c_str());
        for (const LargeEligibleFile& largeFile : warning.largeFiles) {
            browser->add(("  Large file: " + Ui::pathText(largeFile.relativePath) + " (" +
                          formatMiB(largeFile.sizeBytes) + ")").c_str());
        }
        browser->add(("  Eligible project total: " + formatMiB(warning.eligibleSizeBytes)).c_str());
    }

    auto* ignoreButton = new ActionButton(24, 386, 228, 42, "Skip large files");
    styleButton(*ignoreButton, UiTheme::kWarning, UiTheme::kWarningPressed);
    ignoreButton->callback(ignorePermanentlyCallback, this);
    auto* alwaysButton = new ActionButton(264, 386, 228, 42, "Include large files");
    styleButton(*alwaysButton, UiTheme::kSuccessAction, UiTheme::kSuccessActionPressed);
    alwaysButton->labelfont(UiTheme::kUiFontSemibold);
    alwaysButton->callback(alwaysAllowCallback, this);
    auto* later = new ActionButton(504, 386, 192, 42, "Decide later");
    later->callback(decideLaterCallback, this);
    Ui::label(24, 342, 672, 36, "Skipping uses the configured size limit, including for new large files in this project.", 12, UiTheme::kSecondaryText);
    window.end();
    window_ = &window;
    window.set_modal();
    showWithDarkWindowChrome(window);
    const HWND nativeWindow = fl_xid(&window);
    SetForegroundWindow(nativeWindow);
    later->take_focus();
    while (window.shown()) {
        Fl::wait(0.05);
    }
    return result_;
}

void SizeApprovalDialog::ignorePermanentlyCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::ignorePermanently;
    dialog->window_->hide();
}

void SizeApprovalDialog::alwaysAllowCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::alwaysAllow;
    dialog->window_->hide();
}

void SizeApprovalDialog::decideLaterCallback(Fl_Widget*, void* context) {
    auto* dialog = static_cast<SizeApprovalDialog*>(context);
    dialog->result_ = SizeApprovalResult::decideLater;
    dialog->window_->hide();
}
