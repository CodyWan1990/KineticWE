#pragma once

#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;
class DesktopEntry;

// The Default Applications settings page: lets the user pick the default
// application for common file types (web browser, image viewer, music player,
// etc.) through a "System" option that clears the override and reverts to the
// system/XDG default.
//
// Persists to the standard XDG file ~/.config/mimeapps.list ([Default
// Applications] group) using the same KineticweKwe reader/writer as the other
// pages (pointed at mimeapps.list instead of kineticwe.kwe). Each row maps to
// one or more MIME type keys; applying a choice writes it to EVERY key the row
// owns (each key resolves independently — writing only the first left
// consumers of the other keys, e.g. x-scheme-handler/https, falling back past
// the user's choice), and the effective value for a row is the first present
// key's default application. "System" stages a removal of every key the row
// owns so the freedesktop default applies. The terminal-emulator row is the
// exception: terminals have no MIME type, so it persists to kdeglobals
// ([General] TerminalApplication — the location KIO and this shell resolve
// the terminal from) through kwriteconfig6.
//
// App options come from the shell's desktop-entry registry (desktopEntries()),
// filtered to entries that declare support for the row's MIME types (or, for
// the terminal row, entries that ARE terminal emulators — the TerminalEmulator
// desktop-file category or a well-known terminal program; never the
// Terminal=true key, which means "runs inside a terminal", i.e. console
// tools like htop, not terminals).
//
// Like the other pages, edits are STAGED: changing a dropdown only updates an
// in-memory pending map and nothing touches mimeapps.list until the window's
// Apply button calls applyAll(), which writes every staged change in one save.
// Closing the window discards staged edits.
class DefaultAppsPage {
public:
  using Value = std::string; // desktop-entry id, or "" for "System" (no default)

  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  DefaultAppsPage() = default;

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }
  // Writes every staged edit (mimeapps.list rows plus the terminal row's
  // kdeglobals key) and refreshes the desktop caches. Returns false on failure
  // (the staged edits are kept for retry).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_errorNotice.clear();
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  // One configurable default. A row owns one or more MIME keys; the effective
  // value is the first key that has a default. Selecting "System" removes every
  // key the row owns.
  struct Field {
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup; // group header shown on the page
    bool advanced = false;
    // Semicolon-separated MIME keys this row manages. May be empty for rows
    // filtered by another predicate (e.g. the terminal emulator row).
    std::string_view mimeKeys;
    // For rows without MIME keys (terminal): match terminal emulators (the
    // TerminalEmulator desktop-file category or a well-known terminal program;
    // the Terminal=true key means "runs inside a terminal" — console tools —
    // and must NOT be used). Such rows persist to kdeglobals
    // [General] TerminalApplication instead of mimeapps.list (see
    // writeTerminalApplication()).
    bool matchByTerminal = false;
  };

  static const std::vector<Field>& fields();
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);

  void ensureLoaded();
  // Value read from mimeapps.list (ignoring staged edits); "" = no override.
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: the staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;

  // Candidates offered for a row: entries from desktopEntries() that support at
  // least one of the row's MIME keys (or match the row's terminal predicate),
  // sorted by name.
  [[nodiscard]] std::vector<const DesktopEntry*> candidateApps(const Field& field) const;

  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  // Writes (or clears, when empty) the terminal row's choice to kdeglobals
  // [General] TerminalApplication via terminal_launch::writeConfiguredTerminal.
  // Returns false (with the error notice set) on failure.
  [[nodiscard]] bool writeTerminalApplication(const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node>
  buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  KineticweKwe m_config;
  bool m_loaded = false;
  std::map<std::string, Value> m_pending;
  std::string m_errorNotice;
};
