#pragma once

#include <vector>
#include <functional>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class Fl_Box;
class Fl_Browser;
class ClearHistoryButton;
class StateStore;
struct ConfiguredProjectsSource;

class OverviewPanel final : public Fl_Group {
public:
    OverviewPanel(int x, int y, int width, int height, const BackupConfig& config,
                  const std::vector<ConfiguredProjectsSource>& projectsSources, StateStore& stateStore,
                  std::function<void()> openSources, std::function<void()> openDestinations);

    void refresh();

private:
    const BackupConfig& config_;
    const std::vector<ConfiguredProjectsSource>& projectsSources_;
    StateStore& stateStore_;
    std::function<void()> openSources_;
    std::function<void()> openDestinations_;
    Fl_Box* healthTitle_{};
    Fl_Box* healthHint_{};
    Fl_Box* destinationHealth_{};
    Fl_Box* pendingRoutes_{};
    Fl_Box* lastBackup_{};
    Fl_Box* lastArchive_{};
    Fl_Box* watcherStatus_{};
    Fl_Browser* recentFailures_{};
    ClearHistoryButton* clearFailuresButton_{};

    static void clearFailuresCallback(Fl_Widget*, void* context);
    void clearFailures();
};
