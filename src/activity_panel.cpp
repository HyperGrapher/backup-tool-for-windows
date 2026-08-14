#include "activity_panel.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>

#include "backup_config.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"

namespace {

constexpr int kActivityColumnWidths[] = {155, 90, 175, 145, 0};

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, int size, Fl_Color color,
                 Fl_Font font = UiTheme::kUiFont) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
    return label;
}

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
    return "Completed";
}

}  // namespace

ActivityPanel::ActivityPanel(int x, int y, int width, int height, const BackupConfig& config,
                             const StateStore& stateStore)
    : Fl_Group(x, y, width, height), config_(config), stateStore_(stateStore) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 16, y + 10, width - 32, 28, "Activity", 18, UiTheme::kText, UiTheme::kUiFontSemibold);
    addLabel(x + 16, y + 36, width - 32, 20,
             "Mirror, Snapshot, and failure history. Newest events appear first.", 11,
             UiTheme::kSecondaryText);

    addLabel(x + 20, y + 66, 151, 22, "Time", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 175, y + 66, 86, 22, "Result", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 265, y + 66, 171, 22, "Source", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 440, y + 66, 141, 22, "Destination", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 585, y + 66, width - 601, 22, "Details", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);

    activityBrowser_ = new Fl_Browser(x + 16, y + 88, width - 32, height - 120);
    activityBrowser_->box(FL_BORDER_BOX);
    activityBrowser_->color(UiTheme::kSurface);
    activityBrowser_->textcolor(UiTheme::kText);
    activityBrowser_->selection_color(UiTheme::kSelection);
    activityBrowser_->textfont(UiTheme::kUiFont);
    activityBrowser_->textsize(12);
    activityBrowser_->column_widths(kActivityColumnWidths);
    activityBrowser_->column_char('\t');
    activityBrowser_->format_char(0);

    resultSummary_ = addLabel(x + 16, y + height - 28, width - 32, 20, "", 11, UiTheme::kSecondaryText);

    end();
    resizable(activityBrowser_);
    refresh();
}

void ActivityPanel::refresh() {
    activityBrowser_->clear();
    const std::vector<ActivityRecord> records = stateStore_.recentActivity(250);
    const ConfiguredProjectsDiscovery projects = discoverConfiguredProjects(config_.projectsRoots);
    for (const ActivityRecord& record : records) {
        const std::string line = record.occurredUtc + '\t' + resultName(record) + '\t' +
                                 sourceName(config_, projects, record.sourceId) + '\t' +
                                 destinationName(config_, record.destinationId) + '\t' + record.message;
        activityBrowser_->add(line.c_str());
    }
    if (records.empty()) {
        activityBrowser_->add("—\tNo activity yet\t—\t—\tBackups will appear here after they run.");
    }
    const std::string summary = std::to_string(records.size()) + " recent events";
    resultSummary_->copy_label(summary.c_str());
    redraw();
}
