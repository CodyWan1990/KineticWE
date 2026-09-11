#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Flex;
class Node;
class Renderer;
class SessionBus;

// The Appearance settings page: edits the Global Theme choice stored in
// ~/.config/kineticwe/appearance.kwe ([Appearance] group, GlobalTheme key) and
// applies the selected Breeze Dark/Light scheme to Qt/KDE applications by
// expanding a Breeze seed palette through Noctalia's palette generator and
// merging the resulting KColorScheme into ~/.config/kdeglobals.
//
// Appearance is deliberately independent of KDE Plasma: it links only against
// the Noctalia theme library (already compiled into this binary) and writes the
// shared kdeglobals file directly (Qt apps pick the scheme up on their next
// launch). The appearance.kwe file is portable — copy it (and the rest of
// ~/.config/kineticwe) to a new machine to bring your theme choice along.
//
// Like the Tiling/Workspaces pages, edits are STAGED: controls update an
// in-memory pending map and nothing touches disk until the window's Apply
// button calls applyAll(), which saves appearance.kwe and regenerates
// kdeglobals in one go. On failure the staged edits are kept so Apply can be
// retried. The Icons and Cursors subcategories live in their own pages
// (IconsPage/CursorsPage) rendered by the owning window, not this page; the
// Window Decorations picker is rendered inline on this page, below the Global
// Theme selector.
class AppearancePage {
public:
  using Value = std::variant<bool, int, double, std::string, std::vector<std::string>>;

  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
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
    return !m_pending.empty() || m_pendingDecoration.has_value();
  }
  // Writes every staged edit: the Global Theme to appearance.kwe (regenerating
  // ~/.config/kdeglobals when it changed) and the window decoration selection
  // to kineticwe.kwe. Returns false on failure (staged edits are kept for retry).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_pendingDecoration.reset();
    m_errorNotice.clear();
  }

  // Error message to surface when the last Apply failed (kept until the next
  // successful Apply or reload).
  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  // The window decoration selection: a (library, theme) pair written to
  // kineticwe.kwe [org.kde.kdecoration2]. Breeze is the compiled-in default.
  struct DecorationSelection {
    std::string library; // "org.kde.breeze" or "org.kde.kwin.aurorae.v2"
    std::string theme;   // "Breeze" or "__aurorae__svg__<Name>"

    [[nodiscard]] bool operator==(const DecorationSelection& other) const noexcept {
      return library == other.library && theme == other.theme;
    }
  };
  enum class ControlKind { Select };

  struct Field {
    std::string_view group;
    std::string_view key;
    ControlKind control = ControlKind::Select;
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup;
    bool advanced = false;
    const std::vector<std::string>* optionValues = nullptr;
    const std::vector<std::string>* optionLabels = nullptr;
    Value fallback;
  };

  static const std::vector<Field>& fields();
  static const std::vector<std::string>& themeValues();
  static const std::vector<std::string>& themeLabels();
  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);
  [[nodiscard]] static bool valuesEqual(const Field& field, const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);

  void ensureLoaded();
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;

  void writeValue(const Field& field, const Value& value);
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node>
  buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  // Applies the given theme ("BreezeDark"/"BreezeLight") by regenerating
  // ~/.config/kdeglobals from the Noctalia-expanded seed palette.
  [[nodiscard]] bool applyTheme(std::string_view theme, std::string* error);

  // --- Window Decorations (inline section on this page) --------------------
  [[nodiscard]] static DecorationSelection defaultDecoration();
  [[nodiscard]] static std::string decorationKeyPath() { return "org.kde.kdecoration2.theme"; }
  [[nodiscard]] DecorationSelection fileBackedDecoration() const;
  [[nodiscard]] DecorationSelection effectiveDecoration() const;
  [[nodiscard]] bool overriddenDecoration() const;
  // Stages a decoration selection; clears the staged value when it matches disk.
  void stageDecoration(const DecorationSelection& selection);
  void stageDecorationAndRebuild(
      const DecorationSelection& selection, const Callbacks& callbacks
  );
  // Writes the selection to kineticwe.kwe [org.kde.kdecoration2] and asks the
  // compositor to reconfigure. Reloads the file first so concurrently applied
  // edits on other pages are preserved.
  [[nodiscard]] bool applyDecoration(const DecorationSelection& selection);
  // Builds the inline section (header row with store/folder buttons + reset,
  // then the tile grid). Returns nullptr when the active filter leaves nothing
  // to show (so the caller can skip the group header).
  [[nodiscard]] std::unique_ptr<Node> buildDecorationsSection(
      float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey,
      const Callbacks& callbacks
  );

  KineticweKwe m_config;
  bool m_loaded = false;
  std::map<std::string, Value> m_pending;
  // Staged window decoration selection (kineticwe.kwe, not appearance.kwe).
  std::optional<DecorationSelection> m_pendingDecoration;
  // kineticwe.kwe reader/writer for the decoration keys; kept separate from
  // m_config (which targets appearance.kwe).
  KineticweKwe m_decorationsConfig;
  CompositorDbus m_dbus;
  std::string m_errorNotice;
};
