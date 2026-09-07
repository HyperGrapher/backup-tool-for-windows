#pragma once

#include <FL/Fl_Group.H>

struct BackupConfig;
class Fl_Box;
class DataTable;
class Fl_Choice;
class Fl_Input;
class Fl_Button;
class StateStore;

class ActivityPanel final : public Fl_Group {
public:
    ActivityPanel(int x, int y, int width, int height, const BackupConfig& config,
                  StateStore& stateStore);

    void refresh();

private:
    const BackupConfig& config_;
    StateStore& stateStore_;
    DataTable* activityBrowser_{};
    Fl_Box* resultSummary_{};
    Fl_Button* clearActivityButton_{};

    Fl_Choice* filterChoice_{};
    Fl_Input* searchInput_{};
    static void clearActivityCallback(Fl_Widget*, void* context);
    void clearActivity();
};
