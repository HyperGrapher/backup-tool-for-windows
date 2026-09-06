#include "projects_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>

#include "config_store.hpp"
#include "native_file_dialog.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"

namespace {

constexpr int kRootColumnWidths[] = {128, 90, 380, 90, 100, 0};

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void styleButton(Fl_Button& button, bool isPrimary = false, bool isDanger = false) {
    button.box(FL_FLAT_BOX);
    button.down_box(FL_FLAT_BOX);
    button.color(isPrimary ? UiTheme::kPrimary : (isDanger ? UiTheme::kDanger : UiTheme::kControl));
    button.down_color(isPrimary ? UiTheme::kPrimaryPressed
                                : (isDanger ? UiTheme::kDangerPressed : UiTheme::kPressedControl));
    button.selection_color(isPrimary ? UiTheme::kPrimary : (isDanger ? UiTheme::kDanger : UiTheme::kSelection));
    button.labelcolor(UiTheme::kText);
    button.labelfont(isPrimary ? UiTheme::kUiFontSemibold : UiTheme::kUiFont);
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

[[nodiscard]] UiTheme::BackupStatus rootStatus(const ProjectsRoot& root,
                                                const ConfiguredProjectsDiscovery& discovery,
                                                const BackupConfig& config, const StateStore& stateStore) {
    bool hasRoute = false;
    UiTheme::BackupStatus status = UiTheme::BackupStatus::current;
    for (const BackupRoute& route : config.routes) {
        if (route.sourceId != root.id) {
            continue;
        }
        hasRoute = true;
        for (const ConfiguredProjectsSource& source : discovery.sources) {
            if (source.rootId != root.id) {
                continue;
            }
            const std::optional<RouteRuntimeState> state =
                stateStore.routeState(source.source.id, route.destinationId);
            if (!state.has_value() || state->isDirty) {
                status = UiTheme::BackupStatus::waiting;
            }
            if (state.has_value() && state->status == RouteStatus::running) {
                status = UiTheme::BackupStatus::syncing;
            }
            if (state.has_value() && state->status == RouteStatus::error) {
                return UiTheme::BackupStatus::error;
            }
        }
    }
    return hasRoute ? status : UiTheme::BackupStatus::inactive;
}

}  // namespace

ProjectsPanel::ProjectsPanel(int x, int y, int width, int height, BackupConfig& config,
                             const ConfigStore& configStore, const StateStore& stateStore,
                             std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      stateStore_(stateStore), configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();
    addLabel(x + 16, y + 10, 220, 28, "Projects", 18, UiTheme::kText, UiTheme::kUiFontSemibold);
    addLabel(x + 16, y + 36, width - 32, 20,
             "Folders at any depth opt in with .backup-watch. Git repositories and generated folders are excluded.",
             11, UiTheme::kSecondaryText);

    auto* addButton = new Fl_Button(x + 16, y + 66, 112, 30, "Add roots");
    styleButton(*addButton, true);
    addButton->callback(addRootsCallback, this);
    removeButton_ = new Fl_Button(x + 136, y + 66, 132, 30, "Remove selected");
    styleButton(*removeButton_, false, true);
    removeButton_->callback(removeCallback, this);

    addLabel(x + 20, y + 108, 124, 22, "State", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 148, y + 108, 86, 22, "Backup", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 238, y + 108, 370, 22, "Root folder", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 618, y + 108, 86, 22, "Projects", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 708, y + 108, 100, 22, "Destinations", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    rootBrowser_ = new Fl_Multi_Browser(x + 16, y + 130, width - 32, height - 162);
    rootBrowser_->box(FL_BORDER_BOX);
    rootBrowser_->color(UiTheme::kSurface);
    rootBrowser_->textcolor(UiTheme::kText);
    rootBrowser_->selection_color(UiTheme::kSelection);
    rootBrowser_->textfont(UiTheme::kUiFont);
    rootBrowser_->textsize(12);
    rootBrowser_->column_widths(kRootColumnWidths);
    rootBrowser_->column_char('\t');
    rootBrowser_->format_char(0);
    rootBrowser_->callback(selectionCallback, this);
    resultSummary_ = addLabel(x + 16, y + height - 28, width - 32, 20, "", 11, UiTheme::kSecondaryText);
    end();
    resizable(rootBrowser_);
    refresh();
}

void ProjectsPanel::refresh() {
    discovery_ = discoverConfiguredProjects(config_.projectsRoots);
    rootBrowser_->clear();
    for (const ProjectsRoot& root : config_.projectsRoots) {
        const auto optedIn = std::ranges::count_if(discovery_.sources, [&](const ConfiguredProjectsSource& source) {
            return source.rootId == root.id;
        });
        const std::string modeText = root.backupMode == BackupMode::mirror ? "Mirror" : "Zipped";
        const std::string line = std::string{UiTheme::statusText(rootStatus(root, discovery_, config_, stateStore_))} +
                                 '\t' + modeText + '\t' + pathToUtf8(root.path) + '\t' +
                                 std::to_string(optedIn) + '\t' +
                                 std::to_string(config_.destinations.size());
        rootBrowser_->add(line.c_str());
    }
    const std::string summary = std::to_string(config_.projectsRoots.size()) + " Roots, " +
                                std::to_string(discovery_.sources.size()) + " opted-in Projects";
    resultSummary_->copy_label(summary.c_str());
    refreshSelectionState();
    redraw();
}

void ProjectsPanel::addRootsCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->addRoots(); }
void ProjectsPanel::removeCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->removeSelectedRoots(); }
void ProjectsPanel::selectionCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->refreshSelectionState(); }

void ProjectsPanel::addRoots() {
    try {
        const std::vector<std::filesystem::path> paths = selectFolders(fl_xid(window()));
        if (paths.empty()) {
            return;
        }
        const int modeChoice = fl_choice(
            "How should Projects under these Roots be backed up?\n\nMirror keeps uncompressed folder copies.\nZipped creates best-compression ZIP archives.",
            "Cancel", "Mirror", "Zipped");
        if (modeChoice == 0) {
            return;
        }
        const BackupMode backupMode = modeChoice == 1 ? BackupMode::mirror : BackupMode::zipped;
        BackupConfig updated = config_;
        for (const std::filesystem::path& path : paths) {
            const auto normalized = std::filesystem::absolute(path).lexically_normal();
            if (!std::ranges::any_of(updated.projectsRoots, [&](const ProjectsRoot& root) { return root.path == normalized; })) {
                updated.projectsRoots.push_back(
                    ProjectsRoot{generateStableId("projects-root"), normalized, backupMode});
            }
        }
        configStore_.save(updated);
        config_ = std::move(updated);
        configChangedCallback_();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

std::vector<std::string> ProjectsPanel::selectedRootIds() const {
    std::vector<std::string> ids;
    for (int line = 1; line <= rootBrowser_->size(); ++line) {
        if (rootBrowser_->selected(line) != 0) {
            ids.push_back(config_.projectsRoots.at(static_cast<std::size_t>(line - 1)).id);
        }
    }
    return ids;
}

void ProjectsPanel::removeSelectedRoots() {
    const std::vector<std::string> ids = selectedRootIds();
    if (ids.empty() || fl_choice("Remove the selected Project Roots? Existing backups stay untouched.", "Cancel",
                                 "Remove", nullptr) != 1) {
        return;
    }
    try {
        const std::unordered_set<std::string> selected(ids.begin(), ids.end());
        BackupConfig updated = config_;
        std::erase_if(updated.projectsRoots, [&](const ProjectsRoot& root) { return selected.contains(root.id); });
        std::erase_if(updated.routes, [&](const BackupRoute& route) { return selected.contains(route.sourceId); });
        configStore_.save(updated);
        config_ = std::move(updated);
        configChangedCallback_();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ProjectsPanel::refreshSelectionState() {
    const bool hasSelection = !selectedRootIds().empty();
    hasSelection ? removeButton_->activate() : removeButton_->deactivate();
}

void ProjectsPanel::reportError(const std::exception& error) const { fl_alert("%s", error.what()); }
