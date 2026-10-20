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
class Renderer;

// The Icons settings page: enumerates the icon themes installed on the system
// (XDG icon theme directories), shows a preview resolved from each theme, and
// lets the user pick the theme to apply. Persists to appearance.kwe
// ([Appearance] IconTheme) and, on Apply, writes the choice to kdeglobals
// ([Icons] Theme), the GTK settings.ini files (gtk-icon-theme-name) and
// gsettings (org.gnome.desktop.interface icon-theme) so Qt/KDE and GTK apps
// pick it up.
//
// Like the other pages, edits are STAGED: selecting a tile updates an
// in-memory pending map and nothing touches disk until the window's Apply
// button calls applyAll(). An empty selection (the "System default" tile)
// clears the override and leaves the distro/system theme untouched.
class IconsPage {
public:
  using Value = std::string; // theme name; "" = system default

  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window (same shape as AppearancePage).
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  IconsPage() = default;

  std::unique_ptr<Node> build(
      float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
  );

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }
  // Writes every staged edit to appearance.kwe and applies the effective theme
  // to kdeglobals + the GTK settings.ini files + gsettings + the qt5ct/qt6ct
  // icon_theme + KGlobalSettings notification (even with nothing staged, so
  // Apply can repair drifted targets).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_errorNotice.clear();
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  [[nodiscard]] static std::string keyPath() { return "Appearance.IconTheme"; }
  void ensureLoaded();
  [[nodiscard]] Value fileBackedValue() const;
  [[nodiscard]] Value effectiveValue() const;
  [[nodiscard]] bool overridden() const;

  void stage(const Value& value);
  void stageAndRebuild(const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node> buildTile(
      float scale, Renderer& renderer, const std::string& displayName, const std::string& previewPath, bool selected,
      std::function<void()> onClick
  );

  // Applies the given icon theme system-wide (kdeglobals + gsettings + notify).
  [[nodiscard]] bool applyTheme(std::string_view theme, std::string* error);

  KineticweKwe m_config;
  bool m_loaded = false;
  std::map<std::string, Value> m_pending;
  std::string m_errorNotice;
};