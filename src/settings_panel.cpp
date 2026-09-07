#include "settings_panel.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Int_Input.H>
#include <FL/fl_ask.H>

#include "config_store.hpp"
#include "startup_registration.hpp"
#include "ui_theme.hpp"

namespace {

constexpr std::uint64_t kBytesPerMiB = 1024ULL * 1024ULL;

Fl_Box* addLabel(int x, int y, int width, int height, const char* text, int size, Fl_Color color,
                 Fl_Font font = UiTheme::kUiFont) {
    auto* label = new Fl_Box(x, y, width, height, text);
    label->box(FL_NO_BOX);
    label->labelsize(size);
    label->labelcolor(color);
    label->labelfont(font);
    label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return label;
}

void addDivider(int x, int y, int width) {
    auto* divider = new Fl_Box(x, y, width, 1);
    divider->box(FL_FLAT_BOX);
    divider->color(UiTheme::kBorder);
}

void styleInput(Fl_Int_Input& input) {
    input.box(FL_BORDER_BOX);
    input.color(UiTheme::kSurface);
    input.textcolor(UiTheme::kText);
    input.cursor_color(UiTheme::kText);
    input.selection_color(UiTheme::kSelection);
    input.textfont(UiTheme::kMonoFont);
    input.textsize(12);
}

void styleButton(Fl_Button& button) {
    button.box(FL_FLAT_BOX);
    button.down_box(FL_FLAT_BOX);
    button.color(UiTheme::kPrimary);
    button.down_color(UiTheme::kPrimaryPressed);
    button.labelcolor(UiTheme::kText);
    button.labelfont(UiTheme::kUiFontSemibold);
    button.labelsize(12);
    button.clear_visible_focus();
}

[[nodiscard]] int positiveInt(const char* text, const char* fieldName) {
    try {
        const std::string value{text};
        std::size_t parsedLength = 0;
        const long parsed = std::stol(value, &parsedLength);
        if (parsedLength != value.size() || parsed <= 0 || parsed > 100000) {
            throw std::invalid_argument("range");
        }
        return static_cast<int>(parsed);
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string{fieldName} + " must be a positive whole number.");
    }
}

}  // namespace

SettingsPanel::SettingsPanel(int x, int y, int width, int height, BackupConfig& config,
                             const ConfigStore& configStore, std::function<void()> configChangedCallback)
    : Fl_Group(x, y, width, height), config_(config), configStore_(configStore),
      configChangedCallback_(std::move(configChangedCallback)) {
    box(FL_FLAT_BOX);
    color(UiTheme::kBackground);
    begin();

    addLabel(x + 16, y + 10, width - 32, 28, "Settings", 18, UiTheme::kText, UiTheme::kUiFontSemibold);
    addLabel(x + 16, y + 36, width - 32, 20,
             "These values control when changes settle and which Project files need approval.", 11,
             UiTheme::kSecondaryText);

    addLabel(x + 16, y + 72, width - 32, 22, "Watching", 13, UiTheme::kText, UiTheme::kUiFontSemibold);
    addDivider(x + 16, y + 98, width - 32);
    addLabel(x + 16, y + 108, 230, 28, "Windows change detection", 12, UiTheme::kText);
    addLabel(x + 250, y + 108, width - 266, 28, "ReadDirectoryChangesW — no scheduled Source scans", 11,
             UiTheme::kSecondaryText);

    addLabel(x + 16, y + 146, 230, 30, "Settle delay", 12, UiTheme::kText);
    debounceInput_ = new Fl_Int_Input(x + 250, y + 146, 90, 28);
    styleInput(*debounceInput_);
    addLabel(x + 348, y + 146, 180, 28, "seconds after the last change", 11, UiTheme::kSecondaryText);

    addLabel(x + 16, y + 198, width - 32, 22, "Project approval", 13, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addDivider(x + 16, y + 224, width - 32);
    addLabel(x + 16, y + 236, 230, 30, "Large file warning", 12, UiTheme::kText);
    largeFileInput_ = new Fl_Int_Input(x + 250, y + 236, 90, 28);
    styleInput(*largeFileInput_);
    addLabel(x + 348, y + 236, 120, 28, "MiB per file", 11, UiTheme::kSecondaryText);

    addLabel(x + 16, y + 286, width - 32, 22, "Windows startup", 13, UiTheme::kText,
             UiTheme::kUiFontSemibold);
    addDivider(x + 16, y + 312, width - 32);
    launchAtStartupCheckbox_ = new Fl_Check_Button(
        x + 16, y + 324, width - 32, 30, "Launch BackItUp Tool when Windows starts");
    launchAtStartupCheckbox_->color(UiTheme::kBackground);
    launchAtStartupCheckbox_->selection_color(UiTheme::kPrimary);
    launchAtStartupCheckbox_->labelcolor(UiTheme::kText);
    launchAtStartupCheckbox_->labelfont(UiTheme::kUiFont);
    launchAtStartupCheckbox_->labelsize(12);
    launchAtStartupCheckbox_->clear_visible_focus();

    saveButton_ = new Fl_Button(x + 250, y + 372, 112, 30, "Save settings");
    styleButton(*saveButton_);
    saveButton_->callback(saveCallback, this);

    resultSummary_ = addLabel(x + 16, y + height - 28, width - 32, 20, "", 11, UiTheme::kSecondaryText);

    end();
    refresh();
}

void SettingsPanel::refresh() {
    const std::string debounce = std::to_string(config_.settings.debounceSeconds);
    const std::string largeFile = std::to_string(config_.settings.largeFileThresholdBytes / kBytesPerMiB);
    debounceInput_->value(debounce.c_str());
    largeFileInput_->value(largeFile.c_str());
    try {
        launchAtStartupCheckbox_->value(isLaunchAtStartupEnabled() ? 1 : 0);
    } catch (const std::exception& error) {
        launchAtStartupCheckbox_->value(0);
        resultSummary_->copy_label(error.what());
        redraw();
        return;
    }
    resultSummary_->copy_label("Changes are saved in config.json and applied immediately.");
    redraw();
}

void SettingsPanel::saveCallback(Fl_Widget*, void* context) {
    static_cast<SettingsPanel*>(context)->save();
}

void SettingsPanel::save() {
    try {
        BackupConfig updated = config_;
        updated.settings.debounceSeconds = positiveInt(debounceInput_->value(), "Settle delay");
        updated.settings.largeFileThresholdBytes =
            static_cast<std::uint64_t>(positiveInt(largeFileInput_->value(), "Large file warning")) * kBytesPerMiB;
        const bool wasLaunchAtStartupEnabled = isLaunchAtStartupEnabled();
        const bool shouldLaunchAtStartup = launchAtStartupCheckbox_->value() != 0;
        setLaunchAtStartupEnabled(shouldLaunchAtStartup);
        try {
            configStore_.save(updated);
        } catch (...) {
            try {
                setLaunchAtStartupEnabled(wasLaunchAtStartupEnabled);
            } catch (const std::exception&) {
            }
            throw;
        }
        config_ = std::move(updated);
        configChangedCallback_();
        resultSummary_->copy_label("Settings saved.");
    } catch (const std::exception& error) {
        reportError(error);
    }
}

void SettingsPanel::reportError(const std::exception& error) const {
    fl_alert("%s", error.what());
}
