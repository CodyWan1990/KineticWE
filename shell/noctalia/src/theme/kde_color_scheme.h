#pragma once

#include <filesystem>
#include <string>

namespace noctalia::theme {

  struct KdeColorSchemeApplyResult {
    bool success = false;
    std::string error;
    std::string notificationError;
  };

  [[nodiscard]] bool mergeKdeColorScheme(
      const std::filesystem::path& schemePath, const std::filesystem::path& kdeGlobalsPath, std::string* error
  );

  [[nodiscard]] KdeColorSchemeApplyResult applyKdeColorScheme(const std::filesystem::path& schemePath);

  // Emits org.kde.KGlobalSettings.notifyChange(0,0) on the session bus so Qt/KDE
  // apps (including qt5ct) reload their palette. Returns an error string on
  // failure, empty on success. Safe to call after rewriting the color scheme.
  [[nodiscard]] std::string notifyKdeColorSchemeChanged();

} // namespace noctalia::theme
