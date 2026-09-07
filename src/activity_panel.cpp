#include "activity_panel.hpp"

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>
#include <FL/fl_ask.H>

#include "backup_config.hpp"
#include "clear_history_button.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"
#include "ui_helpers.hpp"
#include "ui_table.hpp"
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>

namespace {

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] std::string sourceName(const BackupConfig& config,
                                     const ConfiguredProjectsDiscovery& projects,
                                     const std::optional<std::string>& sourceId) {
    if (!sourceId.has_value()) {
        return "—";
    }
    for (const ManualSource& source : config.manualSources) {
        if (source.id == *sourceId) {
            const std::filesystem::path name = source.path.filename();
            return pathToUtf8(name.empty() ? source.path : name);
        }
    }
    for (const ConfiguredProjectsSource& source : projects.sources) {
        if (source.source.id == *sourceId) {
            return pathToUtf8(source.source.path.filename());
        }
    }
    return "Previous Source";
}

[[nodiscard]] std::string destinationName(const BackupConfig& config,
                                          const std::optional<std::string>& destinationId) {
    if (!destinationId.has_value()) {
        return "—";
    }
    for (const Destination& destination : config.destinations) {
        if (destination.id == *destinationId) {
            return destination.name;
        }
    }
    return "Previous Destination";
}

[[nodiscard]] std::string resultName(const ActivityRecord& record) {
    if (record.severity == "error") {
        return "Action needed";
    }
    if (record.severity == "warning") {
        return "Waiting";
    }
    return "Info";
}

}  // namespace

ActivityPanel::ActivityPanel(int x, int y, int width, int height, const BackupConfig& config,
                             StateStore& stateStore)
    : Fl_Group(x, y, width, height), config_(config), stateStore_(stateStore) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    Ui::label(x + 24, y + 16, width - 48, 36, "Activity", 24, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 56, width - 48, 40,
              "Recent backup work, newest first. Select an event to read and copy the full details.", 13, UiTheme::kSecondaryText);
    filterChoice_ = new Fl_Choice(x + 24, y + 108, 164, 36);
    filterChoice_->add("All events");
    filterChoice_->add("Errors");
    filterChoice_->add("Waiting");
    filterChoice_->value(0);
    filterChoice_->color(UiTheme::kSurface);
    filterChoice_->textcolor(UiTheme::kText);
    filterChoice_->textfont(UiTheme::kUiFont);
    filterChoice_->textsize(13);
    filterChoice_->callback([](Fl_Widget*, void* context) { static_cast<ActivityPanel*>(context)->refresh(); }, this);
    Ui::label(x + 200, y + 108, 56, 36, "Search", 12, UiTheme::kSecondaryText);
    searchInput_ = new Fl_Input(x + 260, y + 108, width - 284, 36);
    searchInput_->color(UiTheme::kSurface);
    searchInput_->textcolor(UiTheme::kText);
    searchInput_->cursor_color(UiTheme::kText);
    searchInput_->textfont(UiTheme::kUiFont);
    searchInput_->textsize(14);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback([](Fl_Widget*, void* context) { static_cast<ActivityPanel*>(context)->refresh(); }, this);
    auto* details = new ActionButton(x + 24, y + 156, 140, 36, "Event details");
    details->callback([](Fl_Widget*, void* context) {
        auto* panel = static_cast<ActivityPanel*>(context);
        const auto text = panel->activityBrowser_->selectedDetails();
        if (!text.empty()) { Ui::showDetails(text, "Activity details"); }
        else { panel->resultSummary_->copy_label("Select one or more events to see their details."); }
    }, this);
    clearActivityButton_ = new ActionButton(x + 176, y + 156, 144, 36, "Clear history…");
    Ui::styleButton(*clearActivityButton_, false, true);
    clearActivityButton_->callback(clearActivityCallback, this);
    activityBrowser_ = new DataTable(x + 24, y + 208, width - 48, height - 264,
                                     {"Local time", "Result", "Source → destination"}, {29, 23, 48});
    resultSummary_ = Ui::label(x + 24, y + height - 48, width - 48, 40, "", 12, UiTheme::kSecondaryText);
    end();
    resizable(activityBrowser_);
    refresh();
}

void ActivityPanel::refresh() {
    try {
        const auto records = stateStore_.recentActivity(250);
        const auto projects = discoverConfiguredProjects(config_.projectsRoots);
        std::vector<TableRow> rows;
        const std::string query = searchInput_->value();
        for (const auto& record : records) {
            if (filterChoice_->value() == 1 && record.severity != "error") { continue; }
            if (filterChoice_->value() == 2 && record.severity != "warning") { continue; }
            const std::string source = sourceName(config_, projects, record.sourceId);
            const std::string destination = destinationName(config_, record.destinationId);
            const std::string details = Ui::localTime(record.occurredUtc) + " (local)\nUTC: " + record.occurredUtc +
                                        "\n" + source + " → " + destination + "\n\n" + record.message;
            if (!query.empty() && details.find(query) == std::string::npos) { continue; }
            rows.push_back({std::to_string(record.id), {Ui::localTime(record.occurredUtc), resultName(record),
                                                       source + " → " + destination}, details});
        }
        const std::string summary = std::to_string(rows.size()) + " shown · Filters search the latest " +
                                    std::to_string(records.size()) + " events (up to 250)";
        activityBrowser_->setRows(std::move(rows));
        activityBrowser_->emptyMessage(records.empty() ? "No activity yet. Backup results will appear here."
                                                       : "No events match these filters. Choose All events and clear Search.");
        resultSummary_->copy_label(summary.c_str());
        if (records.empty()) { clearActivityButton_->deactivate(); }
        else { clearActivityButton_->activate(); }
        redraw();
    } catch (const std::exception& error) {
        resultSummary_->copy_label(error.what());
        resultSummary_->labelcolor(UiTheme::kError);
    }
}

void ActivityPanel::clearActivityCallback(Fl_Widget*, void* context) {
    static_cast<ActivityPanel*>(context)->clearActivity();
}

void ActivityPanel::clearActivity() {
    if (fl_choice("Clear recorded activity? Backup files and pending work stay unchanged.", "Cancel", "Clear history", nullptr) != 1) { return; }
    try {
        stateStore_.clearActivity();
        refresh();
    } catch (const std::exception& error) {
        fl_alert("%s", error.what());
    }
}
