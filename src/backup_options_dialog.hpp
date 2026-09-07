#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Scroll.H>

#include "backup_config.hpp"
#include "ui_controls.hpp"
#include "ui_helpers.hpp"
#include "ui_theme.hpp"

struct BackupItemOptions {
    BackupMode backupMode{BackupMode::mirror};
    bool followSymbolicLinks{};
    std::vector<std::string> destinationIds;
};

struct BackupItemOptionsDialogInput {
    std::optional<BackupMode> fixedBackupMode;
    BackupItemOptions initial;
};

[[nodiscard]] inline std::optional<BackupItemOptions> chooseBackupItemOptions(
    const BackupConfig& config, std::size_t itemCount, BackupItemOptionsDialogInput input) {
    struct DialogState {
        std::optional<BackupItemOptions> result;
        Fl_Round_Button* mirror{};
        Fl_Round_Button* zipped{};
        Fl_Check_Button* follow{};
        std::vector<Fl_Check_Button*> destinations;
        std::vector<std::string> destinationIds;
        ActionButton* confirm{};
        BackupMode fixedMode{BackupMode::mirror};
    } state;

    if (config.destinations.empty()) {
        Ui::showDetails("Add at least one destination before adding a source or watched project.",
                        "Destination required");
        return std::nullopt;
    }

    const bool isEditing = !input.initial.destinationIds.empty();
    const int destinationRowHeight = 30;
    const int destinationHeight = std::clamp(static_cast<int>(config.destinations.size()) * destinationRowHeight, 90, 210);
    const int dialogHeight = destinationHeight + (input.fixedBackupMode.has_value() ? 300 : 390);
    Fl_Double_Window dialog(720, dialogHeight, isEditing ? "Edit backup options" : "Choose backup options");
    dialog.color(UiTheme::kBackground);
    const std::string heading = isEditing ? "Update these backup options" :
        "Choose options for " + std::to_string(itemCount) + (itemCount == 1 ? " item" : " items");
    Ui::label(24, 20, 672, 32, heading.c_str(), 20, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(24, 56, 672, 32,
              "Pick one or more destinations. You can change this later from Details.", 13,
              UiTheme::kSecondaryText);

    int y = 96;
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
        state.mirror = new Fl_Round_Button(24, y, 318, 30, "Mirror");
        state.zipped = new Fl_Round_Button(370, y, 318, 30, "Zipped");
        for (auto* option : {state.mirror, state.zipped}) {
            option->type(FL_RADIO_BUTTON);
            option->labelcolor(UiTheme::kText);
            option->labelfont(UiTheme::kUiFontSemibold);
            option->labelsize(13);
            option->selection_color(UiTheme::kPrimary);
            option->callback([](Fl_Widget*, void* context) {
                static_cast<DialogState*>(context)->confirm->activate();
            }, &state);
        }
        (input.initial.backupMode == BackupMode::mirror ? state.mirror : state.zipped)->value(1);
        Ui::label(24, y + 34, 664, 40,
                  "Mirror keeps a browsable latest copy. Zipped creates compressed snapshots with history.", 12,
                  UiTheme::kSecondaryText);
        y += 82;
    }

    state.follow = new Fl_Check_Button(24, y, 664, 30, "Follow symbolic links inside folders");
    state.follow->value(input.initial.followSymbolicLinks ? 1 : 0);
    state.follow->labelcolor(UiTheme::kText);
    state.follow->labelfont(UiTheme::kUiFont);
    state.follow->labelsize(13);
    state.follow->selection_color(UiTheme::kPrimary);
    Ui::label(48, y + 28, 640, 34,
              "Include files and folders reached through symbolic links. Links that point back into the source are skipped.",
              12, UiTheme::kSecondaryText);
    y += 70;

    Ui::label(24, y, 664, 24, "Destinations", 13, UiTheme::kSecondaryText);
    y += 28;
    auto* destinationScroll = new Fl_Scroll(24, y, 664, destinationHeight);
    destinationScroll->box(FL_FLAT_BOX);
    destinationScroll->color(UiTheme::kSurface);
    destinationScroll->begin();
    for (std::size_t index = 0; index < config.destinations.size(); ++index) {
        const Destination& destination = config.destinations[index];
        const std::string destinationLabel = destination.name + "  —  " + Ui::pathText(destination.root);
        auto* check = new Fl_Check_Button(destinationScroll->x() + 12,
                                          destinationScroll->y() + static_cast<int>(index) * destinationRowHeight + 4,
                                          destinationScroll->w() - 32, destinationRowHeight - 4);
        check->copy_label(destinationLabel.c_str());
        const bool selected = input.initial.destinationIds.empty() ||
                              std::ranges::find(input.initial.destinationIds, destination.id) !=
                                  input.initial.destinationIds.end();
        check->value(selected ? 1 : 0);
        check->labelcolor(UiTheme::kText);
        check->labelfont(UiTheme::kUiFont);
        check->labelsize(13);
        check->selection_color(UiTheme::kPrimary);
        check->callback([](Fl_Widget*, void* context) {
            auto* dialogState = static_cast<DialogState*>(context);
            const bool anySelected = std::ranges::any_of(dialogState->destinations,
                                                         [](const Fl_Check_Button* button) { return button->value() != 0; });
            if (anySelected) {
                dialogState->confirm->activate();
            } else {
                dialogState->confirm->deactivate();
            }
        }, &state);
        state.destinations.push_back(check);
        state.destinationIds.push_back(destination.id);
    }
    destinationScroll->end();
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
        for (std::size_t index = 0; index < dialogState->destinations.size(); ++index) {
            if (dialogState->destinations[index]->value()) {
                result.destinationIds.push_back(dialogState->destinationIds[index]);
            }
        }
        dialogState->result = std::move(result);
        widget->window()->hide();
    }, &state);
    state.confirm->deactivate();
    for (const Fl_Check_Button* destination : state.destinations) {
        if (destination->value()) {
            state.confirm->activate();
            break;
        }
    }

    dialog.end();
    dialog.set_modal();
    showWithDarkWindowChrome(dialog);
    state.confirm->take_focus();
    while (dialog.shown()) {
        Fl::wait();
    }
    return state.result;
}
