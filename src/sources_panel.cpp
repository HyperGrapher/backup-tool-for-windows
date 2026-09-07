#include "sources_panel.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
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

using Ui::styleButton;

[[nodiscard]] UiTheme::BackupStatus sourceStatus(const ManualSource& source, const BackupConfig& config,
                                                  const StateStore& stateStore) {
    bool hasRoute = false;
    UiTheme::BackupStatus status = UiTheme::BackupStatus::current;
    for (const BackupRoute& route : config.routes) {
        if (route.sourceId != source.id) {
            continue;
        }
        hasRoute = true;
        const std::optional<RouteRuntimeState> state = stateStore.routeState(source.id, route.destinationId);
        if (!state.has_value() || state->isDirty) {
            status = UiTheme::BackupStatus::waiting;
        }
        if (state.has_value() && state->status == RouteStatus::running) {
            status = UiTheme::BackupStatus::syncing;
        }
        if (state.has_value() && state->status == RouteStatus::error) {
            return UiTheme::BackupStatus::error;
        }
    }
    return hasRoute ? status : UiTheme::BackupStatus::inactive;
}

[[nodiscard]] std::string destinationSummary(const BackupConfig& config, std::string_view sourceId) {
    std::string result;
    for (const BackupRoute& route : config.routes) {
        if (route.sourceId != sourceId) {
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

SourcesPanel::SourcesPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                           const StateStore& stateStore, std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      stateStore_(stateStore), configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    Ui::label(x + 24, y + 16, 220, 36, "Sources", 24, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 56, width - 48, 36,
              "What to back up. Choose one or more destinations for each source.", 13, UiTheme::kSecondaryText);
    auto* files = new ActionButton(x + 24, y + 108, 112, 36, "Add files");
    styleButton(*files, true);
    files->callback(addFilesCallback, this);
    auto* folders = new ActionButton(x + 144, y + 108, 124, 36, "Add folders");
    folders->callback(addFoldersCallback, this);
    removeButton_ = new ActionButton(x + 276, y + 108, 132, 36, "Remove…");
    styleButton(*removeButton_, false, true);
    removeButton_->callback(removeCallback, this);
    auto* details = new ActionButton(x + 416, y + 108, 112, 36, "Edit details");
    details->callback(detailsCallback, this);
    Ui::label(x + 24, y + 156, 64, 36, "Search", 13, UiTheme::kSecondaryText);
    searchInput_ = new Fl_Input(x + 92, y + 156, width - 116, 36);
    searchInput_->box(FL_BORDER_BOX);
    searchInput_->color(UiTheme::kSurface);
    searchInput_->textcolor(UiTheme::kText);
    searchInput_->cursor_color(UiTheme::kText);
    searchInput_->textfont(UiTheme::kUiFont);
    searchInput_->textsize(14);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback(searchCallback, this);
    sourceBrowser_ = new DataTable(x + 24, y + 208, width - 48, height - 264,
                                   {"Source / path", "Status", "Backup mode"}, {55, 25, 20});
    sourceBrowser_->callback(selectionCallback, this);
    resultSummary_ = Ui::label(x + 24, y + height - 48, width - 48, 40, "", 12, UiTheme::kSecondaryText);

    end();
    resizable(sourceBrowser_);
    refresh();
}

void SourcesPanel::refresh() {
    const std::string filter = lowercaseAscii(searchInput_->value());
    std::vector<TableRow> rows;
    for (std::size_t index = 0; index < config_.manualSources.size(); ++index) {
        const ManualSource& source = config_.manualSources[index];
        const std::string pathText = pathToUtf8(source.path);
        const std::string typeText = source.kind == ManualSourceKind::file ? "File" : "Folder";
        const std::string modeText = source.backupMode == BackupMode::mirror ? "Mirror" : "Zipped";
        if (!filter.empty() && lowercaseAscii(pathText).find(filter) == std::string::npos &&
            lowercaseAscii(typeText).find(filter) == std::string::npos &&
            lowercaseAscii(modeText).find(filter) == std::string::npos) {
            continue;
        }

        const std::string status = std::string{UiTheme::statusText(sourceStatus(source, config_, stateStore_))};
        rows.push_back({source.id, {Ui::pathText(source.path.filename()) + "\n" + pathText, status, modeText},
                       pathText + "\n" + typeText + " · " + modeText +
                       (source.followSymbolicLinks ? " · Following links" : " · Links skipped") + "\n" + status +
                       "\nDestinations: " + destinationSummary(config_, source.id)});
    }
    const std::string summary = std::to_string(rows.size()) + " shown of " +
                                std::to_string(config_.manualSources.size()) + " sources · Select rows for details or removal";
    sourceBrowser_->emptyMessage(filter.empty() ? "Choose Add files or Add folders to start backing up."
                                               : "No sources match this search. Clear the search to show all sources.");
    sourceBrowser_->setRows(std::move(rows));
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

void SourcesPanel::searchCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refresh();
}

void SourcesPanel::selectionCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refreshSelectionState();
}

void SourcesPanel::detailsCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->editSelectedSource();
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

    const auto options = chooseBackupItemOptions(config_, paths.size(), BackupItemOptionsDialogInput{});
    if (!options.has_value()) {
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
        const std::string sourceId = generateStableId("source");
        updatedConfig.manualSources.push_back(
            ManualSource{sourceId, normalizedPath, kind, options->backupMode, options->followSymbolicLinks});
        for (const std::string& destinationId : options->destinationIds) {
            updatedConfig.routes.push_back(BackupRoute{sourceId, destinationId});
        }
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
    const std::string receipt = "Added " + std::to_string(addedCount) + " sources · " +
                                std::to_string(paths.size() - addedCount) + " already configured";
    resultSummary_->copy_label(receipt.c_str());
}

void SourcesPanel::editSelectedSource() {
    const auto selected = sourceBrowser_->selectedKeys();
    if (selected.size() != 1) {
        resultSummary_->copy_label(selected.empty() ? "Select one source to edit its options."
                                                     : "Select one source at a time to edit its options.");
        return;
    }
    const auto source = std::ranges::find(config_.manualSources, selected.front(), &ManualSource::id);
    if (source == config_.manualSources.end()) {
        return;
    }

    BackupItemOptions initial;
    initial.backupMode = source->backupMode;
    initial.followSymbolicLinks = source->followSymbolicLinks;
    for (const BackupRoute& route : config_.routes) {
        if (route.sourceId == source->id) {
            initial.destinationIds.push_back(route.destinationId);
        }
    }
    const auto options = chooseBackupItemOptions(config_, 1, BackupItemOptionsDialogInput{std::nullopt, initial});
    if (!options.has_value()) {
        return;
    }

    try {
        BackupConfig updatedConfig = config_;
        const auto updatedSource = std::ranges::find(updatedConfig.manualSources, source->id, &ManualSource::id);
        updatedSource->backupMode = options->backupMode;
        updatedSource->followSymbolicLinks = options->followSymbolicLinks;
        std::erase_if(updatedConfig.routes, [&](const BackupRoute& route) { return route.sourceId == source->id; });
        for (const std::string& destinationId : options->destinationIds) {
            updatedConfig.routes.push_back(BackupRoute{source->id, destinationId});
        }
        configStore_.save(updatedConfig);
        config_ = std::move(updatedConfig);
        configChangedCallback_();
        refresh();
        resultSummary_->copy_label("Source options saved.");
    } catch (const std::exception& error) {
        reportError(error);
    }
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

std::vector<std::string> SourcesPanel::selectedSourceIds() const {
    return sourceBrowser_->selectedKeys();
}

void SourcesPanel::refreshSelectionState() {
    const auto count = sourceBrowser_->selectedKeys().size();
    const std::string label = count ? "Remove " + std::to_string(count) + "…" : "Remove…";
    removeButton_->copy_label(label.c_str());
    if (count) { removeButton_->activate(); }
    else { removeButton_->deactivate(); }
}

void SourcesPanel::reportError(const std::exception& error) const {
    fl_alert("%s", error.what());
}
