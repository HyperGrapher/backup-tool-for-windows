#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "config_store.hpp"
#include "destinations_panel.hpp"
#include "sources_panel.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"

namespace {

constexpr wchar_t kAppName[] = L"BackItUpTool";
constexpr wchar_t kAppId[] = L"BackItUpTool";
constexpr wchar_t kTrayWindowClass[] = L"BackItUpTool.TrayWindow";
constexpr wchar_t kSingleInstanceName[] = L"Local\\BackItUpTool.SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowApplicationMessage = WM_APP + 2;
constexpr UINT kTrayOpenCommand = 1;
constexpr UINT kTrayExitCommand = 2;

constexpr int kWindowWidth = 900;
constexpr int kWindowHeight = 620;
constexpr int kTopBarHeight = 64;
constexpr int kSidebarWidth = 170;
constexpr int kFooterHeight = 38;

class TrayIcon final {
public:
    TrayIcon(std::function<void()> openCallback, std::function<void()> exitCallback)
        : openCallback_(std::move(openCallback)), exitCallback_(std::move(exitCallback)) {}

    ~TrayIcon() {
        remove();
    }

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    [[nodiscard]] bool create();
    void remove();

private:
    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter);
    LRESULT handleMessage(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter);

    std::function<void()> openCallback_;
    std::function<void()> exitCallback_;
    HWND window_{};
    HICON icon_{};
    bool ownsIcon_{};
    bool ownsWindowClass_{};
    NOTIFYICONDATAW notification_{};
};

class InstanceMutex final {
public:
    InstanceMutex() {
        handle_ = CreateMutexW(nullptr, TRUE, kSingleInstanceName);
        if (handle_ == nullptr) {
            throw std::runtime_error("Unable to create the single-instance mutex.");
        }
        alreadyExists_ = GetLastError() == ERROR_ALREADY_EXISTS;
    }

    ~InstanceMutex() {
        if (handle_ != nullptr) {
            if (!alreadyExists_) {
                ReleaseMutex(handle_);
            }
            CloseHandle(handle_);
        }
    }

    InstanceMutex(const InstanceMutex&) = delete;
    InstanceMutex& operator=(const InstanceMutex&) = delete;

    [[nodiscard]] bool alreadyExists() const {
        return alreadyExists_;
    }

private:
    HANDLE handle_{};
    bool alreadyExists_{};
};

class ComApartment final {
public:
    ComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        if (FAILED(result_)) {
            throw std::runtime_error("Unable to initialize Windows file dialogs.");
        }
    }

    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    HRESULT result_{E_FAIL};
};

[[nodiscard]] std::filesystem::path applicationDataDirectory() {
    PWSTR rawPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &rawPath))) {
        throw std::runtime_error("Unable to locate the Local AppData directory.");
    }

    const std::filesystem::path path = std::filesystem::path{rawPath} / kAppId;
    CoTaskMemFree(rawPath);
    std::filesystem::create_directories(path);
    return path;
}

void configureLogging(const std::filesystem::path& dataDirectory) {
    const auto logDirectory = dataDirectory / L"logs";
    std::filesystem::create_directories(logDirectory);
    auto logger = spdlog::rotating_logger_mt("application", (logDirectory / L"app.log").string(), 1024 * 1024, 3);
    spdlog::set_default_logger(std::move(logger));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
}

[[nodiscard]] BackupConfig loadOrCreateConfig(const ConfigStore& store) {
    const bool doesConfigExist = std::filesystem::exists(store.path());
    BackupConfig config = store.load();
    if (!doesConfigExist) {
        store.save(config);
    }
    return config;
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

void styleButton(Fl_Button& button, Fl_Color color, Fl_Color labelColor = UiTheme::kText) {
    button.box(FL_BORDER_BOX);
    button.down_box(FL_BORDER_BOX);
    button.color(color);
    button.selection_color(UiTheme::kSelection);
    button.labelcolor(labelColor);
    button.labelsize(12);
}

class App final {
public:
    App()
        : dataDirectory_(applicationDataDirectory()), configStore_(dataDirectory_ / L"config.json"),
          stateStore_(dataDirectory_ / L"state.db"), config_(loadOrCreateConfig(configStore_)),
          tray_([this] { show(); }, [this] { requestExit(); }) {
        configureLogging(dataDirectory_);
        buildUi();
        if (!tray_.create()) {
            throw std::runtime_error("Unable to create the notification-area icon.");
        }
        spdlog::info("Application started in the notification area");
    }

    ~App() {
        spdlog::info("Application stopped");
        spdlog::shutdown();
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool isRunning() const {
        return isRunning_;
    }

    [[nodiscard]] bool hasVisibleWindow() const {
        return window_ != nullptr && window_->shown();
    }

    void show() {
        if (!hasPositionedWindow_) {
            centerWindow();
            hasPositionedWindow_ = true;
        }
        window_->show();

        const HWND nativeWindow = fl_xid(window_.get());
        constexpr DWORD kDarkTitleBarAttribute = 20;
        const BOOL useDarkTitleBar = TRUE;
        DwmSetWindowAttribute(nativeWindow, kDarkTitleBarAttribute, &useDarkTitleBar, sizeof(useDarkTitleBar));
        ShowWindow(nativeWindow, SW_SHOWNORMAL);
        SetForegroundWindow(nativeWindow);
        BringWindowToTop(nativeWindow);
        window_->take_focus();
        spdlog::debug("Main window opened");
    }

    void hide() {
        if (window_ != nullptr) {
            window_->hide();
            spdlog::debug("Main window hidden");
        }
    }

    void requestExit() {
        hide();
        isRunning_ = false;
    }

private:
    std::filesystem::path dataDirectory_;
    ConfigStore configStore_;
    StateStore stateStore_;
    BackupConfig config_;
    TrayIcon tray_;
    std::unique_ptr<Fl_Double_Window> window_;
    SourcesPanel* sourcesPanel_{};
    DestinationsPanel* destinationsPanel_{};
    Fl_Button* sourcesNavigationButton_{};
    Fl_Button* destinationsNavigationButton_{};
    Fl_Box* configurationSummary_{};
    bool isRunning_{true};
    bool hasPositionedWindow_{};

    static void hideCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->hide();
    }

    static void openDataDirectoryCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->openDataDirectory();
    }

    static void sourcesNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showSourcesPage();
    }

    static void destinationsNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showDestinationsPage();
    }

    void buildUi() {
        Fl::scheme("none");
        Fl::background(9, 9, 11);
        Fl::foreground(250, 250, 250);

        window_ = std::make_unique<Fl_Double_Window>(kWindowWidth, kWindowHeight, "BackItUpTool");
        window_->size_range(760, 520);
        window_->color(UiTheme::kBackground);
        window_->callback(hideCallback, this);
        window_->begin();

        auto* header = new Fl_Box(0, 0, kWindowWidth, kTopBarHeight);
        header->box(FL_FLAT_BOX);
        header->color(UiTheme::kBackground);
        addLabel(20, 0, 300, kTopBarHeight, "BackItUpTool", 18, UiTheme::kText, FL_HELVETICA_BOLD);
        configurationSummary_ = addLabel(430, 0, 450, kTopBarHeight, "", 11, UiTheme::kMutedText);
        configurationSummary_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);

        auto* sidebar = new Fl_Box(0, kTopBarHeight, kSidebarWidth, kWindowHeight - kTopBarHeight);
        sidebar->box(FL_FLAT_BOX);
        sidebar->color(UiTheme::kSidebar);

        constexpr const char* navigationLabels[] = {
            "Overview", "Sources", "Projects", "Destinations", "Activity", "Settings"};
        for (std::size_t index = 0; index < std::size(navigationLabels); ++index) {
            const int buttonY = kTopBarHeight + 16 + static_cast<int>(index) * 44;
            auto* button = new Fl_Button(12, buttonY, kSidebarWidth - 24, 34, navigationLabels[index]);
            styleButton(*button, index == 1 ? UiTheme::kCard : UiTheme::kSidebar,
                        index == 1 ? UiTheme::kText : UiTheme::kMutedText);
            button->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
            if (index == 1) {
                sourcesNavigationButton_ = button;
                button->callback(sourcesNavigationCallback, this);
            } else if (index == 3) {
                destinationsNavigationButton_ = button;
                button->callback(destinationsNavigationCallback, this);
            } else {
                button->deactivate();
            }
        }

        auto* openFolderButton =
            new Fl_Button(12, kWindowHeight - kFooterHeight - 52, kSidebarWidth - 24, 34, "Open data folder");
        styleButton(*openFolderButton, UiTheme::kCard);
        openFolderButton->callback(openDataDirectoryCallback, this);

        const int panelX = kSidebarWidth + 12;
        const int panelY = kTopBarHeight + 12;
        const int panelWidth = kWindowWidth - panelX - 12;
        const int panelHeight = kWindowHeight - panelY - kFooterHeight - 12;
        sourcesPanel_ = new SourcesPanel(
            panelX, panelY, panelWidth, panelHeight, config_, configStore_, [this] { handleConfigChanged(); });
        destinationsPanel_ = new DestinationsPanel(
            panelX, panelY, panelWidth, panelHeight, config_, configStore_, [this] { handleConfigChanged(); });
        destinationsPanel_->hide();

        auto* footer = new Fl_Box(kSidebarWidth, kWindowHeight - kFooterHeight, kWindowWidth - kSidebarWidth,
                                  kFooterHeight);
        footer->box(FL_FLAT_BOX);
        footer->color(UiTheme::kSidebar);
        addLabel(kSidebarWidth + 16, kWindowHeight - kFooterHeight, kWindowWidth - kSidebarWidth - 32,
                 kFooterHeight, "Backup engine is not active yet. Configuration changes are saved.", 10,
                 UiTheme::kMutedText);

        window_->end();
        window_->resizable(sourcesPanel_);
        updateConfigurationSummary();
    }

    void centerWindow() {
        POINT cursor{};
        GetCursorPos(&cursor);
        const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        GetMonitorInfoW(monitor, &monitorInfo);
        const int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - kWindowWidth) / 2;
        const int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - kWindowHeight) / 2;
        window_->position(x, y);
    }

    void openDataDirectory() {
        const auto result = reinterpret_cast<std::intptr_t>(
            ShellExecuteW(nullptr, L"open", dataDirectory_.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) {
            reportError(std::runtime_error("Unable to open the BackItUpTool data folder."));
        }
    }

    void showSourcesPage() {
        destinationsPanel_->hide();
        sourcesPanel_->show();
        styleButton(*sourcesNavigationButton_, UiTheme::kCard, UiTheme::kText);
        styleButton(*destinationsNavigationButton_, UiTheme::kSidebar, UiTheme::kMutedText);
        window_->redraw();
    }

    void showDestinationsPage() {
        sourcesPanel_->hide();
        destinationsPanel_->refresh();
        destinationsPanel_->show();
        styleButton(*sourcesNavigationButton_, UiTheme::kSidebar, UiTheme::kMutedText);
        styleButton(*destinationsNavigationButton_, UiTheme::kCard, UiTheme::kText);
        window_->redraw();
    }

    void handleConfigChanged() {
        sourcesPanel_->refresh();
        updateConfigurationSummary();
    }

    void updateConfigurationSummary() {
        const std::string summary = std::to_string(config_.manualSources.size()) + " Manual Sources   " +
                                    std::to_string(config_.projectsRoots.size()) + " Project Roots   " +
                                    std::to_string(config_.destinations.size()) + " Destinations";
        configurationSummary_->copy_label(summary.c_str());
        window_->redraw();
    }

    void reportError(const std::exception& error) {
        spdlog::error("UI action failed: {}", error.what());
        fl_alert("%s", error.what());
    }
};

bool TrayIcon::create() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTrayWindowClass;

    const ATOM classAtom = RegisterClassW(&windowClass);
    if (classAtom == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    ownsWindowClass_ = classAtom != 0;

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTrayWindowClass,
        kAppName,
        WS_POPUP,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        instance,
        this);
    if (window_ == nullptr) {
        remove();
        return false;
    }

    using LoadIconMetricFunction = HRESULT(WINAPI*)(HINSTANCE, PCWSTR, int, HICON*);
    const auto loadIconMetric = reinterpret_cast<LoadIconMetricFunction>(
        GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "LoadIconMetric"));
    if (loadIconMetric != nullptr && SUCCEEDED(loadIconMetric(instance, MAKEINTRESOURCEW(101), LIM_SMALL, &icon_))) {
        ownsIcon_ = true;
    } else {
        icon_ = static_cast<HICON>(LoadImageW(
            instance,
            MAKEINTRESOURCEW(101),
            IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),
            GetSystemMetrics(SM_CYSMICON),
            LR_DEFAULTCOLOR));
        ownsIcon_ = icon_ != nullptr;
    }
    if (icon_ == nullptr) {
        icon_ = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }

    notification_ = {};
    notification_.cbSize = sizeof(notification_);
    notification_.hWnd = window_;
    notification_.uID = 1;
    notification_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notification_.uCallbackMessage = kTrayMessage;
    notification_.hIcon = icon_;
    lstrcpynW(notification_.szTip, kAppName, static_cast<int>(std::size(notification_.szTip)));
    if (Shell_NotifyIconW(NIM_ADD, &notification_) != TRUE) {
        remove();
        return false;
    }
    return true;
}

void TrayIcon::remove() {
    if (window_ != nullptr) {
        Shell_NotifyIconW(NIM_DELETE, &notification_);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    if (ownsIcon_ && icon_ != nullptr) {
        DestroyIcon(icon_);
    }
    icon_ = nullptr;
    ownsIcon_ = false;

    if (ownsWindowClass_) {
        UnregisterClassW(kTrayWindowClass, GetModuleHandleW(nullptr));
        ownsWindowClass_ = false;
    }
}

LRESULT CALLBACK TrayIcon::windowProcedure(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    TrayIcon* trayIcon = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(longParameter);
        trayIcon = static_cast<TrayIcon*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(trayIcon));
    }
    if (trayIcon != nullptr) {
        return trayIcon->handleMessage(window, message, wordParameter, longParameter);
    }
    return DefWindowProcW(window, message, wordParameter, longParameter);
}

LRESULT TrayIcon::handleMessage(HWND window, UINT message, WPARAM wordParameter, LPARAM longParameter) {
    static_cast<void>(wordParameter);
    if (message == kShowApplicationMessage) {
        openCallback_();
        return 0;
    }
    if (message == kTrayMessage) {
        if (longParameter == WM_LBUTTONUP) {
            openCallback_();
            return 0;
        }
        if (longParameter == WM_RBUTTONUP || longParameter == WM_CONTEXTMENU) {
            const HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, kTrayOpenCommand, L"Open BackItUpTool");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit");

            POINT cursor{};
            GetCursorPos(&cursor);
            SetForegroundWindow(window);
            const UINT command = TrackPopupMenu(
                menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON,
                cursor.x,
                cursor.y,
                0,
                window,
                nullptr);
            DestroyMenu(menu);
            PostMessageW(window, WM_NULL, 0, 0);

            if (command == kTrayOpenCommand) {
                openCallback_();
            } else if (command == kTrayExitCommand) {
                exitCallback_();
            }
            return 0;
        }
    }
    return DefWindowProcW(window, message, wordParameter, longParameter);
}

void dispatchNativeMessages() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        InstanceMutex instanceMutex;
        if (instanceMutex.alreadyExists()) {
            if (const HWND existingWindow = FindWindowW(kTrayWindowClass, kAppName); existingWindow != nullptr) {
                PostMessageW(existingWindow, kShowApplicationMessage, 0, 0);
            }
            return 0;
        }

        ComApartment comApartment;
        App app;
        while (app.isRunning()) {
            if (app.hasVisibleWindow()) {
                Fl::wait(0.1);
            } else {
                MsgWaitForMultipleObjectsEx(0, nullptr, 250, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
            dispatchNativeMessages();
            Fl::check();
        }
        return 0;
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "BackItUpTool", MB_OK | MB_ICONERROR);
        return 1;
    }
}
