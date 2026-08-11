#include "sources_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>

#include "config_store.hpp"
#include "native_file_dialog.hpp"
#include "ui_theme.hpp"

namespace {

constexpr int kSourceColumnWidths[] = {86, 430, 105, 0};

[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string bytes = path.u8string();
    std::string text;
    text.reserve(bytes.size());
    for (const char8_t byte : bytes) {
        text.push_back(static_cast<char>(byte));
    }
    return text;
}

[[nodiscard]] std::string lowercaseAscii(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
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

SourcesPanel::SourcesPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                           std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 20, y + 14, 200, 32, "Manual Sources", 22, UiTheme::kText, FL_HELVETICA_BOLD);
    addLabel(x + width - 300, y + 16, 52, 28, "Search", 11, UiTheme::kMutedText);
    searchInput_ = new Fl_Input(x + width - 245, y + 16, 225, 28);
    searchInput_->box(FL_BORDER_BOX);
    searchInput_->color(UiTheme::kCard);
    searchInput_->textcolor(UiTheme::kText);
    searchInput_->cursor_color(UiTheme::kText);
    searchInput_->selection_color(UiTheme::kSelection);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback(searchCallback, this);

    auto* addFilesButton = new Fl_Button(x + 20, y + 60, 120, 34, "Add files");
    styleButton(*addFilesButton, true);
    addFilesButton->callback(addFilesCallback, this);

    auto* addFoldersButton = new Fl_Button(x + 150, y + 60, 120, 34, "Add folders");
    styleButton(*addFoldersButton);
    addFoldersButton->callback(addFoldersCallback, this);

    removeButton_ = new Fl_Button(x + 280, y + 60, 145, 34, "Remove selected");
    styleButton(*removeButton_);
    removeButton_->callback(removeCallback, this);
    removeButton_->deactivate();

    addLabel(x + 20, y + 105, 78, 34, "Send to", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    destinationChoice_ = new Fl_Choice(x + 88, y + 105, 210, 34);
    destinationChoice_->box(FL_BORDER_BOX);
    destinationChoice_->color(UiTheme::kCard);
    destinationChoice_->textcolor(UiTheme::kText);
    destinationChoice_->selection_color(UiTheme::kSelection);
    destinationChoice_->callback(destinationChoiceCallback, this);

    connectButton_ = new Fl_Button(x + 308, y + 105, 150, 34, "Connect selected");
    styleButton(*connectButton_, true);
    connectButton_->callback(connectCallback, this);
    connectButton_->deactivate();

    disconnectButton_ = new Fl_Button(x + 468, y + 105, 170, 34, "Disconnect selected");
    styleButton(*disconnectButton_);
    disconnectButton_->callback(disconnectCallback, this);
    disconnectButton_->deactivate();

    addLabel(x + 24, y + 150, 78, 24, "Type", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 110, y + 150, 420, 24, "Path", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);
    addLabel(x + 540, y + 150, 110, 24, "Destinations", 11, UiTheme::kMutedText, FL_HELVETICA_BOLD);

    sourceBrowser_ = new Fl_Multi_Browser(x + 20, y + 175, width - 40, height - 225);
    sourceBrowser_->box(FL_BORDER_BOX);
    sourceBrowser_->color(UiTheme::kCard);
    sourceBrowser_->textcolor(UiTheme::kText);
    sourceBrowser_->selection_color(UiTheme::kSelection);
    sourceBrowser_->textsize(12);
    sourceBrowser_->column_widths(kSourceColumnWidths);
    sourceBrowser_->column_char('\t');
    sourceBrowser_->format_char(0);
    sourceBrowser_->callback(selectionCallback, this);
    sourceBrowser_->when(FL_WHEN_CHANGED);

    resultSummary_ = addLabel(x + 20, y + height - 42, width - 40, 24, "", 11, UiTheme::kMutedText);

    end();
    resizable(sourceBrowser_);
    refresh();
}

void SourcesPanel::refresh() {
    refreshDestinationChoices();
    const std::string filter = lowercaseAscii(searchInput_->value());
    visibleSourceIndexes_.clear();
    sourceBrowser_->clear();

    for (std::size_t index = 0; index < config_.manualSources.size(); ++index) {
        const ManualSource& source = config_.manualSources[index];
        const std::string pathText = pathToUtf8(source.path);
        const std::string typeText = source.kind == ManualSourceKind::file ? "File" : "Folder";
        if (!filter.empty() && lowercaseAscii(pathText).find(filter) == std::string::npos &&
            lowercaseAscii(typeText).find(filter) == std::string::npos) {
            continue;
        }

        const auto routeCount = std::count_if(config_.routes.begin(), config_.routes.end(), [&](const BackupRoute& route) {
            return route.sourceId == source.id;
        });
        const std::string line = typeText + '\t' + pathText + '\t' + std::to_string(routeCount);
        sourceBrowser_->add(line.c_str());
        visibleSourceIndexes_.push_back(index);
    }

    const std::string summary = std::to_string(visibleSourceIndexes_.size()) + " shown of " +
                                std::to_string(config_.manualSources.size()) + " Manual Sources";
    resultSummary_->copy_label(summary.c_str());
    refreshSelectionState();
    redraw();
}

void SourcesPanel::addFilesCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->addFiles();
}

void SourcesPanel::addFoldersCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->addFolders();
}

void SourcesPanel::removeCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->removeSelectedSources();
}

void SourcesPanel::connectCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->connectSelectedSources();
}

void SourcesPanel::disconnectCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->disconnectSelectedSources();
}

void SourcesPanel::searchCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refresh();
}

void SourcesPanel::selectionCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refreshSelectionState();
}

void SourcesPanel::destinationChoiceCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refreshSelectionState();
}

void SourcesPanel::addFiles() {
    try {
        addSources(selectFiles(fl_xid(window())), ManualSourceKind::file);
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SourcesPanel::addFolders() {
    try {
        addSources(selectFolders(fl_xid(window())), ManualSourceKind::folder);
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SourcesPanel::addSources(const std::vector<std::filesystem::path>& paths, ManualSourceKind kind) {
    if (paths.empty()) {
        return;
    }

    BackupConfig updatedConfig = config_;
    std::unordered_set<std::wstring> knownPaths;
    for (const ManualSource& source : updatedConfig.manualSources) {
        knownPaths.insert(normalizedPathKey(source.path));
    }

    std::size_t addedCount = 0;
    for (const std::filesystem::path& path : paths) {
        const std::filesystem::path normalizedPath = std::filesystem::absolute(path).lexically_normal();
        if (!knownPaths.insert(normalizedPathKey(normalizedPath)).second) {
            continue;
        }
        updatedConfig.manualSources.push_back(ManualSource{generateStableId("source"), normalizedPath, kind});
        ++addedCount;
    }
    if (addedCount == 0) {
        resultSummary_->copy_label("Those Sources are already in BackItUpTool.");
        return;
    }

    configStore_.save(updatedConfig);
    config_ = std::move(updatedConfig);
    configChangedCallback_();
    refresh();
}

void SourcesPanel::removeSelectedSources() {
    const std::vector<std::string> selectedSourceIds = this->selectedSourceIds();
    const std::unordered_set<std::string> selectedIds(selectedSourceIds.begin(), selectedSourceIds.end());
    if (selectedIds.empty()) {
        return;
    }

    const int choice = fl_choice(
        "Remove %zu selected Sources? Existing backup files will not be deleted.", "Cancel", "Remove", nullptr,
        selectedIds.size());
    if (choice != 1) {
        return;
    }

    try {
        BackupConfig updatedConfig = config_;
        std::erase_if(updatedConfig.manualSources, [&](const ManualSource& source) {
            return selectedIds.contains(source.id);
        });
        std::erase_if(updatedConfig.routes, [&](const BackupRoute& route) {
            return selectedIds.contains(route.sourceId);
        });
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SourcesPanel::connectSelectedSources() {
    const std::vector<std::string> sourceIds = selectedSourceIds();
    const int destinationIndex = destinationChoice_->value();
    if (sourceIds.empty() || destinationIndex < 0 ||
        static_cast<std::size_t>(destinationIndex) >= config_.destinations.size()) {
        return;
    }

    try {
        BackupConfig updatedConfig = config_;
        const std::string& destinationId =
            updatedConfig.destinations.at(static_cast<std::size_t>(destinationIndex)).id;
        std::size_t connectedCount = 0;
        for (const std::string& sourceId : sourceIds) {
            const bool routeExists = std::ranges::any_of(updatedConfig.routes, [&](const BackupRoute& route) {
                return route.sourceId == sourceId && route.destinationId == destinationId;
            });
            if (routeExists) {
                continue;
            }
            updatedConfig.routes.push_back(BackupRoute{sourceId, destinationId, true, true, {}});
            ++connectedCount;
        }

        if (connectedCount == 0) {
            resultSummary_->copy_label("The selected Sources are already connected to that Destination.");
            return;
        }
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
        const std::string result = "Connected " + std::to_string(connectedCount) + " Sources to " +
                                   config_.destinations.at(static_cast<std::size_t>(destinationIndex)).name + '.';
        resultSummary_->copy_label(result.c_str());
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SourcesPanel::disconnectSelectedSources() {
    const std::vector<std::string> sourceIds = selectedSourceIds();
    const int destinationIndex = destinationChoice_->value();
    if (sourceIds.empty() || destinationIndex < 0 ||
        static_cast<std::size_t>(destinationIndex) >= config_.destinations.size()) {
        return;
    }

    const std::unordered_set<std::string> selectedIds(sourceIds.begin(), sourceIds.end());
    try {
        BackupConfig updatedConfig = config_;
        const Destination& destination = updatedConfig.destinations.at(static_cast<std::size_t>(destinationIndex));
        const std::size_t previousRouteCount = updatedConfig.routes.size();
        std::erase_if(updatedConfig.routes, [&](const BackupRoute& route) {
            return route.destinationId == destination.id && selectedIds.contains(route.sourceId);
        });
        const std::size_t disconnectedCount = previousRouteCount - updatedConfig.routes.size();
        if (disconnectedCount == 0) {
            resultSummary_->copy_label("The selected Sources are not connected to that Destination.");
            return;
        }

        const std::string destinationName = destination.name;
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
        const std::string result = "Disconnected " + std::to_string(disconnectedCount) + " Sources from " +
                                   destinationName + '.';
        resultSummary_->copy_label(result.c_str());
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SourcesPanel::refreshDestinationChoices() {
    const int previousSelection = destinationChoice_->value();
    destinationChoice_->clear();
    for (const Destination& destination : config_.destinations) {
        destinationChoice_->add(destination.name.c_str());
    }
    if (config_.destinations.empty()) {
        destinationChoice_->add("Add a Destination first");
        destinationChoice_->value(0);
        return;
    }

    const int lastIndex = static_cast<int>(config_.destinations.size() - 1);
    destinationChoice_->value(std::clamp(previousSelection, 0, lastIndex));
}

std::vector<std::string> SourcesPanel::selectedSourceIds() const {
    std::vector<std::string> sourceIds;
    for (int line = 1; line <= sourceBrowser_->size(); ++line) {
        if (sourceBrowser_->selected(line) == 0) {
            continue;
        }
        const std::size_t visibleIndex = static_cast<std::size_t>(line - 1);
        sourceIds.push_back(config_.manualSources.at(visibleSourceIndexes_.at(visibleIndex)).id);
    }
    return sourceIds;
}

void SourcesPanel::refreshSelectionState() {
    bool hasSelection = false;
    for (int line = 1; line <= sourceBrowser_->size(); ++line) {
        if (sourceBrowser_->selected(line) != 0) {
            hasSelection = true;
            break;
        }
    }
    if (hasSelection) {
        removeButton_->activate();
    } else {
        removeButton_->deactivate();
    }

    const bool hasDestination = !config_.destinations.empty() && destinationChoice_->value() >= 0;
    if (hasSelection && hasDestination) {
        connectButton_->activate();
        disconnectButton_->activate();
    } else {
        connectButton_->deactivate();
        disconnectButton_->deactivate();
    }
}

void SourcesPanel::reportError(const std::exception& error) const {
    fl_alert("%s", error.what());
}
