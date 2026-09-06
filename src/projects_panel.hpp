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
class Fl_Multi_Browser;
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
    Fl_Button* removeButton_{};
    Fl_Multi_Browser* rootBrowser_{};
    Fl_Box* resultSummary_{};
    ConfiguredProjectsDiscovery discovery_;

    static void addRootsCallback(Fl_Widget*, void* context);
    static void removeCallback(Fl_Widget*, void* context);
    static void selectionCallback(Fl_Widget*, void* context);

    void addRoots();
    void removeSelectedRoots();
    [[nodiscard]] std::vector<std::string> selectedRootIds() const;
    void refreshSelectionState();
    void reportError(const std::exception& error) const;
};
