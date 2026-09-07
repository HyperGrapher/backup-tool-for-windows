#pragma once

#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>

#include <FL/Fl_Box.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Multiline_Output.H>

#include "ui_controls.hpp"
#include "window_theme.hpp"

namespace Ui {

inline Fl_Box* label(int x, int y, int width, int height, const char* text, int size = 14,
                     Fl_Color color = UiTheme::kText, Fl_Font font = UiTheme::kUiFont) {
    auto* box = new Fl_Box(x, y, width, height);
    box->copy_label(text);
    box->box(FL_NO_BOX);
    box->labelfont(font);
    box->labelsize(size);
    box->labelcolor(color);
    box->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
    return box;
}

inline void styleButton(Fl_Button& button, bool isPrimary = false, bool isDanger = false) {
    button.color(isPrimary ? UiTheme::kPrimary : UiTheme::kControl);
    button.labelcolor(isDanger ? UiTheme::kError : UiTheme::kText);
    button.labelfont(UiTheme::kUiFontSemibold);
    button.labelsize(13);
}

[[nodiscard]] inline std::string pathText(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

[[nodiscard]] inline std::string localTime(const std::string& utc) {
    std::tm parsed{};
    std::istringstream input(utc);
    input >> std::get_time(&parsed, "%Y-%m-%dT%H:%M:%SZ");
    if (input.fail()) {
        return utc;
    }
    const std::time_t timestamp = _mkgmtime(&parsed);
    std::tm local{};
    if (timestamp == -1 || localtime_s(&local, &timestamp) != 0) {
        return utc;
    }
    std::ostringstream result;
    result << std::put_time(&local, "%d %b %Y, %H:%M");
    return result.str();
}

inline void showDetails(const std::string& text, const char* title = "Details") {
    Fl_Double_Window dialog(680, 400, title);
    dialog.color(UiTheme::kBackground);
    auto* output = new Fl_Multiline_Output(24, 24, 632, 304);
    output->color(UiTheme::kSurface);
    output->textcolor(UiTheme::kText);
    output->textfont(UiTheme::kUiFont);
    output->textsize(14);
    output->value(text.c_str());
    auto* close = new ActionButton(544, 344, 112, 36, "Close");
    close->callback([](Fl_Widget* widget, void*) { widget->window()->hide(); });
    dialog.end();
    dialog.resizable(output);
    dialog.size_range(480, 300);
    dialog.set_modal();
    showWithDarkWindowChrome(dialog);
    output->take_focus();
    while (dialog.shown()) {
        Fl::wait();
    }
}

}  // namespace Ui
