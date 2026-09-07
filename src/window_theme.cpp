#include "window_theme.hpp"

#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>

#include <FL/Fl_Window.H>
#include <FL/platform.H>

namespace {

LRESULT CALLBACK darkChromeMessageHook(int code, WPARAM wordParameter, LPARAM longParameter) {
    if (code >= 0) {
        const auto* message = reinterpret_cast<const CWPSTRUCT*>(longParameter);
        if (message != nullptr && message->message == WM_CREATE &&
            (GetWindowLongPtrW(message->hwnd, GWL_STYLE) & WS_CAPTION) != 0) {
            applyDarkWindowChrome(message->hwnd);
        }
    }
    return CallNextHookEx(nullptr, code, wordParameter, longParameter);
}

}  // namespace

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

void showWithDarkWindowChrome(Fl_Window& window) {
    const HHOOK messageHook =
        SetWindowsHookExW(WH_CALLWNDPROC, darkChromeMessageHook, nullptr, GetCurrentThreadId());
    window.show();
    if (messageHook != nullptr) {
        UnhookWindowsHookEx(messageHook);
    }
    applyDarkWindowChrome(fl_xid(&window));
}
