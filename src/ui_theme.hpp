#pragma once

#include <string_view>

#include <FL/Enumerations.H>
#include <FL/Fl.H>

namespace UiTheme {

// Quiet Harbor: ink surfaces, sea-glass actions, and warm readable text.
inline const Fl_Color kBackground = fl_rgb_color(20, 29, 34);
inline const Fl_Color kSurface = fl_rgb_color(29, 42, 48);
inline const Fl_Color kNavigation = fl_rgb_color(16, 24, 29);
inline const Fl_Color kBorder = fl_rgb_color(53, 73, 80);
inline const Fl_Color kOutline = fl_rgb_color(102, 133, 141);
inline const Fl_Color kText = fl_rgb_color(237, 242, 236);
inline const Fl_Color kSecondaryText = fl_rgb_color(178, 195, 197);
inline const Fl_Color kDisabledText = fl_rgb_color(141, 159, 163);
inline const Fl_Color kSelection = fl_rgb_color(41, 74, 75);
inline const Fl_Color kControl = fl_rgb_color(38, 55, 63);
inline const Fl_Color kPressedControl = fl_rgb_color(53, 73, 80);
inline const Fl_Color kFocus = fl_rgb_color(178, 233, 221);
inline const Fl_Color kPrimary = fl_rgb_color(130, 216, 197);
inline const Fl_Color kPrimaryHover = fl_rgb_color(154, 229, 212);
inline const Fl_Color kPrimaryPressed = fl_rgb_color(101, 187, 170);
inline const Fl_Color kPrimaryText = fl_rgb_color(16, 40, 36);
inline const Fl_Color kDanger = fl_rgb_color(93, 44, 49);
inline const Fl_Color kDangerPressed = fl_rgb_color(116, 53, 60);
inline const Fl_Color kWarning = fl_rgb_color(88, 65, 34);
inline const Fl_Color kWarningPressed = fl_rgb_color(107, 79, 42);
inline const Fl_Color kSuccessAction = kPrimary;
inline const Fl_Color kSuccessActionPressed = kPrimaryPressed;
inline const Fl_Color kSafe = fl_rgb_color(168, 217, 152);
inline const Fl_Color kPending = fl_rgb_color(240, 196, 119);
inline const Fl_Color kSyncing = fl_rgb_color(153, 203, 238);
inline const Fl_Color kError = fl_rgb_color(242, 155, 155);

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
            return "Current";
        case BackupStatus::waiting:
            return "Waiting";
        case BackupStatus::syncing:
            return "Backing up";
        case BackupStatus::error:
            return "Action needed";
        case BackupStatus::inactive:
            return "Not set up";
    }
    return "Unknown";
}

}  // namespace UiTheme
