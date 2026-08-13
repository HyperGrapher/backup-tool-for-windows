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
                 Fl_Font font = FL_HELVETICA) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return label;
}

Fl_Box* addStatusCard(int x, int y, int width, int height, const char* title) {
    auto* card = new Fl_Box(x, y, width, height);
    card->box(FL_BORDER_BOX);
    card->color(UiTheme::kCard);
    addLabel(x + 14, y + 10, width - 28, 20, title, 10, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    return addLabel(x + 14, y + 34, width - 28, height - 42, "", 16, UiTheme::kText, FL_HELVETICA_BOLD);
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

    addLabel(x + 20, y + 14, 240, 32, "Overview", 22, UiTheme::kText, FL_HELVETICA_BOLD);
    const int smallCardWidth = (width - 80) / 3;
    destinationHealth_ = addStatusCard(x + 20, y + 60, smallCardWidth, 88, "DESTINATIONS");
    pendingRoutes_ = addStatusCard(x + 30 + smallCardWidth, y + 60, smallCardWidth, 88, "PENDING MIRRORS");
    watcherStatus_ = addStatusCard(x + 40 + smallCardWidth * 2, y + 60, smallCardWidth, 88, "AUTOMATIC WATCHING");
    const int wideCardWidth = (width - 70) / 2;
    lastMirror_ = addStatusCard(x + 20, y + 158, wideCardWidth, 88, "LAST SUCCESSFUL MIRROR");
    lastSnapshot_ = addStatusCard(x + 30 + wideCardWidth, y + 158, wideCardWidth, 88, "LAST SNAPSHOT");

    addLabel(x + 20, y + 268, width - 40, 24, "Recent failures", 13, UiTheme::kText, FL_HELVETICA_BOLD);
    recentFailures_ = new Fl_Browser(x + 20, y + 298, width - 40, height - 348);
    recentFailures_->box(FL_BORDER_BOX);
    recentFailures_->color(UiTheme::kCard);
    recentFailures_->textcolor(UiTheme::kText);
    recentFailures_->selection_color(UiTheme::kSelection);
    recentFailures_->textsize(11);

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
    const std::string pendingText = std::to_string(pendingCount);
    pendingRoutes_->copy_label(pendingText.c_str());

    std::optional<std::string> latestSuccess;
    for (const RouteRuntimeState& state : states) {
        if (state.lastSuccessUtc.has_value() && (!latestSuccess.has_value() || *state.lastSuccessUtc > *latestSuccess)) {
            latestSuccess = state.lastSuccessUtc;
        }
    }
    lastMirror_->copy_label(latestSuccess.has_value() ? latestSuccess->c_str() : "Not run yet");
    const std::optional<std::string> latestSnapshot = stateStore_.latestSnapshotUtc();
    lastSnapshot_->copy_label(latestSnapshot.has_value() ? latestSnapshot->c_str() : "Not created yet");

    const std::size_t watchedSourceCount = config_.manualSources.size() + projects.sources.size();
    const std::string watcherText = std::to_string(watchedSourceCount) + " Sources configured";
    watcherStatus_->copy_label(watcherText.c_str());

    recentFailures_->clear();
    for (const ActivityRecord& record : stateStore_.recentActivity(50)) {
        if (record.severity == "error") {
            const std::string line = record.occurredUtc + "   " + record.message;
            recentFailures_->add(line.c_str());
        }
    }
    if (recentFailures_->size() == 0) {
        recentFailures_->add("No recent failures.");
    }
    redraw();
}
