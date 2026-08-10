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
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Browser.H>
#include <FL/fl_ask.H>
#include <FL/platform.H>

#include "config_store.hpp"
#include "native_file_dialog.hpp"

namespace {

const Fl_Color kPanel = fl_rgb_color(27, 36, 48);
const Fl_Color kPanelRaised = fl_rgb_color(42, 55, 72);
const Fl_Color kText = fl_rgb_color(235, 240, 247);
const Fl_Color kMuted = fl_rgb_color(149, 164, 182);
const Fl_Color kAccent = fl_rgb_color(55, 183, 158);
const Fl_Color kInputBackground = fl_rgb_color(247, 249, 252);
const Fl_Color kInputText = fl_rgb_color(24, 31, 42);
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

void styleButton(Fl_Button& button) {
    button.box(FL_THIN_UP_BOX);
    button.color(kPanelRaised);
    button.selection_color(kAccent);
    button.labelcolor(kText);
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
    color(kPanel);
    begin();

    addLabel(x + 20, y + 14, 200, 32, "Manual Sources", 22, kText, FL_HELVETICA_BOLD);
    addLabel(x + width - 300, y + 16, 52, 28, "Search", 11, kMuted);
    searchInput_ = new Fl_Input(x + width - 245, y + 16, 225, 28);
    searchInput_->color(kInputBackground);
    searchInput_->textcolor(kInputText);
    searchInput_->selection_color(kAccent);
    searchInput_->when(FL_WHEN_CHANGED);
    searchInput_->callback(searchCallback, this);

    auto* addFilesButton = new Fl_Button(x + 20, y + 60, 120, 34, "Add files");
    styleButton(*addFilesButton);
    addFilesButton->callback(addFilesCallback, this);

    auto* addFoldersButton = new Fl_Button(x + 150, y + 60, 120, 34, "Add folders");
    styleButton(*addFoldersButton);
    addFoldersButton->callback(addFoldersCallback, this);

    removeButton_ = new Fl_Button(x + 280, y + 60, 145, 34, "Remove selected");
    styleButton(*removeButton_);
    removeButton_->callback(removeCallback, this);
    removeButton_->deactivate();

    addLabel(x + 24, y + 105, 78, 24, "Type", 11, kMuted, FL_HELVETICA_BOLD);
    addLabel(x + 110, y + 105, 420, 24, "Path", 11, kMuted, FL_HELVETICA_BOLD);
    addLabel(x + 540, y + 105, 110, 24, "Destinations", 11, kMuted, FL_HELVETICA_BOLD);

    sourceBrowser_ = new Fl_Multi_Browser(x + 20, y + 130, width - 40, height - 180);
    sourceBrowser_->color(fl_rgb_color(22, 30, 41));
    sourceBrowser_->textcolor(kText);
    sourceBrowser_->selection_color(fl_rgb_color(45, 102, 99));
    sourceBrowser_->textsize(12);
    sourceBrowser_->column_widths(kSourceColumnWidths);
    sourceBrowser_->column_char('\t');
    sourceBrowser_->format_char(0);
    sourceBrowser_->callback(selectionCallback, this);
    sourceBrowser_->when(FL_WHEN_CHANGED);

    resultSummary_ = addLabel(x + 20, y + height - 42, width - 40, 24, "", 11, kMuted);

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
    std::unordered_set<std::string> selectedIds;
    for (int line = 1; line <= sourceBrowser_->size(); ++line) {
        if (sourceBrowser_->selected(line) == 0) {
            continue;
        }
        const std::size_t visibleIndex = static_cast<std::size_t>(line - 1);
        selectedIds.insert(config_.manualSources.at(visibleSourceIndexes_.at(visibleIndex)).id);
    }
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
