#pragma once

#include <string_view>

#include <FL/Enumerations.H>
#include <FL/Fl.H>

namespace UiTheme {

// The base remains dark and low-contrast so that colors keep their meaning.
inline const Fl_Color kBackground = fl_rgb_color(17, 19, 28);
inline const Fl_Color kSurface = fl_rgb_color(27, 30, 43);
inline const Fl_Color kNavigation = fl_rgb_color(22, 24, 35);
inline const Fl_Color kBorder = fl_rgb_color(55, 65, 81);
inline const Fl_Color kText = fl_rgb_color(241, 245, 249);
inline const Fl_Color kSecondaryText = fl_rgb_color(148, 163, 184);
inline const Fl_Color kSelection = fl_rgb_color(49, 46, 129);
inline const Fl_Color kControl = fl_rgb_color(45, 50, 65);
inline const Fl_Color kPressedControl = fl_rgb_color(61, 68, 87);

// Actions use the colors people already recognize from modern desktop and web apps.
inline const Fl_Color kPrimary = fl_rgb_color(99, 102, 241);
inline const Fl_Color kPrimaryPressed = fl_rgb_color(79, 70, 229);
inline const Fl_Color kDanger = fl_rgb_color(220, 38, 38);
inline const Fl_Color kDangerPressed = fl_rgb_color(185, 28, 28);
inline const Fl_Color kWarning = fl_rgb_color(217, 119, 6);
inline const Fl_Color kWarningPressed = fl_rgb_color(180, 83, 9);
inline const Fl_Color kSuccessAction = fl_rgb_color(5, 150, 105);
inline const Fl_Color kSuccessActionPressed = fl_rgb_color(4, 120, 87);
inline const Fl_Color kSafe = fl_rgb_color(52, 211, 153);
inline const Fl_Color kPending = fl_rgb_color(251, 191, 36);
inline const Fl_Color kSyncing = fl_rgb_color(56, 189, 248);
inline const Fl_Color kError = fl_rgb_color(248, 113, 113);

inline constexpr Fl_Font kUiFont = FL_FREE_FONT;
inline constexpr Fl_Font kUiFontSemibold = FL_FREE_FONT + 1;
inline constexpr Fl_Font kMonoFont = FL_FREE_FONT + 2;

enum class BackupStatus {
    current,
    waiting,
    syncing,
    error,
    inactive,
};

inline void initializeFonts() {
    Fl::set_font(kUiFont, "Segoe UI");
    Fl::set_font(kUiFontSemibold, "Segoe UI Semibold");
    Fl::set_font(kMonoFont, "Consolas");
}

[[nodiscard]] inline Fl_Color statusColor(BackupStatus status) {
    switch (status) {
        case BackupStatus::current:
            return kSafe;
        case BackupStatus::waiting:
            return kPending;
        case BackupStatus::syncing:
            return kSyncing;
        case BackupStatus::error:
            return kError;
        case BackupStatus::inactive:
            return kSecondaryText;
    }
    return kSecondaryText;
}

[[nodiscard]] inline std::string_view statusText(BackupStatus status) {
    switch (status) {
        case BackupStatus::current:
            return "●—● Current";
        case BackupStatus::waiting:
            return "●  ○ Waiting";
        case BackupStatus::syncing:
            return "●→○ Copying";
        case BackupStatus::error:
            return "●×○ Action needed";
        case BackupStatus::inactive:
            return "○  ○ Not connected";
    }
    return "○  ○ Unknown";
}

}  // namespace UiTheme
