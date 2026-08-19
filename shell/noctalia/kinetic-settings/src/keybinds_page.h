#pragma once

#include "kinetic-settings/src/custom_keybinds.h"
#include "kinetic-settings/src/keybind_dbus.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;
class SessionBus;
struct KeyChord;

// The Keybinds settings page: edits global shortcuts of every component
// registered in the embedded kglobalaccel daemon (kwin, noctalia, ...) live
// over D-Bus, and keeps a portable mirror of the overridden binds in
// ~/.config/kineticwe/keybinds.kwe (see KeybindsKwe). The page mirrors the
// Noctalia settings content layout like the Workspaces/Tiling pages: a glyph +
// title header, a component list on the left (like the KDE Shortcuts KCM) and
// the selected component's actions on the right, each with a chord recorder
// list (trash to the right of each bound chord; custom actions are deleted as
// a whole) and the Override badge on the action's title line.
//
// Edits are STAGED and only reach the daemon when the window's Apply button
// flushes everything: one setShortcutKeys call per staged action, then the
// mirror file is rewritten from the daemon's current state (only binds that
// differ from defaults; the file is deleted when none remain). Closing the
// window discards staged edits; nothing is applied live.
class KeybindsPage {
public:
  // Same shape as the other pages' filters (Advanced has no effect here yet).
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as the other pages.
  struct Callbacks {
    std::function<void()> requestRebuild;
  };

  explicit KeybindsPage(SessionBus& bus, CustomKeybinds& customKeybinds);

  // Rebuilds the page node.
  std::unique_ptr<Node> build(float scale, const Filter& filter, const Callbacks& callbacks);

  // "component.action" paths of every action on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages every action of every component back at its defaults.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

  // Applies every staged action to the daemon (setShortcutKeys per action),
  // then rewrites the mirror file with the daemon's current overridden binds
  // (deleting it when none remain). Returns false when a D-Bus apply failed —
  // staged edits are kept for retry and the mirror is left untouched.
  bool applyAll();

  // Drops staged edits and forces the next build() to re-query the daemon.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
  }

  // True while any chord recorder on the page is capturing input; the window
  // must not treat Escape as "close" (and must not rebuild) during capture.
  [[nodiscard]] bool isCapturing() const noexcept { return m_recordingCount > 0; }

private:
  struct StagedAction {
    std::string component;
    std::string action;
    std::string friendlyName;
  };

  [[nodiscard]] static std::string keyPath(std::string_view component, std::string_view action);
  [[nodiscard]] static bool splitKeyPath(std::string_view keyPath, std::string& component, std::string& action);

  void ensureLoaded();
  [[nodiscard]] const KeybindDbus::Action* findAction(std::string_view component, std::string_view action) const;
  // What the UI shows: the staged chords when present, otherwise the daemon's.
  [[nodiscard]] std::vector<KeyChord> effectiveChords(std::string_view component, std::string_view action) const;
  [[nodiscard]] bool overridden(const std::string& component, const KeybindDbus::Action& action) const;
  [[nodiscard]] bool matchesFilter(
      const KeybindDbus::Component& component, const KeybindDbus::Action& action, const Filter& filter
  ) const;

  // Stage a new chord list for an action; un-stages when it equals what the
  // daemon currently has. `callbacks` is used to request a rebuild.
  void stage(const std::string& component, const std::string& action, std::vector<KeyChord> chords);
  void stageAndRebuild(const std::string& component, const std::string& action, std::vector<KeyChord> chords, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node> buildComponentList(float scale, const Callbacks& callbacks);
  void buildActionRow(
      Flex& parent, const KeybindDbus::Component& component, const KeybindDbus::Action& action, float scale,
      const Callbacks& callbacks
  );
  void buildChordRow(
      Flex& parent, std::vector<KeyChord> chords, std::size_t index, const std::string& component,
      const std::string& action, float scale, const Callbacks& callbacks
  );
  void buildChordsColumn(
      Flex& parent, const std::string& component, const std::string& action, std::vector<KeyChord> chords,
      float scale, const Callbacks& callbacks
  );

  // Whole-action deletion (custom components only): unregisters the action in
  // the daemon and drops its definition, then reloads and rebuilds.
  void removeCustomAction(const std::string& component, const std::string& action, const Callbacks& callbacks);

  // "Add Application" / "Add Custom Command" flows.
  [[nodiscard]] std::unique_ptr<Node> buildAddMenu(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildAppPicker(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildCommandForm(float scale, const Callbacks& callbacks);

  [[nodiscard]] static bool isCustomComponent(std::string_view component);

  // Replaces chord slot `index` (appending when index == size) after
  // conflict-checking against every other action; notifies on conflict.
  void commitChord(
      const std::string& component, const std::string& action, std::vector<KeyChord> chords, std::size_t index,
      KeyChord chord, const Callbacks& callbacks
  );
  void removeChord(
      const std::string& component, const std::string& action, std::vector<KeyChord> chords, std::size_t index,
      const Callbacks& callbacks
  );

  SessionBus& m_bus;
  CustomKeybinds& m_customKeybinds;
  KeybindDbus m_dbus;
  bool m_loaded = false;
  std::vector<KeybindDbus::Component> m_components;
  // Selected component ("uniqueName"); the first component when never chosen.
  std::string m_selectedComponent;
  // Chords staged for "component.action" paths; flushed by applyAll().
  std::map<std::string, std::vector<KeyChord>> m_pending;
  int m_recordingCount = 0;

  // Add-application / add-command flows and the "+" dropdown.
  enum class AddMode { None, Apps, Command };
  bool m_addMenuOpen = false;
  AddMode m_addMode = AddMode::None;
};