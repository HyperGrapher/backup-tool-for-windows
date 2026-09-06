#pragma once

#include <string_view>

#include <FL/Enumerations.H>
#include <FL/Fl.H>

namespace UiTheme {

// Neutral charcoal layers follow the visual hierarchy of Windows dark surfaces.
inline const Fl_Color kBackground = fl_rgb_color(30, 30, 30);
inline const Fl_Color kSurface = fl_rgb_color(38, 38, 38);
inline const Fl_Color kNavigation = fl_rgb_color(24, 24, 24);
inline const Fl_Color kBorder = fl_rgb_color(63, 63, 63);
inline const Fl_Color kText = fl_rgb_color(243, 243, 243);
inline const Fl_Color kSecondaryText = fl_rgb_color(179, 179, 179);
inline const Fl_Color kSelection = fl_rgb_color(20, 59, 82);
inline const Fl_Color kControl = fl_rgb_color(50, 50, 50);
inline const Fl_Color kPressedControl = fl_rgb_color(68, 68, 68);

// Windows blue identifies primary actions; semantic actions retain familiar colors.
inline const Fl_Color kPrimary = fl_rgb_color(15, 108, 189);
inline const Fl_Color kPrimaryPressed = fl_rgb_color(17, 94, 163);
inline const Fl_Color kDanger = fl_rgb_color(196, 43, 28);
inline const Fl_Color kDangerPressed = fl_rgb_color(159, 36, 26);
inline const Fl_Color kWarning = fl_rgb_color(157, 93, 0);
inline const Fl_Color kWarningPressed = fl_rgb_color(122, 72, 0);
inline const Fl_Color kSuccessAction = fl_rgb_color(16, 124, 16);
inline const Fl_Color kSuccessActionPressed = fl_rgb_color(11, 90, 11);
inline const Fl_Color kSafe = fl_rgb_color(108, 203, 95);
inline const Fl_Color kPending = fl_rgb_color(249, 168, 37);
inline const Fl_Color kSyncing = fl_rgb_color(96, 205, 255);
inline const Fl_Color kError = fl_rgb_color(255, 153, 164);

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
