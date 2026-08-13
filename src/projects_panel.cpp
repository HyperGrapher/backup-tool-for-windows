#include "projects_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>

#include "config_store.hpp"
#include "native_file_dialog.hpp"
#include "ui_theme.hpp"

namespace {

constexpr int kRootColumnWidths[] = {410, 95, 95, 0};

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void styleButton(Fl_Button& button, bool isPrimary = false) {
    button.box(FL_BORDER_BOX);
    button.down_box(FL_BORDER_BOX);
    button.color(isPrimary ? UiTheme::kPrimary : UiTheme::kCard);
    button.selection_color(UiTheme::kSelection);
    button.labelcolor(isPrimary ? UiTheme::kPrimaryText : UiTheme::kText);
    button.labelsize(12);
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

}  // namespace

ProjectsPanel::ProjectsPanel(int x, int y, int width, int height, BackupConfig& config,
                             const ConfigStore& configStore, std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();
    addLabel(x + 20, y + 14, 300, 32, "Project Roots", 22, UiTheme::kText, FL_HELVETICA_BOLD);
    addLabel(x + 20, y + 45, width - 40, 34,
             "Only immediate child folders containing .backup-watch are backed up.", 11, UiTheme::kMutedText);

    auto* addButton = new Fl_Button(x + 20, y + 84, 135, 34, "Add roots");
    styleButton(*addButton, true);
    addButton->callback(addRootsCallback, this);
    removeButton_ = new Fl_Button(x + 165, y + 84, 145, 34, "Remove selected");
    styleButton(*removeButton_);
    removeButton_->callback(removeCallback, this);

    addLabel(x + 330, y + 84, 55, 34, "Send to", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    destinationChoice_ = new Fl_Choice(x + 385, y + 84, 190, 34);
    destinationChoice_->box(FL_BORDER_BOX);
    destinationChoice_->color(UiTheme::kCard);
    destinationChoice_->textcolor(UiTheme::kText);
    connectButton_ = new Fl_Button(x + 585, y + 84, 92, 34, "Connect");
    styleButton(*connectButton_, true);
    connectButton_->callback(connectCallback, this);
    disconnectButton_ = new Fl_Button(x + 585, y + 124, 92, 30, "Disconnect");
    styleButton(*disconnectButton_);
    disconnectButton_->callback(disconnectCallback, this);

    addLabel(x + 24, y + 166, 390, 24, "Root folder", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 434, y + 166, 85, 24, "Opted in", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 529, y + 166, 90, 24, "Destinations", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    rootBrowser_ = new Fl_Multi_Browser(x + 20, y + 191, width - 40, height - 241);
    rootBrowser_->box(FL_BORDER_BOX);
    rootBrowser_->color(UiTheme::kCard);
    rootBrowser_->textcolor(UiTheme::kText);
    rootBrowser_->selection_color(UiTheme::kSelection);
    rootBrowser_->column_widths(kRootColumnWidths);
    rootBrowser_->column_char('\t');
    rootBrowser_->format_char(0);
    rootBrowser_->callback(selectionCallback, this);
    resultSummary_ = addLabel(x + 20, y + height - 42, width - 40, 28, "", 11, UiTheme::kMutedText);
    end();
    resizable(rootBrowser_);
    refresh();
}

void ProjectsPanel::refresh() {
    discovery_ = discoverConfiguredProjects(config_.projectsRoots);
    destinationChoice_->clear();
    for (const Destination& destination : config_.destinations) {
        destinationChoice_->add(destination.name.c_str());
    }
    if (config_.destinations.empty()) {
        destinationChoice_->add("Add a Destination first");
    }
    destinationChoice_->value(0);
    rootBrowser_->clear();
    for (const ProjectsRoot& root : config_.projectsRoots) {
        const auto optedIn = std::ranges::count_if(discovery_.sources, [&](const ConfiguredProjectsSource& source) {
            return source.rootId == root.id;
        });
        const auto routeCount = std::ranges::count(config_.routes, root.id, &BackupRoute::sourceId);
        const std::string line = pathToUtf8(root.path) + '\t' + std::to_string(optedIn) + '\t' +
                                 std::to_string(routeCount);
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
void ProjectsPanel::connectCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->connectSelectedRoots(); }
void ProjectsPanel::disconnectCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->disconnectSelectedRoots(); }
void ProjectsPanel::selectionCallback(Fl_Widget*, void* context) { static_cast<ProjectsPanel*>(context)->refreshSelectionState(); }

void ProjectsPanel::addRoots() {
    try {
        const std::vector<std::filesystem::path> paths = selectFolders(fl_xid(window()));
        if (paths.empty()) {
            return;
        }
        BackupConfig updated = config_;
        for (const std::filesystem::path& path : paths) {
            const auto normalized = std::filesystem::absolute(path).lexically_normal();
            if (!std::ranges::any_of(updated.projectsRoots, [&](const ProjectsRoot& root) { return root.path == normalized; })) {
                updated.projectsRoots.push_back(ProjectsRoot{generateStableId("projects-root"), normalized});
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

void ProjectsPanel::connectSelectedRoots() {
    const std::vector<std::string> ids = selectedRootIds();
    const int destinationIndex = destinationChoice_->value();
    if (ids.empty() || destinationIndex < 0 || config_.destinations.empty()) {
        return;
    }
    try {
        BackupConfig updated = config_;
        const std::string destinationId = updated.destinations.at(static_cast<std::size_t>(destinationIndex)).id;
        for (const std::string& id : ids) {
            if (!std::ranges::any_of(updated.routes, [&](const BackupRoute& route) {
                    return route.sourceId == id && route.destinationId == destinationId;
                })) {
                updated.routes.push_back(BackupRoute{id, destinationId, true, true, {}});
            }
        }
        configStore_.save(updated);
        config_ = std::move(updated);
        configChangedCallback_();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void ProjectsPanel::disconnectSelectedRoots() {
    const std::vector<std::string> ids = selectedRootIds();
    const int destinationIndex = destinationChoice_->value();
    if (ids.empty() || destinationIndex < 0 || config_.destinations.empty()) {
        return;
    }
    try {
        const std::unordered_set<std::string> selected(ids.begin(), ids.end());
        const std::string destinationId = config_.destinations.at(static_cast<std::size_t>(destinationIndex)).id;
        BackupConfig updated = config_;
        std::erase_if(updated.routes, [&](const BackupRoute& route) {
            return selected.contains(route.sourceId) && route.destinationId == destinationId;
        });
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
    if (hasSelection && !config_.destinations.empty()) {
        connectButton_->activate();
        disconnectButton_->activate();
    } else {
        connectButton_->deactivate();
        disconnectButton_->deactivate();
    }
}

void ProjectsPanel::reportError(const std::exception& error) const { fl_alert("%s", error.what()); }
