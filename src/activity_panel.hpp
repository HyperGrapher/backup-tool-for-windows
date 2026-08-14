#pragma once

#include <FL/Fl_Group.H>

struct BackupConfig;
class Fl_Box;
class Fl_Browser;
class StateStore;

class ActivityPanel final : public Fl_Group {
public:
    ActivityPanel(int x, int y, int width, int height, const BackupConfig& config,
                  const StateStore& stateStore);

    void refresh();

private:
    const BackupConfig& config_;
    const StateStore& stateStore_;
    Fl_Browser* activityBrowser_{};
    Fl_Box* resultSummary_{};
};
