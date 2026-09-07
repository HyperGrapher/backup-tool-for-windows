#include "backup_notification.hpp"

#define NOMINMAX
#include <windows.h>

#include <string>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/platform.H>

#include "ui_theme.hpp"

namespace {

constexpr int kNotificationWidth = 360;
constexpr int kNotificationHeight = 96;
constexpr int kScreenMargin = 16;

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

BackupNotification::~BackupNotification() = default;

void BackupNotification::show(std::string_view message) {
    if (window_ == nullptr) {
        buildWindow();
    }

    const std::string messageText{message};
    message_->copy_label(messageText.c_str());
    if (!window_->shown()) {
        int screenX = 0;
        int screenY = 0;
        int screenWidth = 0;
        int screenHeight = 0;
        Fl::screen_work_area(screenX, screenY, screenWidth, screenHeight, 0);
        window_->position(screenX + screenWidth - window_->w() - kScreenMargin,
                          screenY + screenHeight - window_->h() - kScreenMargin);
        window_->show();

        const HWND nativeWindow = fl_xid(window_.get());
        const LONG_PTR extendedStyle = GetWindowLongPtrW(nativeWindow, GWL_EXSTYLE);
        SetWindowLongPtrW(nativeWindow, GWL_EXSTYLE, extendedStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
        SetWindowPos(nativeWindow, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    window_->redraw();
}

void BackupNotification::hide() {
    if (window_ != nullptr) {
        window_->hide();
    }
}

void BackupNotification::buildWindow() {
    window_ = std::make_unique<Fl_Double_Window>(kNotificationWidth, kNotificationHeight);
    window_->border(0);
    window_->set_non_modal();
    window_->color(UiTheme::kSurface);
    window_->begin();

    auto* background = new Fl_Box(0, 0, kNotificationWidth, kNotificationHeight);
    background->box(FL_BORDER_BOX);
    background->color(UiTheme::kSurface);
    background->labelcolor(UiTheme::kBorder);

    auto* accent = new Fl_Box(0, 0, 4, kNotificationHeight);
    accent->box(FL_FLAT_BOX);
    accent->color(UiTheme::kPrimary);

    addLabel(20, 14, kNotificationWidth - 36, 24, "Backup in progress", 14, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    message_ = addLabel(20, 42, kNotificationWidth - 36, 40, "", 11, UiTheme::kSecondaryText);

    window_->end();
}
