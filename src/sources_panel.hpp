#pragma once

#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class ConfigStore;
class Fl_Box;
class Fl_Button;
class Fl_Choice;
class Fl_Input;
class Fl_Multi_Browser;
class StateStore;

class SourcesPanel final : public Fl_Group {
public:
    SourcesPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                 const StateStore& stateStore, std::function<void()> configChangedCallback);

    void refresh();

private:
    BackupConfig& config_;
    const ConfigStore& configStore_;
    const StateStore& stateStore_;
    std::function<void()> configChangedCallback_;
    Fl_Input* searchInput_{};
    Fl_Button* removeButton_{};
    Fl_Choice* destinationChoice_{};
    Fl_Button* connectButton_{};
    Fl_Button* disconnectButton_{};
    Fl_Multi_Browser* sourceBrowser_{};
    Fl_Box* resultSummary_{};
    std::vector<std::size_t> visibleSourceIndexes_;

    static void addFilesCallback(Fl_Widget*, void* context);
    static void addFoldersCallback(Fl_Widget*, void* context);
    static void removeCallback(Fl_Widget*, void* context);
    static void connectCallback(Fl_Widget*, void* context);
    static void disconnectCallback(Fl_Widget*, void* context);
    static void searchCallback(Fl_Widget*, void* context);
    static void selectionCallback(Fl_Widget*, void* context);
    static void destinationChoiceCallback(Fl_Widget*, void* context);

    void addFiles();
    void addFolders();
    void addSources(const std::vector<std::filesystem::path>& paths, ManualSourceKind kind);
    void removeSelectedSources();
    void connectSelectedSources();
    void disconnectSelectedSources();
    void refreshDestinationChoices();
    [[nodiscard]] std::vector<std::string> selectedSourceIds() const;
    void refreshSelectionState();
    void reportError(const std::exception& error) const;
};
