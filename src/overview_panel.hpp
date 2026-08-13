#pragma once

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class Fl_Box;
class Fl_Browser;
class StateStore;

class OverviewPanel final : public Fl_Group {
public:
    OverviewPanel(int x, int y, int width, int height, const BackupConfig& config, const StateStore& stateStore);

    void refresh();

private:
    const BackupConfig& config_;
    const StateStore& stateStore_;
    Fl_Box* destinationHealth_{};
    Fl_Box* pendingRoutes_{};
    Fl_Box* lastMirror_{};
    Fl_Box* lastSnapshot_{};
    Fl_Box* watcherStatus_{};
    Fl_Browser* recentFailures_{};
};
