#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <ntddstor.h>
#include <shellapi.h>
#include <shlobj.h>

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tooltip.H>
#include <FL/fl_ask.H>
#include <FL/fl_draw.H>
#include <FL/platform.H>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include "activity_panel.hpp"
#include "config_store.hpp"
#include "backup_engine.hpp"
#include "backup_health.hpp"
#include "backup_routes.hpp"
#include "deferred_project_backups.hpp"
#include "ui_helpers.hpp"
#include "backup_notification.hpp"
#include "destinations_panel.hpp"
#include "explorer_integration.hpp"
#include "overview_panel.hpp"
#include "projects_panel.hpp"
#include "projects_scanner.hpp"
#include "settings_panel.hpp"
#include "source_watcher.hpp"
#include "size_approval_dialog.hpp"
#include "sources_panel.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"
#include "ui_controls.hpp"
#include "window_theme.hpp"

namespace {

constexpr wchar_t kAppName[] = L"BackItUpTool";
constexpr wchar_t kAppId[] = L"BackItUpTool";
constexpr wchar_t kTrayWindowClass[] = L"BackItUpTool.TrayWindow";
constexpr wchar_t kSingleInstanceName[] = L"Local\\BackItUpTool.SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kShowApplicationMessage = WM_APP + 2;
constexpr UINT kTrayOpenCommand = 1;
constexpr UINT kTrayExitCommand = 2;
constexpr std::string_view kProjectsRootWatchPrefix = "projects-root-watch:";

constexpr int kWindowWidth = 1040;
constexpr int kWindowHeight = 680;
constexpr int kTopBarHeight = 52;
constexpr int kSidebarWidth = 156;
constexpr int kFooterHeight = 26;

class TrayIcon final {
public:
    TrayIcon(std::function<void()> openCallback, std::function<void()> exitCallback,
             std::function<void()> deviceChangedCallback)
        : openCallback_(std::move(openCallback)), exitCallback_(std::move(exitCallback)),
          deviceChangedCallback_(std::move(deviceChangedCallback)) {}

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
    std::function<void()> deviceChangedCallback_;
    HWND window_{};
    HDEVNOTIFY deviceNotification_{};
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

[[nodiscard]] std::string utcNowForApproval() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
    gmtime_s(&utc, &now);
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
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
                 Fl_Font font = UiTheme::kUiFont) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return label;
}

void styleButton(Fl_Button& button, bool isSelected = false, bool isStrong = false) {
    button.box(FL_FLAT_BOX);
    button.down_box(FL_FLAT_BOX);
    button.color(isStrong ? UiTheme::kPrimary : (isSelected ? UiTheme::kSelection : UiTheme::kControl));
    button.down_color(isStrong ? UiTheme::kPrimaryPressed : UiTheme::kPressedControl);
    button.selection_color(isStrong ? UiTheme::kPrimary : UiTheme::kSelection);
    button.labelcolor(UiTheme::kText);
    button.labelfont(isSelected || isStrong ? UiTheme::kUiFontSemibold : UiTheme::kUiFont);
    button.labelsize(12);
}

enum class NavigationIcon {
    status,
    source,
    project,
    destination,
    activity,
    settings,
};

class NavigationButton final : public Fl_Button {
public:
    NavigationButton(int x, int y, int width, int height, const char* label, NavigationIcon icon)
        : Fl_Button(x, y, width, height, label), icon_(icon) {
        box(FL_NO_BOX);
        down_box(FL_NO_BOX);
    }

    void setSelected(bool isSelected) {
        isSelected_ = isSelected;
        redraw();
    }

    int handle(int event) override {
        if (event == FL_ENTER) {
            isHovered_ = true;
            redraw();
            return 1;
        }
        if (event == FL_LEAVE) {
            isHovered_ = false;
            redraw();
            return 1;
        }
        return Fl_Button::handle(event);
    }

    void draw() override {
        Fl_Color background = UiTheme::kNavigation;
        if (value() != 0) {
            background = UiTheme::kPressedControl;
        } else if (isSelected_) {
            background = UiTheme::kSelection;
        } else if (isHovered_) {
            background = UiTheme::kControl;
        }
        fl_color(background);
        fl_rectf(x(), y(), w(), h());

        if (isSelected_) {
            fl_color(UiTheme::kPrimary);
            fl_rectf(x(), y() + 6, 2, h() - 12);
        }

        if (Fl::focus() == this) {
            fl_color(UiTheme::kFocus);
            fl_rect(x() + 2, y() + 2, w() - 4, h() - 4);
        }
        const Fl_Color foreground = isSelected_ ? UiTheme::kText : UiTheme::kSecondaryText;
        drawIcon(x() + 14, y() + h() / 2, foreground);
        fl_color(isSelected_ ? UiTheme::kText : UiTheme::kSecondaryText);
        fl_font(isSelected_ ? UiTheme::kUiFontSemibold : UiTheme::kUiFont, 12);
        fl_draw(label(), x() + 42, y(), w() - 50, h(), FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    }

private:
    void drawIcon(int left, int centerY, Fl_Color color) const {
        fl_color(color);
        fl_line_style(FL_SOLID, 1);
        switch (icon_) {
            case NavigationIcon::status:
                fl_circle(left + 3, centerY, 2);
                fl_line(left + 5, centerY, left + 11, centerY);
                fl_circle(left + 13, centerY, 2);
                break;
            case NavigationIcon::source:
                fl_rect(left + 2, centerY - 7, 11, 14);
                fl_line(left + 9, centerY - 7, left + 13, centerY - 3);
                fl_line(left + 9, centerY - 7, left + 9, centerY - 3);
                fl_line(left + 9, centerY - 3, left + 13, centerY - 3);
                break;
            case NavigationIcon::project:
                fl_line(left + 1, centerY - 5, left + 6, centerY - 5);
                fl_line(left + 6, centerY - 5, left + 8, centerY - 3);
                fl_line(left + 8, centerY - 3, left + 15, centerY - 3);
                fl_line(left + 15, centerY - 3, left + 15, centerY + 6);
                fl_line(left + 15, centerY + 6, left + 1, centerY + 6);
                fl_line(left + 1, centerY + 6, left + 1, centerY - 5);
                break;
            case NavigationIcon::destination:
                fl_rect(left + 1, centerY - 6, 14, 12);
                fl_line(left + 1, centerY + 2, left + 15, centerY + 2);
                fl_circle(left + 12, centerY + 4, 1);
                break;
            case NavigationIcon::activity:
                fl_circle(left + 8, centerY, 7);
                fl_line(left + 8, centerY, left + 8, centerY - 4);
                fl_line(left + 8, centerY, left + 11, centerY + 2);
                break;
            case NavigationIcon::settings:
                fl_circle(left + 8, centerY, 4);
                fl_circle(left + 8, centerY, 1);
                fl_line(left + 8, centerY - 7, left + 8, centerY - 5);
                fl_line(left + 8, centerY + 5, left + 8, centerY + 7);
                fl_line(left + 1, centerY, left + 3, centerY);
                fl_line(left + 13, centerY, left + 15, centerY);
                break;
        }
        fl_line_style(0);
    }

    NavigationIcon icon_;
    bool isSelected_{};
    bool isHovered_{};
};

class PanelStack final : public Fl_Group {
public:
    PanelStack(int x, int y, int width, int height) : Fl_Group(x, y, width, height) {}

    void resize(int x, int y, int width, int height) override {
        Fl_Group::resize(x, y, width, height);
        for (int index = 0; index < children(); ++index) {
            child(index)->resize(x, y, width, height);
        }
    }
};

[[nodiscard]] std::string projectsRootWatchId(std::string_view rootId) {
    return std::string{kProjectsRootWatchPrefix} + std::string{rootId};
}

[[nodiscard]] bool isProjectsRootWatchId(std::string_view sourceId) {
    return sourceId.starts_with(kProjectsRootWatchPrefix);
}

class App final {
public:
    App()
        : dataDirectory_(applicationDataDirectory()), configStore_(dataDirectory_ / L"config.json"),
          stateStore_(dataDirectory_ / L"state.db"), config_(loadOrCreateConfig(configStore_)),
          tray_([this] { show(); }, [this] { requestExit(); }, [this] { handleDestinationDevicesChanged(); }) {
        configureLogging(dataDirectory_);
        buildUi();
        if (!tray_.create()) {
            throw std::runtime_error("Unable to create the notification-area icon.");
        }
        restartSourceWatcher();
        stateStore_.recoverInterruptedRoutes();
        initializeRouteStates();
        updateGlobalStatus();
        Fl::add_timeout(1.0, automaticWorkTimerCallback, this);
        if (config_.manualSources.empty() && config_.projectsRoots.empty() && config_.destinations.empty()) {
            show();
            footerStatus_->copy_label("Welcome. Add a source and a destination to begin. Closing this window keeps backups in the tray.");
        }
        spdlog::info("Application started");
    }

    ~App() {
        Fl::remove_timeout(automaticWorkTimerCallback, this);
        sourceWatcher_.stop();
        if (backupThread_.joinable()) {
            backupThread_.join();
        }
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
        backupNotification_.hide();
        showWithDarkWindowChrome(*window_);

        const HWND nativeWindow = fl_xid(window_.get());
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
        if (isBackupRunning_) {
            show();
            footerStatus_->copy_label("A backup is still running. Let it finish, then choose Exit again.");
            return;
        }
        hide();
        isRunning_ = false;
    }

private:
    struct SourceChangeNotification {
        App* app{};
        std::string sourceId;
    };

    struct BackupPreparation {
        std::vector<BackupPlan> mirrorPlans;
        std::vector<BackupPlan> archivePlans;
        std::vector<SizeWarning> sizeWarnings;
        std::optional<std::string> error;
        std::uint64_t largeFileThresholdBytes{};
    };

    std::filesystem::path dataDirectory_;
    ConfigStore configStore_;
    StateStore stateStore_;
    BackupConfig config_;
    TrayIcon tray_;
    std::unique_ptr<Fl_Double_Window> window_;
    PanelStack* panelStack_{};
    SourcesPanel* sourcesPanel_{};
    DestinationsPanel* destinationsPanel_{};
    OverviewPanel* overviewPanel_{};
    ProjectsPanel* projectsPanel_{};
    ActivityPanel* activityPanel_{};
    SettingsPanel* settingsPanel_{};
    NavigationButton* overviewNavigationButton_{};
    NavigationButton* sourcesNavigationButton_{};
    NavigationButton* projectsNavigationButton_{};
    NavigationButton* destinationsNavigationButton_{};
    NavigationButton* activityNavigationButton_{};
    NavigationButton* settingsNavigationButton_{};
    Fl_Box* globalStatus_{};
    Fl_Box* configurationSummary_{};
    Fl_Box* footerStatus_{};
    Fl_Button* runNowButton_{};
    Fl_Menu_Button* pauseMenu_{};
    Fl_Button* resumeButton_{};
    DeferredProjectBackups deferredProjects_;
    BackupEngine backupEngine_;
    BackupNotification backupNotification_;
    SourceWatcher sourceWatcher_;
    std::vector<ConfiguredProjectsSource> projectsSources_;
    std::thread backupThread_;
    std::mutex backupResultMutex_;
    std::optional<BackupRunSummary> backupResult_;
    std::optional<BackupPreparation> backupPreparation_;
    bool isBackupRunning_{};
    bool isRunning_{true};
    bool hasPositionedWindow_{};
    bool isProjectsRefreshPending_{};
    bool isDestinationChangePending_{};
    std::chrono::steady_clock::time_point nextAutomaticAttempt_{};
    std::optional<std::chrono::steady_clock::time_point> pauseUntil_;

    static void hideCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->hide();
    }

    static void openDataDirectoryCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->openDataDirectory();
    }

    static void sourcesNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showSourcesPage();
    }

    static void overviewNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showOverviewPage();
    }

    static void destinationsNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showDestinationsPage();
    }

    static void projectsNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showProjectsPage();
    }

    static void activityNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showActivityPage();
    }

    static void settingsNavigationCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->showSettingsPage();
    }

    static void runNowCallback(Fl_Widget*, void* data) {
        auto* app = static_cast<App*>(data);
        app->deferredProjects_.clear();
        app->startBackupRun();
    }

    static void pauseOneHourCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->pauseFor(std::chrono::hours{1});
    }

    static void pauseThreeHoursCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->pauseFor(std::chrono::hours{3});
    }

    static void pauseFiveHoursCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->pauseFor(std::chrono::hours{5});
    }

    static void resumeCallback(Fl_Widget*, void* data) {
        static_cast<App*>(data)->resumeAutomaticBackups();
    }

    static void backupFinishedAwake(void* data) {
        static_cast<App*>(data)->finishBackupRun();
    }

    static void backupPreparedAwake(void* data) {
        static_cast<App*>(data)->finishBackupPreparation();
    }

    static void sourceChangedAwake(void* data) {
        std::unique_ptr<SourceChangeNotification> notification{static_cast<SourceChangeNotification*>(data)};
        notification->app->handleSourceChanged(notification->sourceId);
    }

    static void automaticWorkTimerCallback(void* data) {
        auto* app = static_cast<App*>(data);
        app->checkAutomaticWork();
        Fl::repeat_timeout(1.0, automaticWorkTimerCallback, data);
    }

    void buildUi() {
        Fl::scheme("none");
        UiTheme::initializeFonts();
        Fl::background(20, 29, 34);
        Fl::background2(29, 42, 48);
        Fl::foreground(237, 242, 236);
        Fl::visible_focus(1);
        Fl_Tooltip::font(UiTheme::kUiFont);
        Fl_Tooltip::size(12);
        Fl_Tooltip::color(UiTheme::kControl);
        Fl_Tooltip::textcolor(UiTheme::kText);
        fl_message_font(UiTheme::kUiFont, 14);

        window_ = std::make_unique<Fl_Double_Window>(kWindowWidth, kWindowHeight, "BackItUpTool");
        window_->size_range(860, 560);
        window_->color(UiTheme::kBackground);
        window_->callback(hideCallback, this);
        window_->begin();

        auto* header = new Fl_Box(0, 0, kWindowWidth, kTopBarHeight);
        header->box(FL_FLAT_BOX);
        header->color(UiTheme::kSurface);
        globalStatus_ = addLabel(16, 0, 190, kTopBarHeight, "●—● Checking", 13, UiTheme::kSecondaryText,
                                 UiTheme::kUiFontSemibold);
        configurationSummary_ = addLabel(206, 0, kWindowWidth - 430, kTopBarHeight, "", 11,
                                         UiTheme::kSecondaryText);

        pauseMenu_ = new Fl_Menu_Button(kWindowWidth - 244, 9, 96, 36, "Pause");
        pauseMenu_->box(FL_FLAT_BOX);
        pauseMenu_->color(UiTheme::kControl);
        pauseMenu_->selection_color(UiTheme::kSelection);
        pauseMenu_->labelcolor(UiTheme::kText);
        pauseMenu_->labelfont(UiTheme::kUiFont);
        pauseMenu_->labelsize(13);
        pauseMenu_->textfont(UiTheme::kUiFont);
        pauseMenu_->textsize(13);
        pauseMenu_->textcolor(UiTheme::kText);
        pauseMenu_->add("1 hour", 0, pauseOneHourCallback, this);
        pauseMenu_->add("3 hours", 0, pauseThreeHoursCallback, this);
        pauseMenu_->add("5 hours", 0, pauseFiveHoursCallback, this);
        pauseMenu_->add("Resume", 0, resumeCallback, this);

        resumeButton_ = new ActionButton(kWindowWidth - 244, 9, 96, 36, "Resume");
        resumeButton_->callback(resumeCallback, this);
        resumeButton_->hide();
        runNowButton_ = new ActionButton(kWindowWidth - 140, 9, 124, 36, "Back up now");
        styleButton(*runNowButton_, false, true);
        runNowButton_->callback(runNowCallback, this);
        auto* headerDivider = new Fl_Box(0, kTopBarHeight - 1, kWindowWidth, 1);
        headerDivider->box(FL_FLAT_BOX);
        headerDivider->color(UiTheme::kBorder);

        auto* sidebar = new Fl_Box(0, kTopBarHeight, kSidebarWidth, kWindowHeight - kTopBarHeight);
        sidebar->box(FL_FLAT_BOX);
        sidebar->color(UiTheme::kNavigation);
        auto* sidebarDivider = new Fl_Box(kSidebarWidth - 1, kTopBarHeight, 1,
                                          kWindowHeight - kTopBarHeight - kFooterHeight);
        sidebarDivider->box(FL_FLAT_BOX);
        sidebarDivider->color(UiTheme::kBorder);

        constexpr const char* navigationLabels[] = {
            "Overview", "Sources", "Projects", "Destinations", "Activity", "Settings"};
        constexpr NavigationIcon navigationIcons[] = {
            NavigationIcon::status, NavigationIcon::source, NavigationIcon::project,
            NavigationIcon::destination, NavigationIcon::activity, NavigationIcon::settings};
        for (std::size_t index = 0; index < std::size(navigationLabels); ++index) {
            const int buttonY = kTopBarHeight + 12 + static_cast<int>(index) * 36;
            auto* button = new NavigationButton(8, buttonY, kSidebarWidth - 16, 30,
                                                navigationLabels[index], navigationIcons[index]);
            button->setSelected(index == 0);
            if (index == 0) {
                overviewNavigationButton_ = button;
                button->callback(overviewNavigationCallback, this);
            } else if (index == 1) {
                sourcesNavigationButton_ = button;
                button->callback(sourcesNavigationCallback, this);
            } else if (index == 2) {
                projectsNavigationButton_ = button;
                button->callback(projectsNavigationCallback, this);
            } else if (index == 3) {
                destinationsNavigationButton_ = button;
                button->callback(destinationsNavigationCallback, this);
            } else if (index == 4) {
                activityNavigationButton_ = button;
                button->callback(activityNavigationCallback, this);
            } else if (index == 5) {
                settingsNavigationButton_ = button;
                button->callback(settingsNavigationCallback, this);
            }
        }

        const int panelX = kSidebarWidth;
        const int panelY = kTopBarHeight;
        const int panelWidth = kWindowWidth - panelX;
        const int panelHeight = kWindowHeight - panelY - kFooterHeight;
        panelStack_ = new PanelStack(panelX, panelY, panelWidth, panelHeight);
        panelStack_->box(FL_FLAT_BOX);
        panelStack_->color(UiTheme::kBackground);
        panelStack_->begin();
        sourcesPanel_ = new SourcesPanel(
            panelX, panelY, panelWidth, panelHeight, config_, configStore_, stateStore_,
            [this] { handleConfigChanged(); });
        destinationsPanel_ = new DestinationsPanel(
            panelX, panelY, panelWidth, panelHeight, config_, configStore_, stateStore_, projectsSources_,
            [this] { handleConfigChanged(); });
        projectsPanel_ = new ProjectsPanel(
            panelX, panelY, panelWidth, panelHeight, config_, configStore_, stateStore_,
            [this] { handleConfigChanged(); });
        overviewPanel_ =
            new OverviewPanel(panelX, panelY, panelWidth, panelHeight, config_, projectsSources_, stateStore_,
                              [this] { showSourcesPage(); }, [this] { showDestinationsPage(); });
        activityPanel_ = new ActivityPanel(panelX, panelY, panelWidth, panelHeight, config_, stateStore_);
        settingsPanel_ = new SettingsPanel(panelX, panelY, panelWidth, panelHeight, config_, configStore_,
                                           [this] { handleConfigChanged(); }, [this] { openDataDirectory(); },
                                           [this] { ensureExplorerIntegration(dataDirectory_); });
        panelStack_->end();
        sourcesPanel_->hide();
        projectsPanel_->hide();
        destinationsPanel_->hide();
        activityPanel_->hide();
        settingsPanel_->hide();

        auto* footer = new Fl_Box(0, kWindowHeight - kFooterHeight, kWindowWidth,
                                  kFooterHeight);
        footer->box(FL_FLAT_BOX);
        footer->color(UiTheme::kSurface);
        auto* footerDivider = new Fl_Box(0, kWindowHeight - kFooterHeight, kWindowWidth, 1);
        footerDivider->box(FL_FLAT_BOX);
        footerDivider->color(UiTheme::kBorder);
        footerStatus_ = addLabel(kSidebarWidth + 16, kWindowHeight - kFooterHeight, kWindowWidth - kSidebarWidth - 32,
                                  kFooterHeight, "Watching for changes.", 10, UiTheme::kSecondaryText);

        window_->end();
        window_->resizable(panelStack_);
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
        sourcesPanel_->refresh();
        showPanel(sourcesPanel_, sourcesNavigationButton_);
    }

    void showDestinationsPage() {
        destinationsPanel_->refresh();
        showPanel(destinationsPanel_, destinationsNavigationButton_);
    }

    void showOverviewPage() {
        overviewPanel_->refresh();
        showPanel(overviewPanel_, overviewNavigationButton_);
    }

    void showProjectsPage() {
        projectsPanel_->refresh();
        showPanel(projectsPanel_, projectsNavigationButton_);
    }

    void showActivityPage() {
        activityPanel_->refresh();
        showPanel(activityPanel_, activityNavigationButton_);
    }

    void showSettingsPage() {
        settingsPanel_->refresh();
        showPanel(settingsPanel_, settingsNavigationButton_);
    }

    void showPanel(Fl_Group* selectedPanel, NavigationButton* selectedNavigationButton) {
        constexpr std::size_t kPageCount = 6;
        Fl_Group* panels[kPageCount] = {
            overviewPanel_, sourcesPanel_, projectsPanel_, destinationsPanel_, activityPanel_, settingsPanel_};
        NavigationButton* navigationButtons[kPageCount] = {
            overviewNavigationButton_, sourcesNavigationButton_, projectsNavigationButton_,
            destinationsNavigationButton_, activityNavigationButton_, settingsNavigationButton_};
        for (Fl_Group* panel : panels) {
            panel->hide();
        }
        for (NavigationButton* button : navigationButtons) {
            button->setSelected(button == selectedNavigationButton);
        }
        selectedPanel->show();
        window_->redraw();
    }

    void handleConfigChanged() {
        configStore_.save(config_);
        sourcesPanel_->refresh();
        projectsPanel_->refresh();
        destinationsPanel_->refresh();
        restartSourceWatcher();
        initializeRouteStates();
        overviewPanel_->refresh();
        activityPanel_->refresh();
        settingsPanel_->refresh();
        nextAutomaticAttempt_ = std::chrono::steady_clock::now();
        updateConfigurationSummary();
        updateGlobalStatus();
        refreshExplorerOverlays();
    }

    void initializeRouteStates() {
        for (const BackupRoute& route : effectiveBackupRoutes(config_, projectsSources_)) {
            const auto state = stateStore_.routeState(route.sourceId, route.destinationId);
            if (!state.has_value() || !state->lastSuccessUtc.has_value()) {
                stateStore_.markRouteDirty(route.sourceId, route.destinationId);
            }
        }
    }

    void restartSourceWatcher() {
        sourceWatcher_.stop();
        ConfiguredProjectsDiscovery discovery = discoverConfiguredProjects(config_.projectsRoots);
        projectsSources_ = std::move(discovery.sources);
        std::vector<SourceWatchTarget> watchTargets;
        watchTargets.reserve(config_.manualSources.size() + config_.projectsRoots.size() + projectsSources_.size());
        for (const ManualSource& source : config_.manualSources) {
            watchTargets.push_back(makeSourceWatchTarget(source));
        }
        for (const ProjectsRoot& root : config_.projectsRoots) {
            SourceWatchTarget target;
            target.sourceId = projectsRootWatchId(root.id);
            target.directory = root.path;
            target.isRecursive = true;
            target.isRelevantChange = isProjectsRootDiscoveryChange;
            watchTargets.push_back(std::move(target));
        }
        for (const ConfiguredProjectsSource& projectsSource : projectsSources_) {
            SourceWatchTarget target;
            target.sourceId = projectsSource.source.id;
            target.directory = projectsSource.source.path;
            target.isRecursive = true;
            const auto projectOption = std::ranges::find(config_.watchedProjects, projectsSource.source.id,
                                                         &WatchedProject::id);
            const bool followSymbolicLinks = projectOption != config_.watchedProjects.end() &&
                                             projectOption->followSymbolicLinks;
            target.followSymbolicLinks = followSymbolicLinks;
            auto changeFilter = std::make_shared<ProjectChangeFilter>(projectsSource.source.path,
                                                                       followSymbolicLinks);
            target.isRelevantChange = [changeFilter](const std::filesystem::path& relativePath) {
                return (*changeFilter)(relativePath);
            };
            watchTargets.push_back(std::move(target));
        }
        sourceWatcher_.start(watchTargets, config_.settings.debounceSeconds, [this](const std::string& sourceId) {
            Fl::awake(sourceChangedAwake, new SourceChangeNotification{this, sourceId});
        });
        const std::string status = "Watching " + std::to_string(sourceWatcher_.watchedSourceCount()) +
                                   " folders for changes.";
        footerStatus_->copy_label(status.c_str());
    }

    void handleSourceChanged(const std::string& sourceId) {
        if (!isProjectsRootWatchId(sourceId)) {
            markSourceDirty(sourceId);
            return;
        }
        if (isBackupRunning_) {
            isProjectsRefreshPending_ = true;
            return;
        }
        refreshProjectsFromRoots();
    }

    void refreshProjectsFromRoots() {
        restartSourceWatcher();
        initializeRouteStates();
        projectsPanel_->refresh();
        overviewPanel_->refresh();
        nextAutomaticAttempt_ = std::chrono::steady_clock::now();
        updateGlobalStatus();
    }

    void markSourceDirty(const std::string& sourceId) {
        std::size_t markedCount = 0;
        for (const BackupRoute& route : effectiveBackupRoutes(config_, projectsSources_)) {
            if (route.sourceId != sourceId) {
                continue;
            }
            stateStore_.markRouteDirty(sourceId, route.destinationId);
            ++markedCount;
        }
        if (markedCount > 0) {
            footerStatus_->copy_label("Change detected. Backup is pending.");
            overviewPanel_->refresh();
            nextAutomaticAttempt_ = std::chrono::steady_clock::now();
            updateGlobalStatus();
        }
    }

    void handleDestinationDevicesChanged() {
        destinationsPanel_->refresh();
        overviewPanel_->refresh();
        if (hasPendingBackupWork()) {
            if (isBackupRunning_) {
                isDestinationChangePending_ = true;
            } else {
                nextAutomaticAttempt_ = std::chrono::steady_clock::now();
            }
            footerStatus_->copy_label("Destination device changed. Checking pending backups.");
        }
        updateGlobalStatus();
    }

    void pauseFor(std::chrono::hours duration) {
        pauseUntil_ = std::chrono::steady_clock::now() + duration;
        const std::time_t resumeAt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() + duration);
        std::tm local{};
        localtime_s(&local, &resumeAt);
        std::ostringstream message;
        message << "Automatic backups paused until " << std::put_time(&local, "%H:%M") << ". Back up now remains available.";
        footerStatus_->copy_label(message.str().c_str());
        updateGlobalStatus();
    }

    void resumeAutomaticBackups() {
        pauseUntil_.reset();
        nextAutomaticAttempt_ = std::chrono::steady_clock::now();
        footerStatus_->copy_label("Automatic backups resumed.");
        updateGlobalStatus();
    }

    void checkAutomaticWork() {
        const auto now = std::chrono::steady_clock::now();
        if (pauseUntil_.has_value()) {
            if (now < *pauseUntil_) {
                return;
            }
            pauseUntil_.reset();
            updateGlobalStatus();
        }
        if (isBackupRunning_ || now < nextAutomaticAttempt_) {
            return;
        }
        if (!hasPendingBackupWork()) {
            nextAutomaticAttempt_ = std::chrono::steady_clock::time_point::max();
            return;
        }
        startBackupRun();
    }

    void startBackupRun() {
        if (isBackupRunning_) {
            return;
        }
        try {
            if (backupThread_.joinable()) {
                backupThread_.join();
            }
            isBackupRunning_ = true;
            runNowButton_->deactivate();
            footerStatus_->copy_label("Checking pending backups...");
            if (!hasVisibleWindow()) { backupNotification_.show("Checking pending backup work..."); }
            const BackupConfig configSnapshot = config_;
            const std::vector<ConfiguredProjectsSource> projectsSourcesSnapshot = projectsSources_;
            const auto deferredProjects = deferredProjects_;
            backupThread_ = std::thread([this, configSnapshot, projectsSourcesSnapshot, deferredProjects] {
                BackupPreparation preparation;
                preparation.largeFileThresholdBytes = configSnapshot.settings.largeFileThresholdBytes;
                try {
                    preparation.mirrorPlans =
                        backupEngine_.previewPendingMirrors(configSnapshot, projectsSourcesSnapshot, stateStore_);
                    preparation.archivePlans =
                        backupEngine_.previewPendingArchives(configSnapshot, projectsSourcesSnapshot, stateStore_);
                    deferredProjects.removeFrom(preparation.mirrorPlans);
                    deferredProjects.removeFrom(preparation.archivePlans);
                    std::vector<BackupPlan> plansToCheck = preparation.mirrorPlans;
                    plansToCheck.insert(plansToCheck.end(), preparation.archivePlans.begin(),
                                        preparation.archivePlans.end());
                    preparation.sizeWarnings =
                        backupEngine_.findSizeWarnings(configSnapshot, plansToCheck, stateStore_);
                } catch (const std::exception& error) {
                    preparation.error = error.what();
                }
                {
                    const std::scoped_lock lock(backupResultMutex_);
                    backupPreparation_ = std::move(preparation);
                }
                Fl::awake(backupPreparedAwake, this);
            });
        } catch (const std::exception& error) {
            isBackupRunning_ = false;
            runNowButton_->activate();
            backupNotification_.hide();
            footerStatus_->copy_label(error.what());
            reportError(error);
        }
    }

    void finishBackupPreparation() {
        if (backupThread_.joinable()) {
            backupThread_.join();
        }
        BackupPreparation preparation;
        {
            const std::scoped_lock lock(backupResultMutex_);
            preparation = std::move(*backupPreparation_);
            backupPreparation_.reset();
        }
        if (preparation.error.has_value()) {
            isBackupRunning_ = false;
            runNowButton_->activate();
            backupNotification_.hide();
            footerStatus_->copy_label(preparation.error->c_str());
            reportError(std::runtime_error(*preparation.error));
            return;
        }

        for (const SizeWarning& warning : preparation.sizeWarnings) {
            const std::vector<SizeWarning> folderWarning{warning};
            SizeApprovalDialog dialog{folderWarning};
            const SizeApprovalResult decision = dialog.show();
            if (decision == SizeApprovalResult::decideLater) {
                deferredProjects_.defer(warning.sourceId);
                deferredProjects_.removeFrom(preparation.mirrorPlans);
                deferredProjects_.removeFrom(preparation.archivePlans);
                continue;
            }
            const ProjectBackupDecision storedDecision = decision == SizeApprovalResult::alwaysAllow
                                                             ? ProjectBackupDecision::alwaysAllow
                                                             : ProjectBackupDecision::ignoreLargeFiles;
            stateStore_.setProjectBackupDecision(warning.sourceId, storedDecision, utcNowForApproval());
        }
        if (preparation.mirrorPlans.empty() && preparation.archivePlans.empty()) {
            isBackupRunning_ = false;
            runNowButton_->activate();
            backupNotification_.hide();
            const bool shouldCheckAgain = consumeDeferredRefreshes();
            if (!deferredProjects_.empty()) {
                footerStatus_->copy_label("Projects await your decision. Use Back up now to review them; other changes still back up automatically.");
            } else if (hasPendingBackupWork()) {
                footerStatus_->copy_label("Backup is pending. Waiting for a Source or Destination.");
            } else {
                footerStatus_->copy_label("No backup work is currently pending.");
            }
            nextAutomaticAttempt_ = shouldCheckAgain ? std::chrono::steady_clock::now()
                                                     : std::chrono::steady_clock::time_point::max();
            updateGlobalStatus();
            return;
        }

        const std::string status = "Backing up " + std::to_string(preparation.mirrorPlans.size()) +
                                   " Mirrors and " + std::to_string(preparation.archivePlans.size()) +
                                   " Zipped backups...";
        footerStatus_->copy_label(status.c_str());
        if (!hasVisibleWindow()) { backupNotification_.show(status); }
        updateGlobalStatus();
        backupThread_ = std::thread([this, preparation = std::move(preparation)] {
            BackupRunSummary result;
            try {
                result = backupEngine_.runMirrors(preparation.mirrorPlans, stateStore_, dataDirectory_ / L"logs",
                                                  preparation.largeFileThresholdBytes);
                BackupRunSummary archives = backupEngine_.runArchives(
                    preparation.archivePlans, stateStore_, preparation.largeFileThresholdBytes);
                result.succeeded += archives.succeeded;
                result.failed += archives.failed;
                result.messages.insert(result.messages.end(), std::make_move_iterator(archives.messages.begin()),
                                       std::make_move_iterator(archives.messages.end()));
            } catch (const std::exception& error) {
                result.failed = 1;
                result.messages.push_back(error.what());
            }
            {
                const std::scoped_lock lock(backupResultMutex_);
                backupResult_ = std::move(result);
            }
            Fl::awake(backupFinishedAwake, this);
        });
    }

    void finishBackupRun() {
        if (backupThread_.joinable()) {
            backupThread_.join();
        }
        BackupRunSummary result;
        {
            const std::scoped_lock lock(backupResultMutex_);
            result = std::move(*backupResult_);
            backupResult_.reset();
        }
        isBackupRunning_ = false;
        runNowButton_->activate();
        backupNotification_.hide();
        const bool shouldCheckAgain = consumeDeferredRefreshes();
        const std::string status = "Backup finished: " + std::to_string(result.succeeded) + " succeeded, " +
                                   std::to_string(result.failed) + " failed.";
        footerStatus_->copy_label(status.c_str());
        overviewPanel_->refresh();
        activityPanel_->refresh();
        nextAutomaticAttempt_ = shouldCheckAgain ? std::chrono::steady_clock::now()
                                                 : std::chrono::steady_clock::time_point::max();
        updateGlobalStatus();
        window_->redraw();
    }

    bool consumeDeferredRefreshes() {
        bool shouldCheckAgain = std::exchange(isDestinationChangePending_, false);
        if (std::exchange(isProjectsRefreshPending_, false)) {
            refreshProjectsFromRoots();
            shouldCheckAgain = true;
        }
        return shouldCheckAgain;
    }

    [[nodiscard]] bool isConfiguredRouteState(const RouteRuntimeState& state) const {
        return isActiveBackupRoute(config_, projectsSources_, state.sourceId, state.destinationId);
    }

    [[nodiscard]] bool hasPendingBackupWork() const {
        return std::ranges::any_of(stateStore_.routeStates(), [&](const RouteRuntimeState& state) {
            return state.isDirty && isConfiguredRouteState(state) && !deferredProjects_.contains(state.sourceId);
        });
    }

    void updateConfigurationSummary() {
        std::optional<std::string> latestSuccess;
        for (const RouteRuntimeState& state : stateStore_.routeStates()) {
            if (state.lastSuccessUtc.has_value() &&
                (!latestSuccess.has_value() || *state.lastSuccessUtc > *latestSuccess)) {
                latestSuccess = state.lastSuccessUtc;
            }
        }
        const std::size_t watchedCount = config_.manualSources.size() + projectsSources_.size();
        const std::string summary = std::to_string(watchedCount) + " watched · " +
                                    std::to_string(config_.destinations.size()) + " destinations · Last backup " +
                                    (latestSuccess.has_value() ? Ui::localTime(*latestSuccess) : "not run yet");
        configurationSummary_->copy_label(summary.c_str());
        window_->redraw();
    }

    void updateGlobalStatus() {
        const auto health = backupHealth(config_, projectsSources_, stateStore_.routeStates());
        UiTheme::BackupStatus status = health.failed ? UiTheme::BackupStatus::error :
            (health.pending ? UiTheme::BackupStatus::waiting : UiTheme::BackupStatus::current);
        std::string text = health.summary();
        if (isBackupRunning_) {
            text = "Backing up";
            status = UiTheme::BackupStatus::syncing;
        } else if (pauseUntil_) {
            text = health.failed ? "Paused · errors" : "Automatic work paused";
            status = UiTheme::BackupStatus::waiting;
        }
        if (pauseUntil_) { pauseMenu_->hide(); resumeButton_->show(); }
        else { resumeButton_->hide(); pauseMenu_->show(); }
        if (!isBackupRunning_ && !deferredProjects_.empty()) {
            text = "Needs approval";
            status = UiTheme::BackupStatus::waiting;
        }
        globalStatus_->copy_label(text.c_str());
        globalStatus_->labelcolor(UiTheme::statusColor(status));
        updateConfigurationSummary();
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

    DEV_BROADCAST_DEVICEINTERFACE_W deviceFilter{};
    deviceFilter.dbcc_size = sizeof(deviceFilter);
    deviceFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    deviceFilter.dbcc_classguid = GUID_DEVINTERFACE_VOLUME;
    deviceNotification_ = RegisterDeviceNotificationW(window_, &deviceFilter, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (deviceNotification_ == nullptr) {
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
    if (deviceNotification_ != nullptr) {
        UnregisterDeviceNotification(deviceNotification_);
        deviceNotification_ = nullptr;
    }
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
    if (message == WM_DEVICECHANGE &&
        (wordParameter == DBT_DEVICEARRIVAL || wordParameter == DBT_DEVICEREMOVECOMPLETE ||
         wordParameter == DBT_DEVNODES_CHANGED)) {
        if (longParameter == 0 || wordParameter == DBT_DEVNODES_CHANGED ||
            reinterpret_cast<const DEV_BROADCAST_HDR*>(longParameter)->dbch_devicetype == DBT_DEVTYP_VOLUME ||
            reinterpret_cast<const DEV_BROADCAST_HDR*>(longParameter)->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            deviceChangedCallback_();
        }
        return 0;
    }
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
        int argumentCount = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
        const bool isExplorerRegistration =
            arguments != nullptr && argumentCount == 2 &&
            std::wstring_view{arguments[1]} == L"--register-explorer-overlay";
        if (arguments != nullptr) {
            LocalFree(arguments);
        }
        if (isExplorerRegistration) {
            registerExplorerIntegrationMachineWide(applicationDataDirectory());
            return 0;
        }

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
