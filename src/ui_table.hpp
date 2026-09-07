#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <FL/Fl_Table_Row.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/fl_draw.H>

#include "ui_theme.hpp"

struct TableRow {
    std::string key;
    std::vector<std::string> cells;
    std::string details;
};

class DataTable final : public Fl_Table_Row {
public:
    DataTable(int x, int y, int width, int height, std::vector<std::string> headings,
              std::vector<int> weights)
        : Fl_Table_Row(x, y, width, height), headings_(std::move(headings)), weights_(std::move(weights)) {
        box(FL_FLAT_BOX);
        color(UiTheme::kSurface);
        table_box(FL_FLAT_BOX);
        selection_color(UiTheme::kSelection);
        col_header_color(UiTheme::kBackground);
        row_header_color(UiTheme::kBackground);
        type(SELECT_MULTI);
        cols(static_cast<int>(headings_.size()));
        col_header(1);
        col_header_height(32);
        row_header(0);
        row_height_all(42);
        when(FL_WHEN_CHANGED);
        end();
        styleScrollbars();
        fitColumns();
    }

    void setRows(std::vector<TableRow> entries) {
        const auto selection = selectedKeys();
        const int previousPosition = row_position();
        entries_ = std::move(entries);
        rows(static_cast<int>(entries_.size()));
        focusedRow_ = std::clamp(focusedRow_, 0, std::max(0, rows() - 1));
        row_height_all(42);
        select_all_rows(0);
        for (int index = 0; index < rows(); ++index) {
            if (std::ranges::find(selection, entries_[index].key) != selection.end()) {
                select_row(index, 1);
            }
        }
        if (rows() > 0) {
            row_position(std::min(previousPosition, rows() - 1));
        }
        redraw();
    }

    [[nodiscard]] std::vector<std::string> selectedKeys() {
        std::vector<std::string> keys;
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (row_selected(index)) {
                keys.push_back(entries_[index].key);
            }
        }
        return keys;
    }

    [[nodiscard]] std::string selectedDetails() {
        std::string details;
        for (int index = 0; index < static_cast<int>(entries_.size()); ++index) {
            if (row_selected(index)) {
                if (!details.empty()) {
                    details += "\n\n";
                }
                details += entries_[index].details;
            }
        }
        return details;
    }

    void emptyMessage(std::string message) {
        emptyMessage_ = std::move(message);
        redraw();
    }

    int handle(int event) override {
        // Table headers are presentation only. Letting Fl_Table_Row treat a header
        // click as a row gesture can leave a partial redraw on FLTK 1.4.
        if (event == FL_PUSH || event == FL_RELEASE || event == FL_DRAG) {
            int row = 0;
            int column = 0;
            ResizeFlag resizeFlag{};
            if (cursor2rowcol(row, column, resizeFlag) == CONTEXT_COL_HEADER) {
                return 1;
            }
        }
        if (event == FL_FOCUS || event == FL_UNFOCUS) {
            redraw();
            return 1;
        }
        if (event == FL_KEYDOWN && rows() > 0) {
            const int key = Fl::event_key();
            int next = focusedRow_;
            if (key == FL_Down) { next = std::min(rows() - 1, focusedRow_ + 1); }
            else if (key == FL_Up) { next = std::max(0, focusedRow_ - 1); }
            else if (key == FL_Home) { next = 0; }
            else if (key == FL_End) { next = rows() - 1; }
            else if (key == 'a' && Fl::event_state(FL_CTRL)) {
                select_all_rows(1);
                Fl_Widget::do_callback();
                return 1;
            } else if (key == ' ') {
                select_row(focusedRow_, 2);
                Fl_Widget::do_callback();
                return 1;
            } else {
                return Fl_Table_Row::handle(event);
            }
            focusedRow_ = next;
            if (!Fl::event_state(FL_SHIFT)) { select_all_rows(0); }
            select_row(focusedRow_, 1);
            row_position(focusedRow_);
            redraw();
            Fl_Widget::do_callback();
            return 1;
        }
        const int handled = Fl_Table_Row::handle(event);
        if (event == FL_PUSH && callback_row() >= 0 && callback_row() < rows()) {
            focusedRow_ = callback_row();
            take_focus();
            redraw();
        }
        return handled;
    }

    void resize(int x, int y, int width, int height) override {
        Fl_Table_Row::resize(x, y, width, height);
        styleScrollbars();
        fitColumns();
    }

protected:
    void draw() override {
        Fl_Table_Row::draw();
        if (entries_.empty()) {
            fl_color(UiTheme::kSecondaryText);
            fl_font(UiTheme::kUiFont, 14);
            fl_draw(emptyMessage_.c_str(), x() + 24, y() + 56, w() - 48, h() - 72,
                    FL_ALIGN_TOP | FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        }
    }

    void draw_cell(TableContext context, int row, int column, int x, int y, int width, int height) override {
        if (context != CONTEXT_COL_HEADER && context != CONTEXT_CELL) {
            return;
        }
        fl_push_clip(x, y, width, height);
        const bool isHeader = context == CONTEXT_COL_HEADER;
        fl_color(isHeader ? UiTheme::kBackground : (row_selected(row) ? UiTheme::kSelection : UiTheme::kSurface));
        fl_rectf(x, y, width, height);
        fl_color(UiTheme::kBorder);
        fl_line(x, y + height - 1, x + width, y + height - 1);
        const std::string& text = isHeader ? headings_.at(column) : entries_.at(row).cells.at(column);
        if (isHeader) {
            fl_font(UiTheme::kUiFontSemibold, 12);
            fl_color(UiTheme::kSecondaryText);
            fl_draw(text.c_str(), x + 12, y + 4, width - 24, height - 8,
                    FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        } else if (column == 0 && text.find('\n') != std::string::npos) {
            const std::size_t separator = text.find('\n');
            const std::string title = text.substr(0, separator);
            const std::string path = text.substr(separator + 1);
            fl_font(UiTheme::kUiFontSemibold, 13);
            fl_color(UiTheme::kText);
            fl_draw(title.c_str(), x + 12, y + 3, width - 24, 17,
                    FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
            fl_font(UiTheme::kUiFont, 11);
            fl_color(UiTheme::kSecondaryText);
            fl_draw(path.c_str(), x + 12, y + 21, width - 24, 16,
                    FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        } else {
            fl_font(UiTheme::kUiFont, 12);
            fl_color(UiTheme::kText);
            if (text == "Current" || text == "Connected") {
                fl_color(UiTheme::kSafe);
            } else if (text == "Action needed" || text == "Unavailable") {
                fl_color(UiTheme::kError);
            } else if (text == "Waiting" || text == "Disconnected" || text == "Never backed up") {
                fl_color(UiTheme::kPending);
            }
            fl_draw(text.c_str(), x + 12, y + 4, width - 24, height - 8,
                    FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_CLIP);
        }
        if (!isHeader && row == focusedRow_ && Fl::focus() == this) {
            fl_color(UiTheme::kFocus);
            fl_line(x, y + height - 2, x + width, y + height - 2);
        }
        fl_pop_clip();
    }

private:
    std::vector<std::string> headings_;
    std::vector<int> weights_;
    std::vector<TableRow> entries_;
    std::string emptyMessage_{"Nothing here yet."};
    int focusedRow_{};

    void styleScrollbars() {
        for (Fl_Scrollbar* scrollbar : {vscrollbar, hscrollbar}) {
            if (scrollbar == nullptr) {
                continue;
            }
            scrollbar->color(UiTheme::kBackground);
            scrollbar->selection_color(UiTheme::kPrimary);
            scrollbar->slider(FL_FLAT_BOX);
            scrollbar->slider_size(0.25f);
        }
    }

    void fitColumns() {
        int total = 0;
        for (const int weight : weights_) {
            total += weight;
        }
        const int available = std::max(100, w() - 20);
        for (int column = 0; column < static_cast<int>(weights_.size()); ++column) {
            col_width(column, available * weights_[column] / total);
        }
    }
};
