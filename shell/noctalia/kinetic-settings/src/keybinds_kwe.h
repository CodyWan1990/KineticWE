#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class SessionBus;
struct KeyChord;

// The portable keybinds mirror: ~/.config/kineticwe/keybinds.kwe. Unlike the
// daemon's own kglobalshortcutsrc (which holds every action in a
// keys;defaults;friendlyName format and is tied to the machine), this file
// only records the user's overridden binds, in a plain KDE scheme style:
//
//   [kwin][Global Shortcuts]
//   Window Close=Meta+Q
//   Walk Through Windows=Meta+Tab; Ctrl+Alt+Tab
//
// It is re-applied to the daemon at session start and on demand
// (`noctalia-kwe msg keybinds-import`), and rewritten by Kinetic Settings on
// Apply. Because the daemon's loader skips entries that are not in its own
// 3-part format, the file is never copied over kglobalshortcutsrc — D-Bus
// replay is the only import path.
namespace KeybindsKwe {
  struct Entry {
    std::string component;
    std::string action;
    std::vector<KeyChord> chords;
  };

  // The mirror path: $KWE_CONFIG_HOME/keybinds.kwe or
  // ~/.config/kineticwe/keybinds.kwe (same resolution as kineticwe.kwe).
  [[nodiscard]] std::filesystem::path defaultPath();

  // Parses the mirror into `out`. Missing/unreadable file or empty groups
  // yield false with `out` untouched; entries with unparseable chords are
  // skipped. Unknown actions are not the parser's concern (the caller applies
  // whatever the daemon can resolve).
  [[nodiscard]] bool load(const std::filesystem::path& path, std::vector<Entry>& out);

  // Writes the mirror atomically (temp + rename). A component with no entries
  // is omitted; an empty `entries` deletes the file. Returns false on failure.
  [[nodiscard]] bool save(const std::filesystem::path& path, const std::vector<Entry>& entries);

  // Removes every entry for the given component.action from the mirror.
  void removeEntries(const std::filesystem::path& path, const std::string& component, const std::string& action);

  // Friendly names that must be used verbatim when replaying an entry. The
  // daemon's findAction() matches all four parts of an action id
  // ([component, action, component friendlyName, action friendlyName]); for
  // user-added Applications/Custom Commands actions the daemon may report
  // different friendly names than the definitions used at doRegister time,
  // which makes the replay silently fail. The caller (kinetic-settings)
  // populates this from the persisted definitions so replay uses exactly the
  // names the actions were registered with.
  struct FriendlyNameOverride {
    std::string componentFriendlyName;
    std::string actionFriendlyName;
  };

  // Replays the mirror into the daemon and returns how many entries were
  // applied. Stale/unknown actions and entries whose chords the daemon rejects
  // are skipped with a warning; the file itself is left untouched.
  [[nodiscard]] int applyToDaemon(
      SessionBus& bus,
      const std::unordered_map<std::string, FriendlyNameOverride>& friendlyNameOverrides = {}
  );
} // namespace KeybindsKwe