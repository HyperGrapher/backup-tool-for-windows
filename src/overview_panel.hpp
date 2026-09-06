#pragma once

#include <vector>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class Fl_Box;
class Fl_Browser;
class StateStore;
struct ConfiguredProjectsSource;

class OverviewPanel final : public Fl_Group {
public:
    OverviewPanel(int x, int y, int width, int height, const BackupConfig& config,
                  const std::vector<ConfiguredProjectsSource>& projectsSources, const StateStore& stateStore);

    void refresh();

private:
    const BackupConfig& config_;
    const std::vector<ConfiguredProjectsSource>& projectsSources_;
    const StateStore& stateStore_;
    Fl_Box* destinationHealth_{};
    Fl_Box* pendingRoutes_{};
    Fl_Box* lastBackup_{};
    Fl_Box* lastArchive_{};
    Fl_Box* watcherStatus_{};
    Fl_Browser* recentFailures_{};
};
