#include "projects_scanner.hpp"

#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>

#include "ignore_rules.hpp"

namespace {

constexpr wchar_t kMarkerName[] = L".backup-watch";
constexpr wchar_t kIgnoreFileName[] = L".backup-ignore";

[[nodiscard]] std::string trimWhitespace(std::string text) {
    const auto isWhitespace = [](unsigned char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    text.erase(text.begin(), std::find_if_not(text.begin(), text.end(), isWhitespace));
    text.erase(std::find_if_not(text.rbegin(), text.rend(), isWhitespace).base(), text.end());
    return text;
}

[[nodiscard]] bool isHexDigit(char character) {
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

[[nodiscard]] bool isUuid(std::string_view text) {
    if (text.size() != 36) {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        const bool shouldBeHyphen = index == 8 || index == 13 || index == 18 || index == 23;
        if (shouldBeHyphen ? text[index] != '-' : !isHexDigit(text[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return text;
}

[[nodiscard]] std::string loadOrCreateMarkerId(const std::filesystem::path& markerPath) {
    std::ifstream input{markerPath, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to read .backup-watch.");
    }
    std::string markerText{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    input.close();
    markerText = trimWhitespace(std::move(markerText));
    if (!markerText.empty()) {
        if (!isUuid(markerText)) {
            throw std::runtime_error(".backup-watch must be empty or contain one UUID.");
        }
        return lowercase(std::move(markerText));
    }

    const std::string id = generateUuid();
    std::filesystem::path temporaryPath = markerPath;
    temporaryPath += L".tmp";
    {
        std::ofstream output{temporaryPath, std::ios::binary | std::ios::trunc};
        if (!output) {
            throw std::runtime_error("Unable to create the temporary .backup-watch file.");
        }
        output << id << '\n';
        output.flush();
        if (!output) {
            throw std::runtime_error("Unable to write the .backup-watch UUID.");
        }
    }
    if (MoveFileExW(temporaryPath.c_str(), markerPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        FALSE) {
        const DWORD error = GetLastError();
        std::error_code ignoredError;
        std::filesystem::remove(temporaryPath, ignoredError);
        throw std::runtime_error("Unable to publish the .backup-watch UUID. Windows error " +
                                 std::to_string(error) + '.');
    }
    return id;
}

[[nodiscard]] bool equalsIgnoreCase(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool isReparsePoint(const std::filesystem::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        throw std::runtime_error("Unable to inspect filesystem attributes.");
    }
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

[[nodiscard]] bool containsGitMarker(const std::filesystem::path& directory) {
    std::error_code error;
    const bool exists = std::filesystem::exists(directory / L".git", error);
    if (error) {
        throw std::runtime_error("Unable to inspect a potential Git repository.");
    }
    return exists;
}

[[nodiscard]] bool isToolMetadata(const std::filesystem::path& relativePath) {
    if (relativePath.has_parent_path()) {
        return false;
    }
    const std::wstring filename = relativePath.filename().wstring();
    return equalsIgnoreCase(filename, kMarkerName) || equalsIgnoreCase(filename, kIgnoreFileName);
}

[[nodiscard]] bool shouldPruneDirectory(const std::filesystem::path& path,
                                        const std::filesystem::path& relativePath,
                                        const IgnoreRules& ignoreRules) {
    if (isReparsePoint(path)) {
        return true;
    }
    const std::wstring directoryName = path.filename().wstring();
    if (equalsIgnoreCase(directoryName, L"build") || equalsIgnoreCase(directoryName, L"node_modules")) {
        return true;
    }
    if (containsGitMarker(path)) {
        return true;
    }
    return ignoreRules.isIgnored(relativePath, true);
}

}  // namespace

bool ProjectPreflight::requiresSizeApproval(bool isPermanentlyApproved) const noexcept {
    return !isPermanentlyApproved && (!largeFiles.empty() || doesProjectExceedThreshold);
}

ProjectsDiscovery discoverProjects(const ProjectsRoot& root) {
    if (root.id.empty() || root.path.empty()) {
        throw std::invalid_argument("Projects Root must have an ID and path.");
    }
    if (!std::filesystem::is_directory(root.path)) {
        throw std::runtime_error("Projects Root is unavailable or is not a directory.");
    }

    ProjectsDiscovery discovery;
    std::unordered_set<std::string> sourceIds;
    std::error_code iterationError;
    for (std::filesystem::directory_iterator iterator{root.path, iterationError}, end; iterator != end;
         iterator.increment(iterationError)) {
        if (iterationError) {
            throw std::runtime_error("Unable to enumerate Projects Root.");
        }
        const std::filesystem::directory_entry& entry = *iterator;
        std::error_code typeError;
        if (!entry.is_directory(typeError) || typeError || isReparsePoint(entry.path())) {
            continue;
        }
        const std::filesystem::path markerPath = entry.path() / kMarkerName;
        std::error_code markerError;
        const bool markerExists = std::filesystem::exists(markerPath, markerError);
        if (markerError) {
            discovery.problems.push_back({entry.path(), "Unable to inspect .backup-watch."});
            continue;
        }
        if (!markerExists) {
            continue;
        }
        if (!std::filesystem::is_regular_file(markerPath, markerError) || markerError) {
            discovery.problems.push_back({entry.path(), ".backup-watch is not a readable regular file."});
            continue;
        }

        try {
            std::string id = loadOrCreateMarkerId(markerPath);
            if (!sourceIds.insert(id).second) {
                throw std::runtime_error("Duplicate .backup-watch UUID within this Projects Root.");
            }
            discovery.sources.push_back(ProjectsSource{std::move(id), entry.path()});
        } catch (const std::exception& error) {
            discovery.problems.push_back({entry.path(), error.what()});
        }
    }
    if (iterationError) {
        throw std::runtime_error("Unable to enumerate Projects Root.");
    }
    return discovery;
}

ConfiguredProjectsDiscovery discoverConfiguredProjects(const std::vector<ProjectsRoot>& roots) {
    ConfiguredProjectsDiscovery combined;
    std::unordered_set<std::string> sourceIds;
    for (const ProjectsRoot& root : roots) {
        try {
            ProjectsDiscovery discovery = discoverProjects(root);
            combined.problems.insert(combined.problems.end(), std::make_move_iterator(discovery.problems.begin()),
                                     std::make_move_iterator(discovery.problems.end()));
            for (ProjectsSource& source : discovery.sources) {
                if (!sourceIds.insert(source.id).second) {
                    combined.problems.push_back(
                        {source.path, "Duplicate .backup-watch UUID across configured Projects Roots."});
                    continue;
                }
                combined.sources.push_back(ConfiguredProjectsSource{root.id, std::move(source)});
            }
        } catch (const std::exception& error) {
            combined.problems.push_back({root.path, error.what()});
        }
    }
    return combined;
}

bool isProjectsRootDiscoveryChange(const std::filesystem::path& relativePath) {
    auto component = relativePath.begin();
    if (component == relativePath.end()) {
        return false;
    }

    ++component;
    if (component == relativePath.end()) {
        return true;
    }

    std::wstring childName = component->native();
    std::ranges::transform(childName, childName.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    ++component;
    if (component != relativePath.end()) {
        return false;
    }
    return childName == L".backup-watch" || childName == L".git";
}

ProjectContents collectProjectContents(const ProjectsSource& source) {
    if (source.id.empty() || source.path.empty()) {
        throw std::invalid_argument("Projects Source must have an ID and path.");
    }
    if (!std::filesystem::is_directory(source.path)) {
        throw std::runtime_error("Projects Source is unavailable or is not a directory.");
    }

    ProjectContents contents;
    contents.source = source;
    contents.isGitRepository = containsGitMarker(source.path);
    if (contents.isGitRepository) {
        return contents;
    }

    const IgnoreRules ignoreRules = IgnoreRules::load(source.path / kIgnoreFileName);
    std::error_code iterationError;
    for (std::filesystem::recursive_directory_iterator iterator{
             source.path, std::filesystem::directory_options::none, iterationError},
         end;
         iterator != end; iterator.increment(iterationError)) {
        if (iterationError) {
            throw std::runtime_error("Unable to enumerate Projects Source completely.");
        }

        const std::filesystem::directory_entry& entry = *iterator;
        const std::filesystem::path relativePath = entry.path().lexically_relative(source.path);
        std::error_code typeError;
        if (entry.is_directory(typeError)) {
            if (typeError) {
                throw std::runtime_error("Unable to inspect a Projects Source directory.");
            }
            if (shouldPruneDirectory(entry.path(), relativePath, ignoreRules)) {
                iterator.disable_recursion_pending();
            }
            continue;
        }
        if (typeError) {
            throw std::runtime_error("Unable to inspect a Projects Source item.");
        }
        if (isReparsePoint(entry.path()) || !entry.is_regular_file(typeError) || typeError ||
            isToolMetadata(relativePath) || ignoreRules.isIgnored(relativePath, false)) {
            continue;
        }

        const std::uintmax_t rawSize = entry.file_size(typeError);
        if (typeError) {
            throw std::runtime_error("Unable to read the size of an Eligible Item.");
        }
        contents.files.push_back(EligibleProjectFile{relativePath, static_cast<std::uint64_t>(rawSize)});
    }
    if (iterationError) {
        throw std::runtime_error("Unable to enumerate Projects Source completely.");
    }
    return contents;
}

ProjectPreflight scanProject(const ProjectsSource& source, const BackupSettings& settings) {
    if (source.id.empty() || source.path.empty()) {
        throw std::invalid_argument("Projects Source must have an ID and path.");
    }
    if (settings.largeFileThresholdBytes == 0 || settings.projectSizeThresholdBytes == 0) {
        throw std::invalid_argument("Project size thresholds must be positive.");
    }
    const ProjectContents contents = collectProjectContents(source);
    ProjectPreflight preflight;
    preflight.source = source;
    preflight.isGitRepository = contents.isGitRepository;
    if (preflight.isGitRepository) {
        return preflight;
    }
    for (const EligibleProjectFile& file : contents.files) {
        const std::uint64_t size = file.sizeBytes;
        if (std::numeric_limits<std::uint64_t>::max() - preflight.eligibleSizeBytes < size) {
            throw std::overflow_error("Projects Source size exceeds the supported range.");
        }
        preflight.eligibleSizeBytes += size;
        ++preflight.eligibleFileCount;
        if (size > settings.largeFileThresholdBytes) {
            preflight.largeFiles.push_back({file.relativePath, size});
        }
    }
    preflight.doesProjectExceedThreshold = preflight.eligibleSizeBytes > settings.projectSizeThresholdBytes;
    return preflight;
}
