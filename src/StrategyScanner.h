#pragma once

#include <filesystem>
#include <string>
#include <vector>

std::filesystem::path StrategyFolderFromPath(const std::wstring& value);
std::vector<std::filesystem::path> FindGeneralStrategies(const std::filesystem::path& folder);

