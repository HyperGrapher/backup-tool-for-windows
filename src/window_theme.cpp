#include "window_theme.hpp"

#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

void applyDarkWindowChrome(void* nativeWindow) {
    const HWND window = static_cast<HWND>(nativeWindow);
    if (window == nullptr) {
        return;
    }

    const BOOL useDarkMode = TRUE;
    constexpr DWORD kDarkModeAttribute = 20;
    constexpr DWORD kOlderDarkModeAttribute = 19;
    if (FAILED(DwmSetWindowAttribute(window, kDarkModeAttribute, &useDarkMode, sizeof(useDarkMode)))) {
        DwmSetWindowAttribute(window, kOlderDarkModeAttribute, &useDarkMode, sizeof(useDarkMode));
    }

    const COLORREF captionColor = RGB(24, 24, 24);
    const COLORREF captionTextColor = RGB(243, 243, 243);
    constexpr DWORD kCaptionColorAttribute = 35;
    constexpr DWORD kCaptionTextColorAttribute = 36;
    DwmSetWindowAttribute(window, kCaptionColorAttribute, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(window, kCaptionTextColorAttribute, &captionTextColor, sizeof(captionTextColor));
}
