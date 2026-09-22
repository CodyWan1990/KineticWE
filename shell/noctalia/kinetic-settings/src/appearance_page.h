#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Node;
class Renderer;
class SessionBus;

// The Appearance -> Title Bars settings page: the titlebar (window decoration)
// picker, rendered as a tile grid holding the four Kinetic decoration variants
// (standard/dots x dark/light). The selection is written to the compositor's
// ~/.config/kineticwe/kineticwe.kwe ([org.kde.kdecoration2] library/theme) by
// applyAll(), which the window's Apply button calls; the compositor is then
// asked to reconfigure.
//
// Theme COLORS are not handled here: the shell's theme service owns the
// generated palette (qt5ct/qt6ct color schemes, ~/.config/kdeglobals, the GTK
// dark/light preference), so this page intentionally has no theme selector.
//
// Like the Tiling/Workspaces pages, edits are STAGED: a tile click updates the
// in-memory pending decoration and nothing touches disk until the window's
// Apply button calls applyAll(). On failure the staged edit is kept so Apply can
// be retried. The Icons and Cursors subcategories live in their own pages
// (IconsPage/CursorsPage) rendered by the owning window, not this page.
class AppearancePage {
public:
  struct Filter {
    std::string searchQuery;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window (same shape as TilingPage/WorkspacesPage).
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit AppearancePage(SessionBus& bus);

  std::unique_ptr<Node> build(
      float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey,
      const Callbacks& callbacks
  );

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept {
    return m_pendingDecoration.has_value() || m_pendingHideTitleBars.has_value();
  }
  // Writes the staged titlebar selection and "Toggle title bars" state to
  // kineticwe.kwe and asks the compositor to reconfigure. Returns false on
  // failure (the staged edit is kept for retry).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pendingDecoration.reset();
    m_pendingHideTitleBars.reset();
    m_errorNotice.clear();
  }

  // Error message to surface when the last Apply failed (kept until the next
  // successful Apply or reload).
  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  // The titlebar selection: a (library, theme) pair written to kineticwe.kwe
  // [org.kde.kdecoration2]. The Kinetic decoration is the compiled-in default.
  struct DecorationSelection {
    std::string library; // "kineticwe"
    std::string theme;   // "dark" / "light", or "dots-dark" / "dots-light"

    [[nodiscard]] bool operator==(const DecorationSelection& other) const noexcept {
      return library == other.library && theme == other.theme;
    }
  };

  void ensureLoaded();

  // --- Title bars (the page's only section) --------------------------------
  [[nodiscard]] static DecorationSelection defaultDecoration();
  [[nodiscard]] static std::string decorationKeyPath() { return "org.kde.kdecoration2.theme"; }
  [[nodiscard]] static std::string titleBarsKeyPath() { return "Windows/DefaultDecorationPolicy"; }
  [[nodiscard]] DecorationSelection fileBackedDecoration() const;
  [[nodiscard]] DecorationSelection effectiveDecoration() const;
  [[nodiscard]] bool overriddenDecoration() const;
  // Stages a decoration selection; clears the staged value when it matches disk.
  void stageDecoration(const DecorationSelection& selection);
  void stageDecorationAndRebuild(
      const DecorationSelection& selection, const Callbacks& callbacks
  );
  // The global "Toggle title bars" switch: [Windows] DefaultDecorationPolicy
  // (None/0 hides every frame, ClientPreference/1 shows the client's), unless a
  // per-window rule overrides it.
  [[nodiscard]] bool titleBarsHiddenOnDisk() const;
  [[nodiscard]] bool effectiveHideTitleBars() const;
  // Stages the toggle; clears it when it matches disk.
  void stageHideTitleBars(bool hide);
  // Writes the staged titlebar selection and toggle to kineticwe.kwe
  // ([org.kde.kdecoration2] + [Windows]) and asks the compositor to reconfigure.
  // Reloads the file first so concurrently applied edits on other pages are
  // preserved.
  [[nodiscard]] bool applyTitleBars();
  // Builds the section (the toggle row, then the header/reset row and tile
  // grid). Returns nullptr when the active filter leaves nothing to show.
  [[nodiscard]] std::unique_ptr<Node> buildDecorationsSection(
      float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
  );

  bool m_loaded = false;
  // Staged titlebar selection (kineticwe.kwe [org.kde.kdecoration2]).
  std::optional<DecorationSelection> m_pendingDecoration;
  // Staged "Toggle title bars" value (kineticwe.kwe [Windows]); nullopt = no edit.
  std::optional<bool> m_pendingHideTitleBars;
  // kineticwe.kwe reader/writer for the decoration keys.
  KineticweKwe m_decorationsConfig;
  CompositorDbus m_dbus;
  std::string m_errorNotice;
};
