#include "settings_panel.hpp"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Scroll.H>

#include "config_store.hpp"
#include "startup_registration.hpp"
#include "ui_helpers.hpp"

namespace {
constexpr std::uint64_t kBytesPerMiB = 1024ULL * 1024ULL;

[[nodiscard]] int positiveInt(const char* text) {
    const std::string value{text};
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number < 1 || number > 100000) {
        return 0;
    }
    return number;
}

void styleInput(Fl_Int_Input& input) {
    input.box(FL_BORDER_BOX);
    input.color(UiTheme::kSurface);
    input.textcolor(UiTheme::kText);
    input.cursor_color(UiTheme::kText);
    input.selection_color(UiTheme::kSelection);
    input.textfont(UiTheme::kUiFont);
    input.textsize(14);
}
}

SettingsPanel::SettingsPanel(int x, int y, int width, int height, BackupConfig& config,
                             const ConfigStore& configStore, std::function<void()> configChangedCallback,
                             std::function<void()> openLogs, std::function<void()> enableBadges)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      configChangedCallback_(std::move(configChangedCallback)), openLogs_(std::move(openLogs)),
      enableBadges_(std::move(enableBadges)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();
    auto* scroll = new Fl_Scroll(x, y, width, height);
    scroll->type(Fl_Scroll::VERTICAL);
    scroll->color(UiTheme::kBackground);
    Ui::label(x + 24, y + 24, 560, 28, "Automatic backups", 16, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 56, 260, 32, "Wait after a file changes");
    debounceInput_ = new Fl_Int_Input(x + 300, y + 56, 100, 32);
    styleInput(*debounceInput_);
    Ui::label(x + 412, y + 56, 160, 32, "seconds", 13, UiTheme::kSecondaryText);
    Ui::label(x + 24, y + 94, 560, 28, "Wait for edits to settle before copying. Default: 8 seconds.", 13, UiTheme::kSecondaryText);
    debounceError_ = Ui::label(x + 24, y + 120, 560, 22, "", 12, UiTheme::kError);
    Ui::label(x + 24, y + 154, 560, 28, "Large project files", 16, UiTheme::kText, UiTheme::kUiFontSemibold);
    Ui::label(x + 24, y + 186, 260, 32, "Ask about files larger than");
    largeFileInput_ = new Fl_Int_Input(x + 300, y + 186, 100, 32);
    styleInput(*largeFileInput_);
    Ui::label(x + 412, y + 186, 160, 32, "MiB per file", 13, UiTheme::kSecondaryText);
    Ui::label(x + 24, y + 224, 560, 36,
              "Default: 50 MiB. Projects set to skip large files use this limit in future backups.", 13, UiTheme::kSecondaryText);
    largeFileError_ = Ui::label(x + 24, y + 262, 560, 22, "", 12, UiTheme::kError);
    Ui::label(x + 24, y + 296, 560, 28, "Windows integration", 16, UiTheme::kText, UiTheme::kUiFontSemibold);
    launchAtStartupCheckbox_ = new Fl_Check_Button(x + 24, y + 330, 560, 32, "  Start BackItUpTool with Windows, in the tray");
    launchAtStartupCheckbox_->color(UiTheme::kBackground);
    launchAtStartupCheckbox_->selection_color(UiTheme::kPrimary);
    launchAtStartupCheckbox_->labelcolor(UiTheme::kText);
    launchAtStartupCheckbox_->labelfont(UiTheme::kUiFont);
    launchAtStartupCheckbox_->labelsize(14);
    Ui::label(x + 24, y + 368, 560, 40,
              "Explorer folder badges show which folders are watched. Enabling them may ask for administrator approval.",
              13, UiTheme::kSecondaryText);
    auto* badges = new ActionButton(x + 24, y + 416, 228, 36, "Enable Explorer badges");
    badges->callback([](Fl_Widget*, void* context) {
        auto* panel = static_cast<SettingsPanel*>(context);
        try { panel->enableBadges_(); panel->resultSummary_->copy_label("Explorer badges enabled. Reopen Explorer to see them."); }
        catch (const std::exception& error) { panel->reportError(error); }
    }, this);
    auto* logs = new ActionButton(x + 264, y + 416, 320, 36, "Open app logs and configuration");
    logs->callback([](Fl_Widget*, void* context) { static_cast<SettingsPanel*>(context)->openLogs_(); }, this);
    saveButton_ = new ActionButton(x + 24, y + 468, 144, 36, "Save changes");
    Ui::styleButton(*saveButton_, true);
    saveButton_->callback(saveCallback, this);
    discardButton_ = new ActionButton(x + 180, y + 468, 112, 36, "Discard");
    discardButton_->callback([](Fl_Widget*, void* context) {
        auto* panel = static_cast<SettingsPanel*>(context);
        panel->hasDraft_ = false;
        panel->refresh();
    }, this);
    resultSummary_ = Ui::label(x + 24, y + 516, 560, 56, "", 13, UiTheme::kSecondaryText);
    for (auto* input : {debounceInput_, largeFileInput_}) {
        input->when(FL_WHEN_CHANGED);
        input->callback(editCallback, this);
    }
    launchAtStartupCheckbox_->callback(editCallback, this);
    scroll->end();
    end();
    resizable(scroll);
    refresh();
}

void SettingsPanel::refresh() {
    if (hasDraft_) { return; }
    savedDebounce_ = std::to_string(config_.settings.debounceSeconds);
    savedLargeFile_ = std::to_string(config_.settings.largeFileThresholdBytes / kBytesPerMiB);
    debounceInput_->value(savedDebounce_.c_str());
    largeFileInput_->value(savedLargeFile_.c_str());
    try {
        savedStartup_ = isLaunchAtStartupEnabled() ? 1 : 0;
        isStartupKnown_ = true;
        launchAtStartupCheckbox_->activate();
        launchAtStartupCheckbox_->value(savedStartup_);
    } catch (const std::exception& error) {
        isStartupKnown_ = false;
        launchAtStartupCheckbox_->deactivate();
        saveButton_->deactivate();
        reportError(error);
        return;
    }
    updateDraft();
}

void SettingsPanel::editCallback(Fl_Widget*, void* context) {
    static_cast<SettingsPanel*>(context)->updateDraft();
}

void SettingsPanel::updateDraft() {
    hasDraft_ = savedDebounce_ != debounceInput_->value() || savedLargeFile_ != largeFileInput_->value() ||
                savedStartup_ != launchAtStartupCheckbox_->value();
    const bool validDelay = positiveInt(debounceInput_->value()) != 0;
    const bool validLimit = positiveInt(largeFileInput_->value()) != 0;
    debounceError_->copy_label(validDelay ? "" : "Enter a whole number from 1 to 100000 seconds.");
    largeFileError_->copy_label(validLimit ? "" : "Enter a whole number from 1 to 100000 MiB.");
    if (hasDraft_ && validDelay && validLimit && isStartupKnown_) { saveButton_->activate(); }
    else { saveButton_->deactivate(); }
    if (hasDraft_) { discardButton_->activate(); }
    else { discardButton_->deactivate(); }
    resultSummary_->labelcolor(UiTheme::kSecondaryText);
    resultSummary_->copy_label(hasDraft_ ? "Unsaved changes. Your edits stay here when you change pages." : "Changes take effect when you save.");
    redraw();
}

void SettingsPanel::saveCallback(Fl_Widget*, void* context) { static_cast<SettingsPanel*>(context)->save(); }

void SettingsPanel::save() {
    updateDraft();
    if (!saveButton_->active()) { return; }
    try {
        BackupConfig updated = config_;
        updated.settings.debounceSeconds = positiveInt(debounceInput_->value());
        updated.settings.largeFileThresholdBytes = static_cast<std::uint64_t>(positiveInt(largeFileInput_->value())) * kBytesPerMiB;
        const bool wasEnabled = isLaunchAtStartupEnabled();
        setLaunchAtStartupEnabled(launchAtStartupCheckbox_->value() != 0);
        try { configStore_.save(updated); }
        catch (...) {
            try { setLaunchAtStartupEnabled(wasEnabled); }
            catch (...) { throw std::runtime_error("Settings were not saved, and the Windows startup setting could not be restored. Check it again before retrying."); }
            throw;
        }
        config_ = std::move(updated);
        hasDraft_ = false;
        refresh();
        try { configChangedCallback_(); }
        catch (const std::exception& error) {
            const std::string message = std::string{"Settings saved, but applying them failed: "} + error.what();
            resultSummary_->copy_label(message.c_str());
            resultSummary_->labelcolor(UiTheme::kError);
            return;
        }
        resultSummary_->copy_label("Settings saved and applied.");
        resultSummary_->labelcolor(UiTheme::kSafe);
    } catch (const std::exception& error) { reportError(error); }
}

void SettingsPanel::reportError(const std::exception& error) const {
    resultSummary_->labelcolor(UiTheme::kError);
    resultSummary_->copy_label(error.what());
}
