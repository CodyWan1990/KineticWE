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

  // Path to the window-rule book (~/.config/kineticwe/rules.kwe).
  [[nodiscard]] static std::filesystem::path rulesDefaultPath();

  // Overrides the file this reader/writer targets. Default is kineticwe.kwe;
  // the Appearance page uses this for its own portable appearance.kwe. Must be
  // called before load()/save().
  void setPath(std::filesystem::path path) { m_path = std::move(path); }

  // The resolved target path (the override when set, else the default).
  [[nodiscard]] std::filesystem::path path() const { return m_path.value_or(defaultPath()); }

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

  // Drops every group so the file can be rebuilt from scratch (used by
  // window-rules, where the whole file is one owning list). Loaded content,
  // if any, is discarded; call save() afterwards to write the new state.
  void clear() { m_groups.clear(); }

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
  // Optional custom target path (defaultPath() when unset).
  std::optional<std::filesystem::path> m_path;
};