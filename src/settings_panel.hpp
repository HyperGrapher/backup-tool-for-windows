#pragma once

#include <exception>
#include <functional>
#include <string>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"

class ConfigStore;
class Fl_Box;
class Fl_Button;
class Fl_Check_Button;
class Fl_Int_Input;

class SettingsPanel final : public Fl_Group {
public:
    SettingsPanel(int x, int y, int width, int height, BackupConfig& config,
                  const ConfigStore& configStore, std::function<void()> configChangedCallback,
                  std::function<void()> openLogs, std::function<void()> enableBadges);

    void refresh();

private:
    BackupConfig& config_;
    const ConfigStore& configStore_;
    std::function<void()> configChangedCallback_;
    Fl_Int_Input* debounceInput_{};
    Fl_Int_Input* largeFileInput_{};
    Fl_Check_Button* launchAtStartupCheckbox_{};
    Fl_Button* saveButton_{};
    Fl_Box* resultSummary_{};

    Fl_Button* discardButton_{};
    Fl_Box* debounceError_{};
    Fl_Box* largeFileError_{};
    bool hasDraft_{};
    bool isStartupKnown_{};
    int savedStartup_{};
    std::string savedDebounce_;
    std::string savedLargeFile_;
    std::function<void()> openLogs_;
    std::function<void()> enableBadges_;
    static void editCallback(Fl_Widget*, void* context);
    void updateDraft();
    static void saveCallback(Fl_Widget*, void* context);

    void save();
    void reportError(const std::exception& error) const;
};
