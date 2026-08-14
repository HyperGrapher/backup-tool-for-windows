#pragma once

#include <cstddef>
#include <exception>
#include <filesystem>
#include <functional>
#include <vector>

#include <FL/Fl_Group.H>

#include "backup_config.hpp"
#include "connected_volume.hpp"

class ConfigStore;
class Fl_Box;
class Fl_Button;
class Fl_Choice;
class Fl_Multi_Browser;
class StateStore;

class DestinationsPanel final : public Fl_Group {
public:
    DestinationsPanel(int x, int y, int width, int height, BackupConfig& config, const ConfigStore& configStore,
                      const StateStore& stateStore, std::function<void()> configChangedCallback);

    void refresh();

private:
    BackupConfig& config_;
    const ConfigStore& configStore_;
    const StateStore& stateStore_;
    std::function<void()> configChangedCallback_;
    Fl_Choice* connectedDriveChoice_{};
    Fl_Button* addUsbButton_{};
    Fl_Button* removeButton_{};
    Fl_Multi_Browser* destinationBrowser_{};
    Fl_Box* resultSummary_{};
    std::vector<ConnectedVolume> connectedVolumes_;

    static void refreshCallback(Fl_Widget*, void* context);
    static void addUsbCallback(Fl_Widget*, void* context);
    static void addFolderCallback(Fl_Widget*, void* context);
    static void removeCallback(Fl_Widget*, void* context);
    static void selectionCallback(Fl_Widget*, void* context);

    void addSelectedUsb();
    void addFolders();
    void removeSelectedDestinations();
    void refreshConnectedDrives();
    void refreshSelectionState();
    void reportError(const std::exception& error) const;
};
