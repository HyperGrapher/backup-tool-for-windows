#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>
#include <utility>

#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Round_Button.H>

#include "backup_config.hpp"
#include "ui_controls.hpp"
#include "ui_helpers.hpp"
#include "ui_table.hpp"
#include "ui_theme.hpp"

struct BackupItemOptions {
    BackupMode backupMode{BackupMode::mirror};
    bool followSymbolicLinks{};
    std::vector<std::string> destinationIds;
};

struct BackupItemOptionsDialogInput {
    std::optional<BackupMode> fixedBackupMode;
    BackupItemOptions initial;
    bool isEditing{};
};

[[nodiscard]] inline std::optional<BackupItemOptions> chooseBackupItemOptions(
    const BackupConfig& config, std::size_t itemCount, BackupItemOptionsDialogInput input) {
    struct DialogState {
        std::optional<BackupItemOptions> result;
        Fl_Round_Button* mirror{};
        Fl_Round_Button* zipped{};
        Fl_Check_Button* follow{};
        DataTable* destinations{};
        ActionButton* confirm{};
        BackupMode fixedMode{BackupMode::mirror};
    } state;

    if (config.destinations.empty()) {
        Ui::showDetails("Add at least one destination before adding a source or watched project.",
                        "Destination required");
        return std::nullopt;
    }

    const bool isEditing = input.isEditing;
    const int destinationHeight = std::clamp(static_cast<int>(config.destinations.size()) * 42 + 32, 116, 242);
    const int dialogHeight = destinationHeight + (input.fixedBackupMode.has_value() ? 244 : 316);
    Fl_Double_Window dialog(720, dialogHeight, isEditing ? "Edit backup options" : "Choose backup options");
    dialog.color(UiTheme::kBackground);
    const std::string heading = isEditing ? "Update these backup options" :
        "Choose options for " + std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");
    Ui::label(24, 20, 672, 32, heading.c_str(), 20, UiTheme::kText, UiTheme::kUiFontSemibold);
    int y = 64;
    if (input.fixedBackupMode.has_value()) {
        state.mirror = nullptr;
        state.zipped = nullptr;
        const char* mode = *input.fixedBackupMode == BackupMode::mirror ? "Mirror" : "Zipped";
        Ui::label(24, y, 672, 28, (std::string{"Backup mode: "} + mode).c_str(), 14, UiTheme::kText,
                  UiTheme::kUiFontSemibold);
        input.initial.backupMode = *input.fixedBackupMode;
        state.fixedMode = *input.fixedBackupMode;
        y += 38;
    } else {
        Ui::label(24, y, 672, 24, "Backup mode", 13, UiTheme::kSecondaryText);
        y += 28;
        auto* modes = new Fl_Group(24, y, 664, 30);
        state.mirror = new Fl_Round_Button(24, y, 318, 30, "Mirror");
        state.zipped = new Fl_Round_Button(370, y, 318, 30, "Zipped");
        for (auto* option : {state.mirror, state.zipped}) {
            option->type(FL_RADIO_BUTTON);
            option->labelcolor(UiTheme::kText);
            option->labelfont(UiTheme::kUiFontSemibold);
            option->labelsize(13);
            option->selection_color(UiTheme::kPrimary);

        }
        modes->end();
        (input.initial.backupMode == BackupMode::mirror ? state.mirror : state.zipped)->value(1);
        y += 42;
    }

    state.follow = new Fl_Check_Button(24, y, 664, 30, "Follow symbolic links inside folders");
    state.follow->value(input.initial.followSymbolicLinks ? 1 : 0);
    state.follow->labelcolor(UiTheme::kText);
    state.follow->labelfont(UiTheme::kUiFont);
    state.follow->labelsize(13);
    state.follow->selection_color(UiTheme::kPrimary);
    y += 42;

    state.destinations = new DataTable(24, y, 664, destinationHeight, {"Destination path"}, {100});
    std::vector<TableRow> destinationRows;
    destinationRows.reserve(config.destinations.size());
    for (std::size_t index = 0; index < config.destinations.size(); ++index) {
        const Destination& destination = config.destinations[index];
        const std::string destinationPath = Ui::pathText(destination.root);
        destinationRows.push_back({destination.id, {destinationPath}, destinationPath});
    }
    state.destinations->setRows(std::move(destinationRows));
    for (std::size_t index = 0; index < config.destinations.size(); ++index) {
        const Destination& destination = config.destinations[index];
        const bool selected = (!isEditing && input.initial.destinationIds.empty()) ||
                              std::ranges::find(input.initial.destinationIds, destination.id) !=
                                  input.initial.destinationIds.end();
        state.destinations->select_row(static_cast<int>(index), selected ? 1 : 0);
    }
    state.destinations->callback([](Fl_Widget*, void* context) {
        auto* dialogState = static_cast<DialogState*>(context);
        if (dialogState->destinations->selectedKeys().empty()) {
            dialogState->confirm->deactivate();
        } else {
            dialogState->confirm->activate();
        }
    }, &state);
    y += destinationHeight + 26;

    auto* cancel = new ActionButton(448, y, 112, 36, "Cancel");
    cancel->callback([](Fl_Widget* widget, void*) { widget->window()->hide(); });
    state.confirm = new ActionButton(572, y, 116, 36, isEditing ? "Save" : "Continue");
    Ui::styleButton(*state.confirm, true);
    state.confirm->callback([](Fl_Widget* widget, void* context) {
        auto* dialogState = static_cast<DialogState*>(context);
        BackupItemOptions result;
        result.backupMode = dialogState->mirror != nullptr
                                ? (dialogState->mirror->value() ? BackupMode::mirror : BackupMode::zipped)
                                : dialogState->fixedMode;
        result.followSymbolicLinks = dialogState->follow->value() != 0;
        result.destinationIds = dialogState->destinations->selectedKeys();
        if (result.destinationIds.empty()) {
            return;
        }
        dialogState->result = std::move(result);
        widget->window()->hide();
    }, &state);
    state.confirm->deactivate();
    if (!state.destinations->selectedKeys().empty()) { state.confirm->activate(); }

    dialog.end();
    dialog.set_modal();
    showWithDarkWindowChrome(dialog);
    state.confirm->take_focus();
    while (dialog.shown()) {
        Fl::wait();
    }
    return state.result;
}
