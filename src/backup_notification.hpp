#pragma once

#include <memory>
#include <string_view>

class Fl_Box;
class Fl_Double_Window;

class BackupNotification final {
public:
    BackupNotification() = default;
    ~BackupNotification();

    BackupNotification(const BackupNotification&) = delete;
    BackupNotification& operator=(const BackupNotification&) = delete;

    void show(std::string_view message);
    void hide();

private:
    void buildWindow();

    std::unique_ptr<Fl_Double_Window> window_;
    Fl_Box* message_{};
};
