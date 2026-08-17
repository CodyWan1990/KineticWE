#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Reader/writer for kineticwe.kwe — the canonical compositor settings file
// (KConfig INI syntax; the compositor reads it natively). The KineticWE
// session runs with XDG_CONFIG_HOME redirected to ~/.config/kineticwe, so
// configHome()/kineticwe.kwe resolves to ~/.config/kineticwe/kineticwe.kwe.
// No Qt: this is a minimal, self-contained INI implementation covering the
// subset the module edits while preserving every other entry.
class KineticweKwe {
public:
  // Path to the config file ($XDG_CONFIG_HOME/kineticwe.kwe).
  [[nodiscard]] static std::filesystem::path defaultPath();

  // Parse the file. Returns false when the file is missing or unreadable
  // (the module then seeds sane defaults on save).
  bool load();

  // Raw access. An empty optional means "no such key".
  [[nodiscard]] std::optional<std::string> readString(std::string_view group, std::string_view key) const;
  [[nodiscard]] bool readBool(std::string_view group, std::string_view key, bool fallback) const;
  [[nodiscard]] int readInt(std::string_view group, std::string_view key, int fallback) const;
  [[nodiscard]] double readDouble(std::string_view group, std::string_view key, double fallback) const;
  [[nodiscard]] std::vector<std::string> readStringList(std::string_view group, std::string_view key) const;

  void setString(std::string_view group, std::string_view key, std::string_view value);
  void setBool(std::string_view group, std::string_view key, bool value);
  void setInt(std::string_view group, std::string_view key, int value);
  void setDouble(std::string_view group, std::string_view key, double value);
  void setStringList(std::string_view group, std::string_view key, const std::vector<std::string>& values);

  // Whether `key` is explicitly present in `group` (regardless of value).
  [[nodiscard]] bool hasKey(std::string_view group, std::string_view key) const;
  // Removes `key` from `group`. Also drops the group when it ends up empty.
  // Returns true when something was removed.
  bool removeKey(std::string_view group, std::string_view key);

  // Names of every group present in the file (needed by pages that manage
  // dynamic sub-groups, e.g. Tiling's per-desktop overrides).
  [[nodiscard]] std::vector<std::string> groupNames() const;

  // Atomically write the file (temp file + rename). Creates parent dirs.
  bool save() const;

private:
  static std::map<std::string, std::string> emptyGroup() { return {}; }

  // Preserves insertion order (std::map<string,map> — alphabetical is fine
  // and stable for KConfig).
  std::map<std::string, std::map<std::string, std::string>> m_groups;
};