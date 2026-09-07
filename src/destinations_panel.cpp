#include "destinations_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
#include <optional>
#include <sstream>
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
#include "state_store.hpp"
#include "ui_theme.hpp"
#include "ui_controls.hpp"
#include "ui_helpers.hpp"
#include "ui_table.hpp"

namespace {


[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    std::string text;
    text.reserve(bytes.size());
    for (const char8_t byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

[[nodiscard]] std::wstring normalizedPathKey(const std::filesystem::path& path) {
    std::wstring key = std::filesystem::absolute(path).lexically_normal().native();
    std::replace(key.begin(), key.end(), L'/', L'\\');
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
}

[[nodiscard]] std::string formatGibibytes(std::uint64_t bytes) {
    constexpr double kBytesPerGibibyte = 1024.0 * 1024.0 * 1024.0;
    std::ostringstream result;
    result << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / kBytesPerGibibyte << " GiB";
    return result.str();
}

using Ui::styleButton;

}  // namespace

DestinationsPanel::DestinationsPanel(int x, int y, int width, int height, BackupConfig& config,
                                     const ConfigStore& configStore, const StateStore& stateStore,
                                     std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      stateStore_(stateStore), configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    Ui::label(x + 24, y + 16, width - 48, 36, "Destinations", 24, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 56, width - 48, 40,
              "Where copies go. Each source and project can choose one or more destinations.", 13, UiTheme::kSecondaryText);
    connectedDriveChoice_ = new Fl_Choice(x + 24, y + 108, width - 276, 36);
    connectedDriveChoice_->box(FL_BORDER_BOX);
    connectedDriveChoice_->color(UiTheme::kSurface);
    connectedDriveChoice_->textcolor(UiTheme::kText);
    connectedDriveChoice_->selection_color(UiTheme::kSelection);
    connectedDriveChoice_->textfont(UiTheme::kUiFont);
    connectedDriveChoice_->textsize(13);
    connectedDriveChoice_->callback(selectionCallback, this);
    auto* refreshButton = new ActionButton(x + width - 240, y + 108, 96, 36, "Refresh");
    refreshButton->callback(refreshCallback, this);
    addUsbButton_ = new ActionButton(x + width - 136, y + 108, 112, 36, "Add USB");
    styleButton(*addUsbButton_, true);
    addUsbButton_->callback(addUsbCallback, this);
    auto* folders = new ActionButton(x + 24, y + 156, 244, 36, "Add folder / network location");
    folders->callback(addFolderCallback, this);
    removeButton_ = new ActionButton(x + 280, y + 156, 132, 36, "Remove…");
    styleButton(*removeButton_, false, true);
    removeButton_->callback(removeCallback, this);
    auto* details = new ActionButton(x + 424, y + 156, 112, 36, "Details");
    details->callback([](Fl_Widget*, void* context) {
        auto* panel = static_cast<DestinationsPanel*>(context);
        const auto text = panel->destinationBrowser_->selectedDetails();
        if (!text.empty()) { Ui::showDetails(text, "Destination details"); }
        else { panel->resultSummary_->copy_label("Select a destination to see and copy its full path."); }
    }, this);
    destinationBrowser_ = new DataTable(x + 24, y + 208, width - 48, height - 264,
                                        {"Destination / location", "Availability", "Pending", "Free space"}, {43, 24, 13, 20});
    destinationBrowser_->callback(selectionCallback, this);
    destinationBrowser_->emptyMessage("Add a USB drive or folder to receive copies. Disconnected drives keep their pending work.");
    resultSummary_ = Ui::label(x + 24, y + height - 48, width - 48, 40, "", 12, UiTheme::kSecondaryText);

    end();
    resizable(destinationBrowser_);
    refresh();
}

void DestinationsPanel::refresh() {
    try {
        refreshConnectedDrives();
        std::vector<TableRow> rows;
        const std::vector<RouteRuntimeState> states = stateStore_.routeStates();

        for (const Destination& destination : config_.destinations) {
            bool isAvailable = true;
            std::string location = pathToUtf8(destination.root);
            std::string freeSpace = "Not measured";
            if (destination.kind == DestinationKind::removable) {
                const auto connected = std::ranges::find(connectedVolumes_, destination.volumeSerial,
                                                         &ConnectedVolume::serial);
                if (connected == connectedVolumes_.end()) {
                    isAvailable = false;
                } else {
                    location = pathToUtf8(connected->root);
                    freeSpace = formatGibibytes(connected->freeBytes);
                }
            } else if (!std::filesystem::exists(destination.root)) {
                isAvailable = false;
            }

            const auto pendingCount = std::ranges::count_if(states, [&](const RouteRuntimeState& state) {
                return state.destinationId == destination.id && state.isDirty;
            });
            const bool hasError = std::ranges::any_of(states, [&](const RouteRuntimeState& state) {
                return state.destinationId == destination.id && state.status == RouteStatus::error;
            });
            // Availability answers whether the location can be reached. A prior route
            // failure is shown in details and must not make a connected drive look offline.
            const std::string availability = isAvailable
                                                  ? "Connected"
                                                  : (destination.kind == DestinationKind::removable ? "Disconnected"
                                                                                                  : "Unavailable");
            const std::string pendingText = hasError
                                                ? std::to_string(pendingCount) + " · needs attention"
                                                : std::to_string(pendingCount);
            rows.push_back({destination.id, {destination.name + "\n" + location, availability,
                                             pendingText, freeSpace},
                           destination.name + "\n" + location + "\n" + availability + "\n" +
                           pendingText + " pending backups\nFree space: " + freeSpace +
                           (hasError ? "\nAt least one route needs attention; see Activity for details." : "")});
        }
        destinationBrowser_->setRows(std::move(rows));
        const std::string summary = std::to_string(config_.destinations.size()) + " configured Destinations";
        resultSummary_->copy_label(summary.c_str());
        refreshSelectionState();
        redraw();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void DestinationsPanel::refreshCallback(Fl_Widget*, void* context) {
    static_cast<DestinationsPanel*>(context)->refresh();
}

void DestinationsPanel::addUsbCallback(Fl_Widget*, void* context) {
    static_cast<DestinationsPanel*>(context)->addSelectedUsb();
}

void DestinationsPanel::addFolderCallback(Fl_Widget*, void* context) {
    static_cast<DestinationsPanel*>(context)->addFolders();
}

void DestinationsPanel::removeCallback(Fl_Widget*, void* context) {
    static_cast<DestinationsPanel*>(context)->removeSelectedDestinations();
}

void DestinationsPanel::selectionCallback(Fl_Widget*, void* context) {
    static_cast<DestinationsPanel*>(context)->refreshSelectionState();
}

void DestinationsPanel::addSelectedUsb() {
    try {
        const int selectedIndex = connectedDriveChoice_->value();
        if (selectedIndex < 0 || static_cast<std::size_t>(selectedIndex) >= connectedVolumes_.size()) {
            return;
        }

        const ConnectedVolume& volume = connectedVolumes_[static_cast<std::size_t>(selectedIndex)];
        const bool alreadyConfigured = std::ranges::any_of(config_.destinations, [&](const Destination& destination) {
            return destination.kind == DestinationKind::removable && destination.volumeSerial == volume.serial;
        });
        if (alreadyConfigured) {
            resultSummary_->copy_label("That USB drive is already configured.");
            return;
        }

        BackupConfig updatedConfig = config_;
        const std::string name = volume.label.empty() ? "USB Drive" : volume.label;
        updatedConfig.destinations.push_back(Destination{
            generateStableId("destination"),
            name,
            DestinationKind::removable,
            volume.root,
            volume.serial,
            volume.label,
        });
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void DestinationsPanel::addFolders() {
    try {
        const std::vector<std::filesystem::path> paths = selectFolders(fl_xid(window()));
        if (paths.empty()) {
            return;
        }

        BackupConfig updatedConfig = config_;
        std::unordered_set<std::wstring> knownPaths;
        for (const Destination& destination : updatedConfig.destinations) {
            if (destination.kind == DestinationKind::path) {
                knownPaths.insert(normalizedPathKey(destination.root));
            }
        }

        for (const std::filesystem::path& path : paths) {
            const std::filesystem::path normalizedPath = std::filesystem::absolute(path).lexically_normal();
            if (!knownPaths.insert(normalizedPathKey(normalizedPath)).second) {
                continue;
            }
            std::string name = pathToUtf8(normalizedPath.filename());
            if (name.empty()) {
                name = pathToUtf8(normalizedPath);
            }
            updatedConfig.destinations.push_back(Destination{
                generateStableId("destination"), name, DestinationKind::path, normalizedPath, 0, {}});
        }

        if (updatedConfig.destinations.size() == config_.destinations.size()) {
            resultSummary_->copy_label("Those folders are already configured.");
            return;
        }
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void DestinationsPanel::removeSelectedDestinations() {
    const auto selected = destinationBrowser_->selectedKeys();
    const std::unordered_set<std::string> selectedIds(selected.begin(), selected.end());
    if (selectedIds.empty()) {
        return;
    }

    const int choice = fl_choice(
        "Remove %zu selected Destinations? Existing backup files will not be deleted.", "Cancel", "Remove", nullptr,
        selectedIds.size());
    if (choice != 1) {
        return;
    }

    try {
        BackupConfig updatedConfig = config_;
        std::erase_if(updatedConfig.destinations, [&](const Destination& destination) {
            return selectedIds.contains(destination.id);
        });
        std::erase_if(updatedConfig.routes, [&](const BackupRoute& route) {
            return selectedIds.contains(route.destinationId);
        });
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void DestinationsPanel::refreshConnectedDrives() {
    std::optional<std::uint32_t> selectedSerial;
    const int selectedIndex = connectedDriveChoice_->value();
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(connectedVolumes_.size())) {
        selectedSerial = connectedVolumes_[selectedIndex].serial;
    }
    connectedVolumes_ = findConnectedRemovableVolumes();
    connectedDriveChoice_->clear();
    for (const ConnectedVolume& volume : connectedVolumes_) {
        const std::string name = volume.label.empty() ? "USB Drive" : volume.label;
        const bool isConfigured = std::ranges::any_of(config_.destinations, [&](const Destination& destination) { return destination.kind == DestinationKind::removable && destination.volumeSerial == volume.serial; });
        const std::string entry = (isConfigured ? "[Added] " : "") + pathToUtf8(volume.root) + "  " + name + "  (" +
                                  formatGibibytes(volume.freeBytes) + " free)";
        connectedDriveChoice_->add(entry.c_str());
    }

    if (connectedVolumes_.empty()) {
        connectedDriveChoice_->add("No ready USB drives found");
        connectedDriveChoice_->value(0);
        addUsbButton_->deactivate();
    } else {
        int restoreIndex = 0;
        if (selectedSerial) {
            for (int index = 0; index < static_cast<int>(connectedVolumes_.size()); ++index) {
                if (connectedVolumes_[index].serial == *selectedSerial) { restoreIndex = index; }
            }
        }
        connectedDriveChoice_->value(restoreIndex);
        refreshSelectionState();
    }
}

void DestinationsPanel::refreshSelectionState() {
    const auto count = destinationBrowser_->selectedKeys().size();
    const std::string label = count ? "Remove " + std::to_string(count) + "…" : "Remove…";
    removeButton_->copy_label(label.c_str());
    if (count) { removeButton_->activate(); }
    else { removeButton_->deactivate(); }
    const int selected = connectedDriveChoice_->value();
    bool canAdd = selected >= 0 && selected < static_cast<int>(connectedVolumes_.size());
    if (canAdd) {
        canAdd = !std::ranges::any_of(config_.destinations, [&](const Destination& destination) {
            return destination.kind == DestinationKind::removable && destination.volumeSerial == connectedVolumes_[selected].serial;
        });
    }
    if (canAdd) { addUsbButton_->activate(); }
    else { addUsbButton_->deactivate(); }
}

void DestinationsPanel::reportError(const std::exception& error) const {
    resultSummary_->copy_label(error.what());
    resultSummary_->labelcolor(UiTheme::kError);
}
