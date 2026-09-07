#include <filesystem>
#include <fstream>
#include <string_view>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_PNG_Image.H>
#include <memory>
#include <vector>

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>

#include "activity_panel.hpp"
#include "config_store.hpp"
#include "destinations_panel.hpp"
#include "overview_panel.hpp"
#include "projects_panel.hpp"
#include "settings_panel.hpp"
#include "sources_panel.hpp"
#include "state_store.hpp"
#include "ui_helpers.hpp"

// A separate developer target exercises real panels without watchers or the backup engine.
int main(int argc, char** argv) {
    const auto directory = std::filesystem::path{UI_PREVIEW_DIRECTORY};
    const bool capture = argc > 1 && std::string_view{argv[1]} == "--capture";
    const bool populated = argc > 2 && std::string_view{argv[2]} == "populated";
    std::filesystem::create_directories(directory);
    ConfigStore store(directory / "config.json");
    BackupConfig config;
    StateStore state(directory / "state.db");
    std::vector<ConfiguredProjectsSource> projects;
    if (populated) {
        const auto source = directory / "Sample documents";
        const auto destination = directory / "Backup drive";
        const auto projectRoot = directory / "Projects";
        std::filesystem::create_directories(source);
        std::filesystem::create_directories(destination);
        std::filesystem::create_directories(projectRoot / "Sample project");
        std::filesystem::create_directories(projectRoot / "Available project");
        createProjectWatchMarker(projectRoot / "Sample project");
        config.manualSources.push_back({"sample", source, ManualSourceKind::folder, BackupMode::mirror});
        config.manualSources.push_back({"archive", source / "Long report name for reviewing line clipping and alignment.txt", ManualSourceKind::file, BackupMode::zipped});
        config.projectsRoots.push_back({"projects", projectRoot, BackupMode::zipped});
        config.destinations.push_back({"drive", "Backup drive", DestinationKind::path, destination});
        config.destinations.push_back({"offline", "Travel SSD", DestinationKind::removable, "Z:/", 1234567});
        rebuildBackupRoutes(config);
        projects = discoverConfiguredProjects(config.projectsRoots).sources;
        state.markRouteDirty("sample", "drive");
        state.completeRouteFailure("sample", "drive", "Sample error: destination was unavailable.");
        if (state.recentActivity(1).empty()) {
            state.appendActivity("2026-09-07T10:30:00Z", "error", "Sample error: could not copy a file. Reconnect the destination and retry.", "sample", "drive");
        }
    }
    UiTheme::initializeFonts();
    Fl::scheme("none");
    Fl::background(20, 29, 34);
    Fl::background2(29, 42, 48);
    Fl::foreground(237, 242, 236);
    Fl_Double_Window window(1040, 680, "BackItUpTool â€” UI Preview (no backups run)");
    window.color(UiTheme::kBackground);
    window.size_range(860, 560);
    std::vector<Fl_Group*> panels;
    const auto select = [&](std::size_t selected) {
        for (std::size_t index = 0; index < panels.size(); ++index) {
            if (index == selected) { panels[index]->show(); }
            else { panels[index]->hide(); }
        }
        window.redraw();
    };
    const auto changed = [&] { rebuildBackupRoutes(config); };
    panels.push_back(new OverviewPanel(156, 52, 884, 602, config, projects, state,
                                      [&] { select(1); }, [&] { select(3); }));
    panels.push_back(new SourcesPanel(156, 52, 884, 602, config, store, state, changed));
    panels.push_back(new ProjectsPanel(156, 52, 884, 602, config, store, state, changed));
    panels.push_back(new DestinationsPanel(156, 52, 884, 602, config, store, state, changed));
    panels.push_back(new ActivityPanel(156, 52, 884, 602, config, state));
    panels.push_back(new SettingsPanel(156, 52, 884, 602, config, store, changed, [] {}, [] {}));
    struct Navigation { std::function<void()> callback; };
    std::vector<std::unique_ptr<Navigation>> navigation;
    const char* names[] = {"Overview", "Sources", "Projects", "Destinations", "Activity", "Settings"};
    for (std::size_t index = 0; index < panels.size(); ++index) {
        auto* button = new ActionButton(8, 64 + static_cast<int>(index) * 44, 140, 36, names[index]);
        navigation.push_back(std::make_unique<Navigation>(Navigation{[&, index] { select(index); }}));
        button->callback([](Fl_Widget*, void* context) { static_cast<Navigation*>(context)->callback(); }, navigation.back().get());
    }
    Ui::label(16, 8, 600, 36, "BackItUpTool Â· Visual preview Â· Backup engine disabled", 13, UiTheme::kSecondaryText);
    window.end();
    window.resizable(panels[0]);
    select(0);
    if (capture) {
        for (const int width : {1040, 860}) {
            const int height = width == 1040 ? 680 : 560;
            window.size(width, height);
            for (auto* panel : panels) { panel->resize(156, 52, width - 156, height - 78); }
            for (std::size_t index = 0; index < panels.size(); ++index) {
                select(index);
                Fl_Image_Surface surface(width, height);
                Fl_Surface_Device::push_current(&surface);
                surface.draw(&window);
                std::unique_ptr<Fl_RGB_Image> rendered(surface.image());
                Fl_Surface_Device::pop_current();
                const auto filename = directory / (std::string{populated ? "populated-" : "empty-"} + names[index] + "-" + std::to_string(width) + ".png");
                if (fl_write_png(filename.string().c_str(), rendered.get()) != 0) { return 2; }
            }
        }
        return 0;
    }
    showWithDarkWindowChrome(window);
    return Fl::run();
}
