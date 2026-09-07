#pragma once

#include <optional>
#include <string>

#include <FL/Fl_Round_Button.H>

#include "backup_config.hpp"
#include "ui_helpers.hpp"

[[nodiscard]] inline std::optional<BackupMode> chooseBackupMode(std::size_t itemCount) {
    struct Choice {
        std::optional<BackupMode> mode;
        Fl_Round_Button* mirror{};
        Fl_Round_Button* zipped{};
        ActionButton* add{};
    } choice;
    Fl_Double_Window dialog(640, 388, "Choose how to back up");
    dialog.color(UiTheme::kBackground);
    const std::string heading = "How should these " + std::to_string(itemCount) + " items be backed up?";
    Ui::label(24, 20, 592, 36, heading.c_str(), 20, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(24, 62, 592, 36, "You can choose destinations when you add each source or watched project.", 13,
              UiTheme::kSecondaryText);
    choice.mirror = new Fl_Round_Button(24, 110, 592, 32, "Mirror — a browsable copy of the latest state");
    choice.zipped = new Fl_Round_Button(24, 214, 592, 32, "Zipped — compressed snapshots with history");
    for (auto* option : {choice.mirror, choice.zipped}) {
        option->type(FL_RADIO_BUTTON);
        option->labelcolor(UiTheme::kText);
        option->labelfont(UiTheme::kUiFontSemibold);
        option->labelsize(14);
        option->selection_color(UiTheme::kPrimary);
        option->callback([](Fl_Widget*, void* context) {
            static_cast<Choice*>(context)->add->activate();
        }, &choice);
    }
    Ui::label(52, 146, 544, 56,
              "Deleted files in mirrored folders are also removed from the backup. Choose Zipped if you need older snapshots.",
              13, UiTheme::kSecondaryText);
    Ui::label(52, 250, 544, 56,
              "Keeps daily and monthly history. A run with no changes does not create another ZIP.",
              13, UiTheme::kSecondaryText);
    auto* cancel = new ActionButton(368, 328, 112, 36, "Cancel");
    cancel->callback([](Fl_Widget* widget, void*) { widget->window()->hide(); });
    choice.add = new ActionButton(492, 328, 124, 36, "Add items");
    Ui::styleButton(*choice.add, true);
    choice.add->deactivate();
    choice.add->callback([](Fl_Widget* widget, void* context) {
        auto* choice = static_cast<Choice*>(context);
        choice->mode = choice->mirror->value() ? BackupMode::mirror : BackupMode::zipped;
        widget->window()->hide();
    }, &choice);
    dialog.end();
    dialog.set_modal();
    showWithDarkWindowChrome(dialog);
    choice.mirror->take_focus();
    while (dialog.shown()) { Fl::wait(); }
    return choice.mode;
}
