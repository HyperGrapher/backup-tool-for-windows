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
    Fl_Double_Window window(720, 430, "BackItUpTool - approval needed");
    window.callback(ignoreCloseCallback, this);
    window.color(UiTheme::kBackground);
    window.begin();
    addLabel(24, 18, 670, 30, "Large backup items need your approval", 18, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addLabel(24, 52, 670, 42,
             "Ignore permanently skips the listed large files. Always allow includes them in every backup.", 12,
             UiTheme::kSecondaryText);

    auto* browser = new Fl_Browser(24, 102, 672, 235);
    browser->box(FL_BORDER_BOX);
    browser->color(UiTheme::kSurface);
    browser->textcolor(UiTheme::kText);
    browser->selection_color(UiTheme::kSelection);
    browser->textsize(12);
    browser->textfont(UiTheme::kMonoFont);
    for (const SizeWarning& warning : warnings_) {
        browser->add(("Project: " + warning.sourcePath.string()).c_str());
        for (const LargeEligibleFile& largeFile : warning.largeFiles) {
            browser->add(("  Large file: " + largeFile.relativePath.string() + " (" +
                          formatMiB(largeFile.sizeBytes) + ")").c_str());
        }
        browser->add(("  Eligible project total: " + formatMiB(warning.eligibleSizeBytes)).c_str());
    }

    auto* ignoreButton = new Fl_Button(24, 355, 324, 42, "Ignore permanently");
    styleButton(*ignoreButton, UiTheme::kWarning, UiTheme::kWarningPressed);
    ignoreButton->callback(ignorePermanentlyCallback, this);
    auto* alwaysButton = new Fl_Button(360, 355, 336, 42, "Always allow");
    styleButton(*alwaysButton, UiTheme::kSuccessAction, UiTheme::kSuccessActionPressed);
    alwaysButton->labelfont(UiTheme::kUiFontSemibold);
    alwaysButton->callback(alwaysAllowCallback, this);
    window.end();
    window_ = &window;
    window.set_modal();
    window.show();
    const HWND nativeWindow = fl_xid(&window);
    applyDarkWindowChrome(nativeWindow);
    SetWindowPos(nativeWindow, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(nativeWindow);
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

void SizeApprovalDialog::ignoreCloseCallback(Fl_Widget*, void*) {}
