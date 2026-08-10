#include "native_file_dialog.hpp"

#include <shobjidl.h>
#include <wrl/client.h>

#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {

using Microsoft::WRL::ComPtr;

[[noreturn]] void throwDialogError(std::string_view action, HRESULT result) {
    std::ostringstream message;
    message << action << " failed with error 0x" << std::hex << static_cast<unsigned long>(result) << '.';
    throw std::runtime_error(message.str());
}

[[nodiscard]] std::vector<std::filesystem::path> selectPaths(HWND owner, bool areFolders) {
    ComPtr<IFileOpenDialog> dialog;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(result)) {
        throwDialogError("Opening the selection window", result);
    }

    FILEOPENDIALOGOPTIONS options{};
    result = dialog->GetOptions(&options);
    if (FAILED(result)) {
        throwDialogError("Reading selection options", result);
    }
    options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_ALLOWMULTISELECT;
    options |= areFolders ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
    result = dialog->SetOptions(options);
    if (FAILED(result)) {
        throwDialogError("Setting selection options", result);
    }
    dialog->SetTitle(areFolders ? L"Add folders to BackItUpTool" : L"Add files to BackItUpTool");

    result = dialog->Show(owner);
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        return {};
    }
    if (FAILED(result)) {
        throwDialogError("Selecting backup sources", result);
    }

    ComPtr<IShellItemArray> selectedItems;
    result = dialog->GetResults(&selectedItems);
    if (FAILED(result)) {
        throwDialogError("Reading selected sources", result);
    }

    DWORD itemCount{};
    result = selectedItems->GetCount(&itemCount);
    if (FAILED(result)) {
        throwDialogError("Counting selected sources", result);
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(itemCount);
    for (DWORD index = 0; index < itemCount; ++index) {
        ComPtr<IShellItem> item;
        result = selectedItems->GetItemAt(index, &item);
        if (FAILED(result)) {
            throwDialogError("Reading a selected source", result);
        }

        PWSTR rawPath = nullptr;
        result = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
        if (FAILED(result)) {
            throwDialogError("Reading a selected source path", result);
        }
        paths.emplace_back(rawPath);
        CoTaskMemFree(rawPath);
    }
    return paths;
}

}  // namespace

std::vector<std::filesystem::path> selectFiles(HWND owner) {
    return selectPaths(owner, false);
}

std::vector<std::filesystem::path> selectFolders(HWND owner) {
    return selectPaths(owner, true);
}

