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
#include "native_file_dialog.hpp"
#include "state_store.hpp"
#include "ui_theme.hpp"

namespace {

constexpr int kSourceColumnWidths[] = {128, 70, 480, 100, 0};

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

void styleButton(Fl_Button& button, bool isPrimary = false, bool isDanger = false) {
    button.box(FL_FLAT_BOX);
    button.down_box(FL_FLAT_BOX);
    button.color(isPrimary ? UiTheme::kPrimary : (isDanger ? UiTheme::kDanger : UiTheme::kControl));
    button.down_color(isPrimary ? UiTheme::kPrimaryPressed
                                : (isDanger ? UiTheme::kDangerPressed : UiTheme::kPressedControl));
    button.selection_color(isPrimary ? UiTheme::kPrimary : (isDanger ? UiTheme::kDanger : UiTheme::kSelection));
    button.labelcolor(UiTheme::kText);
    button.labelfont(isPrimary ? UiTheme::kUiFontSemibold : UiTheme::kUiFont);
    button.labelsize(12);
}

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, int size, Fl_Color color,
                 Fl_Font font = UiTheme::kUiFont) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    return label;
}

[[nodiscard]] UiTheme::BackupStatus sourceStatus(const ManualSource& source, const BackupConfig& config,
                                                  const StateStore& stateStore) {
    bool hasRoute = false;
    UiTheme::BackupStatus status = UiTheme::BackupStatus::current;
    for (const BackupRoute& route : config.routes) {
        if (route.sourceId != source.id || !route.isMirrorEnabled) {
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

}  // namespace

SourcesPanel::SourcesPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                           const StateStore& stateStore, std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      stateStore_(stateStore), configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 16, y + 10, 180, 28, "Sources", 18, UiTheme::kText, UiTheme::kUiFontSemibold);
    addLabel(x + width - 292, y + 12, 52, 28, "Search", 11, UiTheme::kSecondaryText);
    searchInput_ = new Fl_Input(x + width - 238, y + 12, 222, 28);
    searchInput_->box(FL_BORDER_BOX);
    searchInput_->color(UiTheme::kSurface);
    searchInput_->textcolor(UiTheme::kText);
    searchInput_->cursor_color(UiTheme::kText);
    searchInput_->selection_color(UiTheme::kSelection);
    searchInput_->textfont(UiTheme::kUiFont);
    searchInput_->textsize(12);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback(searchCallback, this);

    auto* addFilesButton = new Fl_Button(x + 16, y + 50, 105, 30, "Add files");
    styleButton(*addFilesButton, true);
    addFilesButton->callback(addFilesCallback, this);

    auto* addFoldersButton = new Fl_Button(x + 129, y + 50, 110, 30, "Add folders");
    styleButton(*addFoldersButton);
    addFoldersButton->callback(addFoldersCallback, this);

    removeButton_ = new Fl_Button(x + 247, y + 50, 132, 30, "Remove selected");
    styleButton(*removeButton_, false, true);
    removeButton_->callback(removeCallback, this);
    removeButton_->deactivate();

    addLabel(x + 20, y + 90, 124, 22, "State", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 148, y + 90, 66, 22, "Type", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 218, y + 90, 470, 22, "Path", 11, UiTheme::kSecondaryText, UiTheme::kUiFontSemibold);
    addLabel(x + 698, y + 90, 100, 22, "Destinations", 11, UiTheme::kSecondaryText,
             UiTheme::kUiFontSemibold);

    sourceBrowser_ = new Fl_Multi_Browser(x + 16, y + 112, width - 32, height - 144);
    sourceBrowser_->box(FL_BORDER_BOX);
    sourceBrowser_->color(UiTheme::kSurface);
    sourceBrowser_->textcolor(UiTheme::kText);
    sourceBrowser_->selection_color(UiTheme::kSelection);
    sourceBrowser_->textsize(12);
    sourceBrowser_->textfont(UiTheme::kUiFont);
    sourceBrowser_->column_widths(kSourceColumnWidths);
    sourceBrowser_->column_char('\t');
    sourceBrowser_->format_char(0);
    sourceBrowser_->callback(selectionCallback, this);
    sourceBrowser_->when(FL_WHEN_CHANGED);

    resultSummary_ = addLabel(x + 16, y + height - 28, width - 32, 20, "", 11, UiTheme::kSecondaryText);

    end();
    resizable(sourceBrowser_);
    refresh();
}

void SourcesPanel::refresh() {
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

        const std::string line = std::string{UiTheme::statusText(sourceStatus(source, config_, stateStore_))} + '\t' +
                                 typeText + '\t' + pathText + '\t' + std::to_string(config_.destinations.size());
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

void SourcesPanel::searchCallback(Fl_Widget*, void* context) {
    static_cast<SourcesPanel*>(context)->refresh();
}

void SourcesPanel::selectionCallback(Fl_Widget*, void* context) {
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

}

void SourcesPanel::reportError(const std::exception& error) const {
    fl_alert("%s", error.what());
}
