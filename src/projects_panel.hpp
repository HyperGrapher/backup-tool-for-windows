#pragma once

#include <exception>
#include <functional>
#include <string>
#include <vector>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"
#include "projects_scanner.hpp"

class ConfigStore;
class Fl_Box;
class Fl_Button;
class DataTable;
class Fl_Choice;
class Fl_Input;
class StateStore;

class ProjectsPanel final : public Fl_Group {
public:
    ProjectsPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                  const StateStore& stateStore, std::function<void()> configChangedCallback);

    void refresh();

private:
    BackupConfig& config_;
    const ConfigStore& configStore_;
    const StateStore& stateStore_;
    std::function<void()> configChangedCallback_;
    Fl_Button* watchButton_{};
    Fl_Button* removeButton_{};
    DataTable* rootBrowser_{};
    Fl_Box* resultSummary_{};
    ConfiguredProjectsDiscovery discovery_;
    Fl_Choice* parentChoice_{};
    Fl_Choice* filterChoice_{};
    Fl_Input* searchInput_{};
    std::vector<std::string> parentIds_;
    std::string selectedParentId_;
    void refreshProjectRows();

    static void addRootsCallback(Fl_Widget*, void* context);
    static void watchCallback(Fl_Widget*, void* context);
    static void removeCallback(Fl_Widget*, void* context);
    static void selectionCallback(Fl_Widget*, void* context);
    static void detailsCallback(Fl_Widget*, void* context);

    void addRoots();
    void watchSelectedFolders();
    void editSelectedProject();
    void removeSelectedRoots();
    [[nodiscard]] std::vector<std::string> selectedRootIds() const;
    [[nodiscard]] std::vector<std::filesystem::path> selectedEligibleFolders() const;
    void refreshSelectionState();
    void reportError(const std::exception& error) const;
};
