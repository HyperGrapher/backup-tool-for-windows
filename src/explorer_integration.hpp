#pragma once

#include <filesystem>

void ensureExplorerIntegration(const std::filesystem::path& dataDirectory);
void registerExplorerIntegrationMachineWide(const std::filesystem::path& dataDirectory);
void refreshExplorerOverlays();
