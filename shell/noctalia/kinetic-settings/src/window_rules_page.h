#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;

// The Window Rules settings page. For now it exposes the compositor-wide
// "Window defaults" that the per-window rules override: a single "Hide title
// bars" toggle that drives [Windows] DefaultDecorationPolicy in
// ~/.config/kineticwe/kineticwe.kwe (None vs ClientPreference). Toggling it
// writes the file; the compositor's KConfig watcher hot-reloads and re-seeds
// every managed window's decoration base (rules still win via
// checkDecorationPolicy).
//
// Like the other pages, edits are STAGED: the toggle updates an in-memory
// pending value and nothing touches disk until the window's Apply button calls
// applyAll(). Closing the window discards staged edits. The per-window rule
// list/editor is a later increment and is stubbed from this page for now.
class WindowRulesPage {
public:
  // Same shape as the other pages' filters (unused by the defaults section yet).
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as the Keybinds page.
  struct Callbacks {
    std::function<void()> requestRebuild;
  };

  explicit WindowRulesPage(class SessionBus& bus);

  std::unique_ptr<Node> build(float scale, const Filter& filter, const Callbacks& callbacks);

  // "Reset Page" scope key path (the single window-defaults toggle).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths();
  // Resets the defaults section back to the app default (title bars shown).
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return m_pending.has_value(); }

  // Writes the staged window defaults to kineticwe.kwe atomically. Returns
  // false on failure (staged edits are kept for retry).
  bool applyAll();

  // Drops staged edits and forces the next build() to re-read the file.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.reset();
    m_errorNotice.clear();
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  // The toggle drives [Windows] DefaultDecorationPolicy: hidden -> None (0),
  // shown -> ClientPreference (1, the KWin default).
  static constexpr std::string_view kGroup = "Windows";
  static constexpr std::string_view kKey = "DefaultDecorationPolicy";
  static constexpr std::string_view kKeyPath = "Windows/DefaultDecorationPolicy";

  void ensureLoaded();
  // Whether the file currently hides title bars (value == None/0). Absent key
  // reads as ClientPreference (1) -> not hidden.
  [[nodiscard]] bool hideTitleBarsOnDisk() const;
  // What the UI shows: the staged value when present, else the file's.
  [[nodiscard]] bool effectiveHideTitleBars() const;
  // True when the effective value differs from the app default (not hidden).
  [[nodiscard]] bool overridden() const;
  // Stage a new toggle state; un-stages when it matches what's on disk.
  void stageHideTitleBars(bool hide, const Callbacks& callbacks);

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Staged "hide title bars" value; nullopt = nothing staged.
  std::optional<bool> m_pending;
  // Message shown when the last Apply failed (cleared on next Apply/reload).
  std::string m_errorNotice;
};
