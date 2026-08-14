#pragma once

#include <string_view>

#include <FL/Enumerations.H>
#include <FL/Fl.H>

namespace UiTheme {

inline const Fl_Color kBackground = fl_rgb_color(23, 23, 23);
inline const Fl_Color kSurface = fl_rgb_color(31, 31, 31);
inline const Fl_Color kNavigation = fl_rgb_color(27, 27, 27);
inline const Fl_Color kBorder = fl_rgb_color(59, 59, 59);
inline const Fl_Color kText = fl_rgb_color(242, 242, 242);
inline const Fl_Color kSecondaryText = fl_rgb_color(168, 168, 168);
inline const Fl_Color kSelection = fl_rgb_color(52, 52, 52);
inline const Fl_Color kControl = fl_rgb_color(43, 43, 43);
inline const Fl_Color kPressedControl = fl_rgb_color(62, 62, 62);
inline const Fl_Color kSafe = fl_rgb_color(108, 203, 95);
inline const Fl_Color kPending = fl_rgb_color(229, 180, 84);
inline const Fl_Color kSyncing = fl_rgb_color(91, 167, 232);
inline const Fl_Color kError = fl_rgb_color(241, 112, 122);

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
