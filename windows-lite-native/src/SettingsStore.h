#pragma once

#include "AppSettings.h"

#include <filesystem>

namespace pitlane::lite {

class SettingsStore {
public:
    static AppSettings load();
    static void save(const AppSettings& settings);
    static std::filesystem::path settings_path();
};

}  // 命名空间 pitlane::lite
