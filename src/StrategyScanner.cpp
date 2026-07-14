#include "StrategyScanner.h"

#include <algorithm>
#include <string>

namespace {

std::wstring LowerFileName(const std::filesystem::path& path) {
    std::wstring name = path.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return name;
}

bool IsGeneralStrategy(const std::filesystem::path& path) {
    std::wstring ext = path.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    if (ext != L".bat" && ext != L".cmd") {
        return false;
    }

    return LowerFileName(path).rfind(L"general", 0) == 0;
}

} // namespace

std::filesystem::path StrategyFolderFromPath(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    std::filesystem::path path(value);
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        return path;
    }
    return path.parent_path();
}

std::vector<std::filesystem::path> FindGeneralStrategies(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> result;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (ec || !entry.is_regular_file(ec)) {
            continue;
        }
        if (IsGeneralStrategy(entry.path())) {
            result.push_back(entry.path());
        }
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return _wcsicmp(left.filename().c_str(), right.filename().c_str()) < 0;
    });
    return result;
}
