#pragma once

#include <string>
#include <vector>

namespace font_discovery {

  // Installed font families, deduplicated and sorted case-insensitively.
  // Discovered via `fc-list : family`, cached until refreshFamilies().
  [[nodiscard]] const std::vector<std::string>& families();

  // Drops the cached list; the next families() call reruns fc-list.
  void refreshFamilies();

  // The per-user font installation directory ($XDG_DATA_HOME/fonts,
  // default ~/.local/share/fonts).
  [[nodiscard]] std::string userFontsPath();

  // Ensures the user font directory exists, then opens it in the default file
  // manager. Returns true when a launch was attempted.
  bool openUserFontsDirectory();

} // namespace font_discovery
