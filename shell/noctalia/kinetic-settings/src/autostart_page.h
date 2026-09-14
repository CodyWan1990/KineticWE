#pragma once

#include <functional>
#include <memory>
#include <string>

class Node;

// The Autostart settings page (System category): lists the .desktop files in
// ~/.config/autostart and lets the user add an application (picked from the
// shell's desktop-entry registry) or a custom command, and remove any entry
// with the "x" on its row. The add flow mirrors the Keybinds page (a "+" that
// opens an Application / Custom Command menu, then the shared search picker or
// the command input).
//
// Edits are written to disk immediately; the page has no staged state and no
// Apply. The shell's autostart supervisor launches the entries once at login
// (see autostart::launchAll()).
class AutostartPage {
public:
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  struct Callbacks {
    std::function<void()> requestRebuild;
  };

  AutostartPage() = default;

  // Rebuilds the page node. Returns nullptr while a global search is active and
  // nothing matches (the window drops empty pages from the merged results).
  [[nodiscard]] std::unique_ptr<Node> build(float scale, const Filter& filter, const Callbacks& callbacks);

  // Clears transient UI state (open add menu / picker) when the window opens.
  void reloadFromDisk() noexcept {
    m_addMenuOpen = false;
    m_addMode = AddMode::None;
  }

  // Edits are written immediately, so the window's generic Apply/Reset plumbing
  // never has anything to flush for this page.
  [[nodiscard]] bool hasPending() const noexcept { return false; }
  bool applyAll() { return true; }

private:
  enum class AddMode { None, Apps, Command };

  [[nodiscard]] std::unique_ptr<Node> buildAddMenu(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildAppPicker(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildCommandForm(float scale, const Callbacks& callbacks);

  bool m_addMenuOpen = false;
  AddMode m_addMode = AddMode::None;
};
