#include "overview_panel.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>

#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/fl_ask.H>

#include "clear_history_button.hpp"
#include "connected_volume.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"
#include "ui_helpers.hpp"
#include "backup_health.hpp"

namespace {

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

Fl_Box* addStatusRow(int x, int y, int width, const char* title) {
    addLabel(x, y, 180, 34, title, 12, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    Fl_Box* value = addLabel(x + 180, y, width - 180, 34, "", 12, UiTheme::kText);
    auto* divider = new Fl_Box(x, y + 33, width, 1);
    divider->box(FL_FLAT_BOX);
    divider->color(UiTheme::kBorder);
    return value;
}

[[nodiscard]] bool isDestinationAvailable(const Destination& destination,
                                          const std::vector<ConnectedVolume>& connectedVolumes) {
    if (destination.kind == DestinationKind::path) {
        return std::filesystem::exists(destination.root);
    }
    return std::ranges::any_of(connectedVolumes, [&](const ConnectedVolume& volume) {
        return volume.serial == destination.volumeSerial;
    });
}

}  // namespace

OverviewPanel::OverviewPanel(int x, int y, int width, int height, const BackupConfig& config,
                             const std::vector<ConfiguredProjectsSource>& projectsSources,
                             StateStore& stateStore, std::function<void()> openSources,
                             std::function<void()> openDestinations)
    : Fl_Group(x, y, width, height), config_(config), projectsSources_(projectsSources),
      stateStore_(stateStore), openSources_(std::move(openSources)), openDestinations_(std::move(openDestinations)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    Ui::label(x + 24, y + 16, width - 48, 36, "Overview", 24, UiTheme::kText, UiTheme::kUiFontSemibold);
    healthTitle_ = Ui::label(x + 24, y + 72, width - 48, 32, "", 20, UiTheme::kPrimary, UiTheme::kUiFontSemibold);
    healthHint_ = Ui::label(x + 24, y + 110, width - 48, 48, "", 14, UiTheme::kSecondaryText);
    auto* sources = new ActionButton(x + 24, y + 170, 168, 36, "Choose sources");
    Ui::styleButton(*sources, true);
    sources->callback([](Fl_Widget*, void* context) { static_cast<OverviewPanel*>(context)->openSources_(); }, this);
    auto* destinations = new ActionButton(x + 204, y + 170, 180, 36, "View destinations");
    destinations->callback([](Fl_Widget*, void* context) { static_cast<OverviewPanel*>(context)->openDestinations_(); }, this);
    const int rowX = x + 24;
    const int rowWidth = width - 48;
    destinationHealth_ = addStatusRow(rowX, y + 226, rowWidth, "Destinations");
    pendingRoutes_ = addStatusRow(rowX, y + 260, rowWidth, "Pending backups");
    watcherStatus_ = addStatusRow(rowX, y + 294, rowWidth, "Configured sources");
    lastBackup_ = addStatusRow(rowX, y + 328, rowWidth, "Last successful backup");
    lastArchive_ = addStatusRow(rowX, y + 362, rowWidth, "Last Zipped backup");
    Ui::label(x + 24, y + 398, width - 88, 28, "Recent errors · history, not current health", 13, UiTheme::kSecondaryText);
    clearFailuresButton_ = new ClearHistoryButton(x + width - 56, y + 396, 32, "Clear error history");
    clearFailuresButton_->callback(clearFailuresCallback, this);
    recentFailures_ = new Fl_Browser(x + 24, y + 432, width - 48, std::max(40, height - 448));
    recentFailures_->box(FL_BORDER_BOX);
    recentFailures_->color(UiTheme::kSurface);
    recentFailures_->textcolor(UiTheme::kText);
    recentFailures_->selection_color(UiTheme::kSelection);
    recentFailures_->textsize(11);
    recentFailures_->textfont(UiTheme::kMonoFont);
    static constexpr int kFailureColumnWidths[] = {150, 0};
    recentFailures_->column_widths(kFailureColumnWidths);
    recentFailures_->column_char('\t');
    recentFailures_->format_char(0);

    end();
    resizable(recentFailures_);
    refresh();
}

void OverviewPanel::refresh() {
    std::vector<ConnectedVolume> connectedVolumes;
    bool availabilityKnown = true;
    try { connectedVolumes = findConnectedRemovableVolumes(); }
    catch (const std::exception&) { availabilityKnown = false; }
    std::size_t availableCount = 0;
    for (const auto& destination : config_.destinations) {
        try { if (isDestinationAvailable(destination, connectedVolumes)) { ++availableCount; } }
        catch (const std::exception&) { availabilityKnown = false; }
    }
    const auto health = backupHealth(config_, projectsSources_, stateStore_.routeStates());
    std::string title = health.summary();
    std::string hint;
    const auto sourceCount = config_.manualSources.size() + projectsSources_.size();
    if (sourceCount == 0) {
        hint = "Choose files or folders to back up, or watch a project from the Projects page.";
    } else if (config_.destinations.empty()) {
        hint = "Add a destination for your copies. Backups start automatically when both are ready.";
    } else if (!availabilityKnown) {
        title = "Availability unknown";
        hint = "Drive availability could not be checked. Refresh the Destinations page to try again.";
    } else if (availableCount < config_.destinations.size()) {
        if (health.failed == 0) { title = "Waiting for a destination"; }
        hint = "Open Destinations to see which locations are disconnected. Their pending work stays queued.";
    } else if (health.failed) {
        hint = std::to_string(health.failed) + " backups need attention. Review their details in Activity, then try Back up now.";
    } else if (health.neverRun) {
        hint = std::to_string(health.neverRun) + " source-to-destination copies have not completed their first backup.";
    } else if (health.pending) {
        hint = "Your changes are queued. Automatic backups run after edits settle, unless paused.";
    } else {
        hint = "No known changes are waiting. New edits will be copied automatically.";
    }
    healthTitle_->copy_label(title.c_str());
    healthTitle_->labelcolor(health.failed ? UiTheme::kError : UiTheme::kPrimary);
    healthHint_->copy_label(hint.c_str());
    const std::string destinationText = config_.destinations.empty() ? "None added yet" :
        (availabilityKnown ? std::to_string(availableCount) + " of " + std::to_string(config_.destinations.size()) + " available" : "Could not check availability");
    destinationHealth_->copy_label(destinationText.c_str());
    destinationHealth_->labelcolor(UiTheme::kText);
    const std::string pending = std::to_string(health.pending) + " source-to-destination copies";
    pendingRoutes_->copy_label(pending.c_str());
    const std::string watched = std::to_string(sourceCount) + " files, folders and opted-in projects";
    watcherStatus_->copy_label(watched.c_str());
    const std::string lastSuccess = health.lastSuccess ? Ui::localTime(*health.lastSuccess) : "No successful backup yet";
    lastBackup_->copy_label(lastSuccess.c_str());
    const auto archive = stateStore_.latestArchiveUtc();
    const std::string lastArchive = archive ? Ui::localTime(*archive) : "No archive created yet";
    lastArchive_->copy_label(lastArchive.c_str());

    recentFailures_->clear();
    std::size_t failureCount = 0;
    for (const ActivityRecord& record : stateStore_.recentActivity(50)) {
        if (record.severity == "error") {
            const std::string line = Ui::localTime(record.occurredUtc) + '\t' + record.message;
            recentFailures_->add(line.c_str());
            ++failureCount;
        }
    }
    if (failureCount == 0) {
        recentFailures_->add("No recent failures.");
        clearFailuresButton_->deactivate();
    } else {
        clearFailuresButton_->activate();
    }
    redraw();
}

void OverviewPanel::clearFailuresCallback(Fl_Widget*, void* context) {
    static_cast<OverviewPanel*>(context)->clearFailures();
}

void OverviewPanel::clearFailures() {
    if (fl_choice("Clear recorded errors? This does not fix pending backups or delete backup files.", "Cancel", "Clear history", nullptr) != 1) { return; }
    try {
        stateStore_.clearFailures();
        refresh();
    } catch (const std::exception& error) {
        fl_alert("%s", error.what());
    }
}
