#pragma once

#include <FL/Fl_Button.H>
#include <FL/fl_draw.H>

#include "ui_theme.hpp"

// Keep FLTK's activation and keyboard handling; only the visual states differ.
class ActionButton final : public Fl_Button {
public:
    ActionButton(int x, int y, int width, int height, const char* label = nullptr)
        : Fl_Button(x, y, width, height, label) {
        box(FL_FLAT_BOX);
        down_box(FL_FLAT_BOX);
        color(UiTheme::kControl);
        labelcolor(UiTheme::kText);
        labelfont(UiTheme::kUiFontSemibold);
        labelsize(13);
    }

    int handle(int event) override {
        if (event == FL_ENTER || event == FL_LEAVE) {
            isHovered_ = event == FL_ENTER;
            redraw();
        }
        if (event == FL_FOCUS || event == FL_UNFOCUS) {
            redraw();
        }
        return Fl_Button::handle(event);
    }

    void draw() override {
        const bool isPrimary = color() == UiTheme::kPrimary;
        Fl_Color fill = color();
        if (!active_r()) {
            fill = UiTheme::kControl;
        } else if (value()) {
            fill = isPrimary ? UiTheme::kPrimaryPressed : UiTheme::kPressedControl;
        } else if (isHovered_) {
            fill = isPrimary ? UiTheme::kPrimaryHover : UiTheme::kPressedControl;
        }
        fl_color(fill);
        fl_rectf(x(), y(), w(), h());
        fl_color(active_r() ? (isPrimary ? UiTheme::kPrimaryText : labelcolor()) : UiTheme::kDisabledText);
        fl_font(labelfont(), labelsize());
        fl_draw(label() ? label() : "", x() + 8, y(), w() - 16, h(), FL_ALIGN_CENTER | FL_ALIGN_CLIP);
        if (Fl::focus() == this && active_r()) {
            fl_color(UiTheme::kFocus);
            fl_line_style(FL_SOLID, 2);
            fl_rect(x() + 2, y() + 2, w() - 4, h() - 4);
            fl_line_style(0);
        }
    }

private:
    bool isHovered_{};
};
