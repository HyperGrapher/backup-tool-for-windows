#pragma once

#include <exception>
#include <functional>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class ConfigStore;
class Fl_Box;
class Fl_Button;
class Fl_Int_Input;

class SettingsPanel final : public Fl_Group {
public:
    SettingsPanel(int x, int y, int width, int height, BackupConfig& config,
                  const ConfigStore& configStore, std::function<void()> configChangedCallback);

    void refresh();

private:
    BackupConfig& config_;
    const ConfigStore& configStore_;
    std::function<void()> configChangedCallback_;
    Fl_Int_Input* debounceInput_{};
    Fl_Int_Input* largeFileInput_{};
    Fl_Int_Input* projectSizeInput_{};
    Fl_Button* saveButton_{};
    Fl_Box* resultSummary_{};

    static void saveCallback(Fl_Widget*, void* context);

    void save();
    void reportError(const std::exception& error) const;
};
