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

// The Cursors settings page: enumerates the cursor themes installed on the
// system, shows a preview decoded from each theme's representative cursor file,
// and lets the user pick the theme (and its size) to apply. Persists to
// appearance.kwe ([Appearance] CursorTheme / CursorSize) and, on Apply, writes
// the choice into the compositor's kineticwe.kwe ([Mouse] cursorTheme /
// cursorSize — what KWin actually reads), kdeglobals, gsettings/dconf, and the
// X11/Xwayland + GTK fallbacks (kcminputrc, ~/.icons/default/index.theme, GTK
// 3/4 settings.ini, xsettingsd.conf, environment.d/90-cursor.conf) so Qt, KDE,
// GTK and Xwayland apps (e.g. Steam) pick it up. Notifies KGlobalSettings
// (type 5 = CursorChanged) so the compositor reloads the cursor live.
//
// Like the other pages, edits are STAGED. An empty theme (the "System default"
// tile) clears the override; the size still applies.
class CursorsPage {
public:
  using ThemeValue = std::string; // theme name; "" = system default
  using SizeValue = int;

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

  CursorsPage() = default;

  std::unique_ptr<Node> build(
      float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
  );

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }
  // Writes every staged edit to appearance.kwe and, when a theme was selected,
  // applies it to the compositor config + kdeglobals + gsettings + notify.
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_errorNotice.clear();
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  [[nodiscard]] static std::string themeKeyPath() { return "Appearance.CursorTheme"; }
  [[nodiscard]] static std::string sizeKeyPath() { return "Appearance.CursorSize"; }
  [[nodiscard]] static std::string themeGroupKey() { return "CursorTheme"; }
  [[nodiscard]] static std::string sizeGroupKey() { return "CursorSize"; }
  void ensureLoaded();
  [[nodiscard]] ThemeValue fileBackedTheme() const;
  [[nodiscard]] SizeValue fileBackedSize() const;
  [[nodiscard]] ThemeValue effectiveTheme() const;
  [[nodiscard]] SizeValue effectiveSize() const;

  void stageTheme(const ThemeValue& value);
  void stageSize(SizeValue value);
  void stageAndRebuild(const std::string& keyPath, const std::string& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node> buildTile(
      float scale, Renderer& renderer, const std::string& displayName, const std::string& cursorFile, bool selected,
      std::function<void()> onClick
  );

  // Applies the given cursor theme + size system-wide.
  [[nodiscard]] bool applyTheme(std::string_view theme, int size, std::string* error);

  KineticweKwe m_config;
  bool m_loaded = false;
  std::map<std::string, std::string> m_pending; // "Appearance.CursorTheme"/"Appearance.CursorSize"
  std::string m_errorNotice;
};