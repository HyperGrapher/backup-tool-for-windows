#include "overview_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include <FL/Fl_Box.H>
#include <FL/Fl_Browser.H>

#include "connected_volume.hpp"
#include "projects_scanner.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"

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
                             const StateStore& stateStore)
    : Fl_Group(x, y, width, height), config_(config), stateStore_(stateStore) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 16, y + 10, width - 32, 28, "Backup status", 18, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 16, y + 36, width - 32, 22,
             "What is protected, what is waiting, and what needs attention.", 11, UiTheme::kSecondaryText);

    const int rowX = x + 16;
    const int rowWidth = width - 32;
    destinationHealth_ = addStatusRow(rowX, y + 68, rowWidth, "Destinations");
    pendingRoutes_ = addStatusRow(rowX, y + 102, rowWidth, "Pending changes");
    watcherStatus_ = addStatusRow(rowX, y + 136, rowWidth, "Watching");
    lastMirror_ = addStatusRow(rowX, y + 170, rowWidth, "Last successful Mirror");
    lastSnapshot_ = addStatusRow(rowX, y + 204, rowWidth, "Last Snapshot");

    addLabel(x + 16, y + 252, width - 32, 24, "Failures requiring attention", 13, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 20, y + 278, 150, 22, "Time", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    addLabel(x + 170, y + 278, width - 190, 22, "Details", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);
    recentFailures_ = new Fl_Browser(x + 16, y + 300, width - 32, height - 316);
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
    try {
        connectedVolumes = findConnectedRemovableVolumes();
    } catch (const std::exception&) {
    }

    const auto availableCount = std::ranges::count_if(config_.destinations, [&](const Destination& destination) {
        return isDestinationAvailable(destination, connectedVolumes);
    });
    const std::string destinationText = std::to_string(availableCount) + " of " +
                                        std::to_string(config_.destinations.size()) + " available";
    destinationHealth_->copy_label(destinationText.c_str());
    destinationHealth_->labelcolor(availableCount == static_cast<std::ptrdiff_t>(config_.destinations.size())
                                       ? UiTheme::kSafe
                                       : UiTheme::kError);

    const std::vector<RouteRuntimeState> states = stateStore_.routeStates();
    const ConfiguredProjectsDiscovery projects = discoverConfiguredProjects(config_.projectsRoots);
    const auto pendingCount = std::ranges::count_if(states, [&](const RouteRuntimeState& state) {
        return state.isDirty && std::ranges::any_of(config_.routes, [&](const BackupRoute& route) {
            if (route.destinationId != state.destinationId || !route.isMirrorEnabled) {
                return false;
            }
            if (route.sourceId == state.sourceId) {
                return std::ranges::any_of(config_.manualSources, [&](const ManualSource& source) {
                    return source.id == state.sourceId;
                });
            }
            return std::ranges::any_of(projects.sources, [&](const ConfiguredProjectsSource& source) {
                return source.rootId == route.sourceId && source.source.id == state.sourceId;
            });
        });
    });
    const std::string pendingText = pendingCount == 0 ? "●—● Current" :
                                                        "●  ○ " + std::to_string(pendingCount) + " waiting";
    pendingRoutes_->copy_label(pendingText.c_str());
    pendingRoutes_->labelcolor(pendingCount == 0 ? UiTheme::kSafe : UiTheme::kPending);

    std::optional<std::string> latestSuccess;
    for (const RouteRuntimeState& state : states) {
        if (state.lastSuccessUtc.has_value() && (!latestSuccess.has_value() || *state.lastSuccessUtc > *latestSuccess)) {
            latestSuccess = state.lastSuccessUtc;
        }
    }
    lastMirror_->copy_label(latestSuccess.has_value() ? latestSuccess->c_str() : "Not run yet");
    lastMirror_->labelfont(UiTheme::kMonoFont);
    const std::optional<std::string> latestSnapshot = stateStore_.latestSnapshotUtc();
    lastSnapshot_->copy_label(latestSnapshot.has_value() ? latestSnapshot->c_str() : "Not created yet");
    lastSnapshot_->labelfont(UiTheme::kMonoFont);

    const std::size_t watchedSourceCount = config_.manualSources.size() + projects.sources.size();
    const std::string watcherText = std::to_string(watchedSourceCount) + " Sources configured";
    watcherStatus_->copy_label(watcherText.c_str());
    watcherStatus_->labelcolor(UiTheme::kSafe);

    recentFailures_->clear();
    for (const ActivityRecord& record : stateStore_.recentActivity(50)) {
        if (record.severity == "error") {
            const std::string line = record.occurredUtc + '\t' + record.message;
            recentFailures_->add(line.c_str());
        }
    }
    if (recentFailures_->size() == 0) {
        recentFailures_->add("No recent failures.");
    }
    redraw();
}
