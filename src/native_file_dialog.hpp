#pragma once

#define NOMINMAX
#include <windows.h>

#include <filesystem>
#include <vector>

[[nodiscard]] std::vector<std::filesystem::path> selectFiles(HWND owner);
[[nodiscard]] std::vector<std::filesystem::path> selectFolders(HWND owner);

