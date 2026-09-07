#pragma once

#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>

#include "ui_theme.hpp"

class ClearHistoryButton final : public Fl_Button {
public:
    ClearHistoryButton(int x, int y, int size, const char* tooltipText)
        : Fl_Button(x, y, size, size) {
        box(FL_NO_BOX);
        down_box(FL_NO_BOX);
        tooltip(tooltipText);
    }

    int handle(int event) override {
        if (event == FL_ENTER) {
            isHovered_ = true;
            redraw();
        } else if (event == FL_LEAVE) {
            isHovered_ = false;
            redraw();
        }
        return Fl_Button::handle(event);
    }

    void draw() override {
        const Fl_Color background = value() != 0 ? UiTheme::kPressedControl
                                                 : (isHovered_ ? UiTheme::kControl : UiTheme::kBackground);
        fl_color(background);
        fl_rectf(x(), y(), w(), h());

        const int centerX = x() + w() / 2;
        const int top = y() + (h() - 14) / 2;
        fl_color(active_r() ? UiTheme::kSecondaryText : UiTheme::kBorder);
        fl_line_style(FL_SOLID, 1);
        fl_line(centerX - 6, top + 3, centerX + 6, top + 3);
        fl_line(centerX - 3, top + 1, centerX + 3, top + 1);
        fl_rect(centerX - 5, top + 5, 10, 9);
        fl_line(centerX - 2, top + 7, centerX - 2, top + 12);
        fl_line(centerX + 2, top + 7, centerX + 2, top + 12);
        fl_line_style(0);
        if (Fl::focus() == this) { fl_color(UiTheme::kFocus); fl_rect(x() + 1, y() + 1, w() - 2, h() - 2); }
    }

private:
    bool isHovered_{};
};
