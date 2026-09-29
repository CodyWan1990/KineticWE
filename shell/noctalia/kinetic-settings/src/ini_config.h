#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace kinetic_settings {

  // Minimal writers for the hand-written config files the settings pages keep in
  // sync (qt5ct/qt6ct, kcminputrc, GTK settings.ini, xsettingsd, environment.d).
  // They exist because these files carry user comments and unrelated keys that
  // must survive an update, and because KConfig dependencies are not available
  // for every one of them.

  // Sets `key` in the `[section]` group of an INI/KConfig file, preserving every
  // other line. An empty `value` removes the key. When `createIfMissing` is true
  // a missing file or section is created; otherwise a missing file is left
  // untouched (the caller may seed it elsewhere). An unchanged value means no
  // write. Returns false only on an I/O error.
  [[nodiscard]] bool setKey(
      const std::filesystem::path& path,
      std::string_view section,
      std::string_view key,
      std::string_view value,
      bool createIfMissing,
      std::string* error
  );

  // Sets a top-level `key<separator><value>` line (the value optionally
  // double-quoted) in a flat config file such as xsettingsd.conf (`Key "value"`)
  // or environment.d (`KEY=value`), preserving every other line. An empty
  // `value` removes the line. When `createIfMissing` is true a missing file is
  // created. Returns false only on an I/O error.
  [[nodiscard]] bool setFlatKey(
      const std::filesystem::path& path,
      std::string_view key,
      std::string_view value,
      std::string_view separator,
      bool quote,
      bool createIfMissing,
      std::string* error
  );

} // namespace kinetic_settings
