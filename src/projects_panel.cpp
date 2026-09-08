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
#include "backup_mode_dialog.hpp"
#include "backup_options_dialog.hpp"
#include "native_file_dialog.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"
#include "ui_controls.hpp"
#include "ui_helpers.hpp"
#include "ui_table.hpp"
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>

namespace {


[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

using Ui::styleButton;

[[nodiscard]] std::string destinationSummary(const BackupConfig& config, std::string_view sourceId,
                                             std::string_view fallbackRootId = {}) {
    std::string result;
    const bool hasExplicitRoutes = std::ranges::any_of(config.routes, [&](const BackupRoute& route) {
        return route.sourceId == sourceId;
    }) || std::ranges::any_of(config.watchedProjects, [&](const WatchedProject& project) {
        return project.id == sourceId;
    });
    for (const BackupRoute& route : config.routes) {
        if (route.sourceId != sourceId && (hasExplicitRoutes || fallbackRootId.empty() || route.sourceId != fallbackRootId)) {
            continue;
        }
        const auto destination = std::ranges::find(config.destinations, route.destinationId, &Destination::id);
        if (destination == config.destinations.end()) {
            continue;
        }
        if (!result.empty()) {
            result += ", ";
        }
        result += destination->name;
    }
    return result.empty() ? "No destinations selected" : result;
}

}  // namespace

ProjectsPanel::ProjectsPanel(int x, int y, int width, int height, BackupConfig& config,
                             const ConfigStore& configStore, StateStore& stateStore,
                             std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      stateStore_(stateStore), configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();
    Ui::label(x + 24, y + 16, width - 48, 36, "Projects", 24, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 56, width - 48, 44,
              "Choose a parent folder, then watch projects and choose where each one is copied.", 14, UiTheme::kSecondaryText);
    auto* add = new ActionButton(x + 24, y + 108, 180, 36, "Add parent folder");
    styleButton(*add, true);
    add->callback(addRootsCallback, this);
    removeButton_ = new ActionButton(x + 216, y + 108, 192, 36, "Remove parent…");
    styleButton(*removeButton_, false, true);
    removeButton_->callback(removeCallback, this);
    auto* help = new ActionButton(x + 420, y + 108, 112, 36, "How it works");
    help->callback([](Fl_Widget*, void*) {
        Ui::showDetails("Watching creates a .backup-watch marker in the selected folder.\n\n"
                        "All watched projects use their parent folder's backup mode. Each project can choose its destinations.\n\n"
                        "Entire Git repository folders, generated folders such as build and node_modules, junctions, "
                        "and paths matched by .backup-ignore are excluded.\n\n"
                        "Removing a parent stops its backups; existing copies and marker files remain.", "Watching projects");
    });
    parentChoice_ = new Fl_Choice(x + 24, y + 156, width - 48, 36);
    parentChoice_->when(FL_WHEN_CHANGED);
    parentChoice_->callback([](Fl_Widget*, void* context) {
        auto* panel = static_cast<ProjectsPanel*>(context);
        const int index = panel->parentChoice_->value();
        if (index >= 0 && index < static_cast<int>(panel->parentIds_.size())) {
            panel->selectedParentId_ = panel->parentIds_[index];
        }
        panel->refreshProjectRows();
    }, this);
    filterChoice_ = new Fl_Choice(x + 24, y + 204, 188, 36);
    filterChoice_->add("Watched projects");
    filterChoice_->add("Available to watch");
    filterChoice_->value(0);
    filterChoice_->callback([](Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->refreshProjectRows(); }, this);
    for (auto* choice : {parentChoice_, filterChoice_}) {
        choice->box(FL_BORDER_BOX);
        choice->color(UiTheme::kSurface);
        choice->textcolor(UiTheme::kText);
        choice->textfont(UiTheme::kUiFont);
        choice->textsize(13);
        choice->selection_color(UiTheme::kSelection);
    }
    Ui::label(x + 224, y + 204, 56, 36, "Search", 12, UiTheme::kSecondaryText);
    searchInput_ = new Fl_Input(x + 284, y + 204, width - 308, 36);
    searchInput_->color(UiTheme::kSurface);
    searchInput_->textcolor(UiTheme::kText);
    searchInput_->cursor_color(UiTheme::kText);
    searchInput_->textfont(UiTheme::kUiFont);
    searchInput_->textsize(14);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback([](Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->refreshProjectRows(); }, this);
    watchButton_ = new ActionButton(x + 24, y + 252, 208, 36, "Watch selected folders");
    styleButton(*watchButton_, true);
    watchButton_->callback(watchCallback, this);
    auto* details = new ActionButton(x + 244, y + 252, 112, 36, "Edit details");
    details->callback(detailsCallback, this);
    rootBrowser_ = new DataTable(x + 24, y + 300, width - 48, height - 356,
                                 {"Project / path", "State", "Backup mode"}, {55, 25, 20});
    rootBrowser_->callback(selectionCallback, this);
    resultSummary_ = Ui::label(x + 24, y + height - 48, width - 48, 40, "", 12, UiTheme::kSecondaryText);
    end();
    resizable(rootBrowser_);
    refresh();
}

void ProjectsPanel::refresh() {
    try {
        discovery_ = discoverConfiguredProjects(config_.projectsRoots);
        parentChoice_->clear();
        parentIds_.clear();
        int selectedIndex = 0;
        for (const auto& root : config_.projectsRoots) {
            if (root.id == selectedParentId_) { selectedIndex = static_cast<int>(parentIds_.size()); }
            parentIds_.push_back(root.id);
            const std::string label = pathToUtf8(root.path) + (root.backupMode == BackupMode::mirror ? " · Mirror" : " · Zipped");
            // Add the final label directly. Replacing a placeholder leaves Fl_Menu_Item's
            // cached value pointing at the placeholder on some FLTK 1.4 builds.
            parentChoice_->add(label.c_str());
        }
        if (parentIds_.empty()) {
            parentChoice_->add("Choose Add parent folder to begin");
            selectedParentId_.clear();
            parentChoice_->deactivate();
        } else {
            selectedParentId_ = parentIds_[selectedIndex];
            parentChoice_->activate();
        }
        parentChoice_->value(selectedIndex);
        refreshProjectRows();
    } catch (const std::exception& error) { reportError(error); }
}

void ProjectsPanel::refreshProjectRows() {
    std::vector<TableRow> rows;
    const auto root = std::ranges::find(config_.projectsRoots, selectedParentId_, &ProjectsRoot::id);
    const std::string query = searchInput_->value();
    if (root != config_.projectsRoots.end()) {
        const std::string mode = root->backupMode == BackupMode::mirror ? "Mirror" : "Zipped";
        if (filterChoice_->value() == 0) {
            for (const auto& project : discovery_.sources) {
                const std::string path = pathToUtf8(project.source.path);
                if (project.rootId != root->id || (!query.empty() && path.find(query) == std::string::npos)) { continue; }
                const auto projectOption = std::ranges::find(config_.watchedProjects, project.source.id,
                                                             &WatchedProject::id);
                rows.push_back({project.source.id, {pathToUtf8(project.source.path.filename()) + "\n" + path, "Watched", mode},
                               path + "\nWatched · " + mode +
                               (projectOption != config_.watchedProjects.end() && projectOption->followSymbolicLinks
                                    ? " · Following links" : " · Links skipped") +
                               "\nDestinations: " + destinationSummary(config_, project.source.id, root->id)});
            }
        } else {
            for (const auto& folder : discovery_.eligibleFolders) {
                const std::string path = pathToUtf8(folder.path);
                if (folder.rootId != root->id || (!query.empty() && path.find(query) == std::string::npos)) { continue; }
                rows.push_back({path, {pathToUtf8(folder.path.filename()) + "\n" + path, "Available to watch", mode}, path});
            }
        }
    }
    const auto count = rows.size();
    rootBrowser_->setRows(std::move(rows));
    rootBrowser_->emptyMessage(root == config_.projectsRoots.end() ? "Choose a folder containing your projects."
        : (!query.empty() ? "No projects match this search. Clear Search to see all results."
        : (filterChoice_->value() == 0 ? "No projects watched here yet. Choose Available to watch to select folders."
                                      : "No eligible folders found. Open How it works to review the exclusions.")));
    const std::string summary = std::to_string(count) + " projects shown · Watching creates a .backup-watch file";
    resultSummary_->copy_label(summary.c_str());
    refreshSelectionState();
}

void ProjectsPanel::addRootsCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->addRoots(); }
void ProjectsPanel::watchCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->watchSelectedFolders(); }
void ProjectsPanel::removeCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->removeSelectedRoots(); }
void ProjectsPanel::selectionCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->refreshSelectionState(); }
void ProjectsPanel::detailsCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->editSelectedProject(); }

void ProjectsPanel::addRoots() {
    try {
        const std::vector<std::filesystem::path> paths = selectFolders(fl_xid(window()));
        if (paths.empty()) {
            return;
        }
        const auto selectedMode = chooseBackupMode(paths.size());
        if (!selectedMode.has_value()) {
            return;
        }
        const BackupMode backupMode = *selectedMode;
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
    return selectedParentId_.empty() ? std::vector<std::string>{} : std::vector<std::string>{selectedParentId_};
}

std::vector<std::filesystem::path> ProjectsPanel::selectedEligibleFolders() const {
    std::vector<std::filesystem::path> folders;
    if (filterChoice_->value() != 1) { return folders; }
    const auto keys = rootBrowser_->selectedKeys();
    for (const auto& folder : discovery_.eligibleFolders) {
        if (folder.rootId == selectedParentId_ && std::ranges::find(keys, pathToUtf8(folder.path)) != keys.end()) {
            folders.push_back(folder.path);
        }
    }
    return folders;
}

void ProjectsPanel::watchSelectedFolders() {
    const std::vector<std::filesystem::path> folders = selectedEligibleFolders();
    if (folders.empty()) {
        return;
    }

    const auto root = std::ranges::find(config_.projectsRoots, selectedParentId_, &ProjectsRoot::id);
    if (root == config_.projectsRoots.end()) {
        return;
    }
    BackupItemOptionsDialogInput dialogInput;
    dialogInput.fixedBackupMode = root->backupMode;
    const auto options = chooseBackupItemOptions(config_, folders.size(), std::move(dialogInput));
    if (!options.has_value()) {
        return;
    }

    std::string firstError;
    std::size_t createdCount = 0;
    BackupConfig updatedConfig = config_;
    for (const std::filesystem::path& folder : folders) {
        try {
            const std::string projectId = createProjectWatchMarker(folder);
            updatedConfig.watchedProjects.push_back(WatchedProject{projectId, options->followSymbolicLinks});
            for (const std::string& destinationId : options->destinationIds) {
                updatedConfig.routes.push_back(BackupRoute{projectId, destinationId});
            }
            ++createdCount;
        } catch (const std::exception& error) {
            firstError += pathToUtf8(folder) + ": " + error.what() + "\n";
        }
    }
    if (createdCount > 0) {
        try {
            configStore_.save(updatedConfig);
            config_ = std::move(updatedConfig);
            configChangedCallback_();
        } catch (const std::exception& error) {
            reportError(error);
            return;
        }
    }
    const std::string receipt = "Now watching " + std::to_string(createdCount) + " folders.";
    resultSummary_->copy_label(receipt.c_str());
    if (!firstError.empty()) { Ui::showDetails(receipt + "\n\nCould not watch:\n" + firstError, "Some folders need attention"); }
}

void ProjectsPanel::editSelectedProject() {
    if (filterChoice_->value() != 0) {
        resultSummary_->copy_label("Switch to Watched projects to edit a project.");
        return;
    }
    const auto selected = rootBrowser_->selectedKeys();
    if (selected.size() != 1) {
        resultSummary_->copy_label(selected.empty() ? "Select one project to edit its options."
                                                     : "Select one project at a time to edit its options.");
        return;
    }
    const auto project = std::ranges::find(discovery_.sources, selected.front(),
                                          [](const ConfiguredProjectsSource& source) { return source.source.id; });
    const auto root = std::ranges::find(config_.projectsRoots, selectedParentId_, &ProjectsRoot::id);
    if (project == discovery_.sources.end() || root == config_.projectsRoots.end()) {
        return;
    }

    BackupItemOptions initial;
    initial.backupMode = root->backupMode;
    const auto projectOption = std::ranges::find(config_.watchedProjects, project->source.id,
                                                 &WatchedProject::id);
    initial.followSymbolicLinks = projectOption != config_.watchedProjects.end() &&
                                  projectOption->followSymbolicLinks;
    for (const BackupRoute& route : config_.routes) {
        if (route.sourceId == project->source.id) {
            initial.destinationIds.push_back(route.destinationId);
        }
    }
    if (initial.destinationIds.empty() && projectOption == config_.watchedProjects.end()) {
        for (const BackupRoute& route : config_.routes) {
            if (route.sourceId == root->id) {
                initial.destinationIds.push_back(route.destinationId);
            }
        }
    }
    const auto options = chooseBackupItemOptions(
        config_, 1, BackupItemOptionsDialogInput{root->backupMode, initial, true});
    if (!options.has_value()) {
        return;
    }

    try {
        BackupConfig updatedConfig = config_;
        const auto updatedOption = std::ranges::find(updatedConfig.watchedProjects, project->source.id,
                                                     &WatchedProject::id);
        if (updatedOption == updatedConfig.watchedProjects.end()) {
            updatedConfig.watchedProjects.push_back(
                WatchedProject{project->source.id, options->followSymbolicLinks});
        } else {
            updatedOption->followSymbolicLinks = options->followSymbolicLinks;
        }
        std::erase_if(updatedConfig.routes, [&](const BackupRoute& route) {
            return route.sourceId == project->source.id;
        });
        for (const std::string& destinationId : options->destinationIds) {
            updatedConfig.routes.push_back(BackupRoute{project->source.id, destinationId});
        }
        const std::string projectId = project->source.id;
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        for (const std::string& destinationId : options->destinationIds) {
            if (initial.followSymbolicLinks != options->followSymbolicLinks ||
                std::ranges::find(initial.destinationIds, destinationId) == initial.destinationIds.end()) {
                stateStore_.markRouteDirty(projectId, destinationId);
            }
        }
        configChangedCallback_();
        refresh();
        resultSummary_->copy_label("Project options saved.");
    } catch (const std::exception& error) {
        reportError(error);
    }
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
        std::unordered_set<std::string> removedProjectIds;
        for (const ConfiguredProjectsSource& project : discovery_.sources) {
            if (selected.contains(project.rootId)) {
                removedProjectIds.insert(project.source.id);
            }
        }
        std::erase_if(updated.projectsRoots, [&](const ProjectsRoot& root) { return selected.contains(root.id); });
        std::erase_if(updated.watchedProjects, [&](const WatchedProject& project) {
            return removedProjectIds.contains(project.id);
        });
        std::erase_if(updated.routes, [&](const BackupRoute& route) { return selected.contains(route.sourceId); });
        std::erase_if(updated.routes, [&](const BackupRoute& route) {
            return removedProjectIds.contains(route.sourceId);
        });
        configStore_.save(updated);
        config_ = std::move(updated);
        configChangedCallback_();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ProjectsPanel::refreshSelectionState() {
    if (selectedParentId_.empty()) { removeButton_->deactivate(); }
    else { removeButton_->activate(); }
    if (selectedEligibleFolders().empty()) { watchButton_->deactivate(); }
    else { watchButton_->activate(); }
}

void ProjectsPanel::reportError(const std::exception& error) const { resultSummary_->copy_label(error.what()); resultSummary_->labelcolor(UiTheme::kError); }
