#include "destinations_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <iomanip>
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
#include "ui_theme.hpp"

namespace {

constexpr int kDestinationColumnWidths[] = {86, 170, 255, 120, 0};

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
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return label;
}

}  // namespace

DestinationsPanel::DestinationsPanel(int x, int y, int width, int height, BackupConfig& config,
                                     const ConfigStore& configStore, std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 20, y + 14, 240, 32, "Destinations", 22, UiTheme::kText, FL_HELVETICA_BOLD);
    addLabel(x + 20, y + 50, width - 40, 24, "Connected USB drive", 11, UiTheme::kMutedText);

    connectedDriveChoice_ = new Fl_Choice(x + 20, y + 76, 290, 34);
    connectedDriveChoice_->box(FL_BORDER_BOX);
    connectedDriveChoice_->color(UiTheme::kCard);
    connectedDriveChoice_->textcolor(UiTheme::kText);
    connectedDriveChoice_->selection_color(UiTheme::kSelection);

    auto* refreshButton = new Fl_Button(x + 320, y + 76, 92, 34, "Refresh");
    styleButton(*refreshButton);
    refreshButton->callback(refreshCallback, this);

    addUsbButton_ = new Fl_Button(x + 422, y + 76, 116, 34, "Add USB");
    styleButton(*addUsbButton_, true);
    addUsbButton_->callback(addUsbCallback, this);

    auto* addFolderButton = new Fl_Button(x + 548, y + 76, 118, 34, "Add folder");
    styleButton(*addFolderButton);
    addFolderButton->callback(addFolderCallback, this);

    removeButton_ = new Fl_Button(x + 20, y + 122, 150, 34, "Remove selected");
    styleButton(*removeButton_);
    removeButton_->callback(removeCallback, this);
    removeButton_->deactivate();

    addLabel(x + 24, y + 168, 78, 24, "Status", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 110, y + 168, 160, 24, "Name", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 280, y + 168, 245, 24, "Location", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 535, y + 168, 120, 24, "Free space", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);

    destinationBrowser_ = new Fl_Multi_Browser(x + 20, y + 193, width - 40, height - 243);
    destinationBrowser_->box(FL_BORDER_BOX);
    destinationBrowser_->color(UiTheme::kCard);
    destinationBrowser_->textcolor(UiTheme::kText);
    destinationBrowser_->selection_color(UiTheme::kSelection);
    destinationBrowser_->textsize(12);
    destinationBrowser_->column_widths(kDestinationColumnWidths);
    destinationBrowser_->column_char('\t');
    destinationBrowser_->format_char(0);
    destinationBrowser_->callback(selectionCallback, this);
    destinationBrowser_->when(FL_WHEN_CHANGED);

    resultSummary_ = addLabel(x + 20, y + height - 42, width - 40, 24, "", 11, UiTheme::kMutedText);

    end();
    resizable(destinationBrowser_);
    refresh();
}

void DestinationsPanel::refresh() {
    try {
        refreshConnectedDrives();
        destinationBrowser_->clear();

        for (const Destination& destination : config_.destinations) {
            std::string status = "Available";
            std::string location = pathToUtf8(destination.root);
            std::string freeSpace = "--";
            if (destination.kind == DestinationKind::removable) {
                const auto connected = std::ranges::find(connectedVolumes_, destination.volumeSerial,
                                                         &ConnectedVolume::serial);
                if (connected == connectedVolumes_.end()) {
                    status = "Unavailable";
                } else {
                    location = pathToUtf8(connected->root);
                    freeSpace = formatGibibytes(connected->freeBytes);
                }
            } else if (!std::filesystem::exists(destination.root)) {
                status = "Unavailable";
            }

            const std::string line = status + '\t' + destination.name + '\t' + location + '\t' + freeSpace;
            destinationBrowser_->add(line.c_str());
        }

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
    std::unordered_set<std::string> selectedIds;
    for (int line = 1; line <= destinationBrowser_->size(); ++line) {
        if (destinationBrowser_->selected(line) != 0) {
            selectedIds.insert(config_.destinations.at(static_cast<std::size_t>(line - 1)).id);
        }
    }
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
    connectedVolumes_ = findConnectedRemovableVolumes();
    connectedDriveChoice_->clear();
    for (const ConnectedVolume& volume : connectedVolumes_) {
        const std::string name = volume.label.empty() ? "USB Drive" : volume.label;
        const std::string entry = pathToUtf8(volume.root) + "  " + name + "  (" +
                                  formatGibibytes(volume.freeBytes) + " free)";
        connectedDriveChoice_->add(entry.c_str());
    }

    if (connectedVolumes_.empty()) {
        connectedDriveChoice_->add("No ready USB drives found");
        connectedDriveChoice_->value(0);
        addUsbButton_->deactivate();
    } else {
        connectedDriveChoice_->value(0);
        addUsbButton_->activate();
    }
}

void DestinationsPanel::refreshSelectionState() {
    bool hasSelection = false;
    for (int line = 1; line <= destinationBrowser_->size(); ++line) {
        if (destinationBrowser_->selected(line) != 0) {
            hasSelection = true;
            break;
        }
    }
    if (hasSelection) {
        removeButton_->activate();
    } else {
        removeButton_->deactivate();
    }
}

void DestinationsPanel::reportError(const std::exception& error) const {
    fl_alert("%s", error.what());
}
