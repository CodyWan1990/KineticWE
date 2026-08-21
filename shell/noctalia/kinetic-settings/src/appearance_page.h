#pragma once

#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Flex;
class Node;

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
// (IconsPage/CursorsPage) rendered by the owning window, not this page.
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

  AppearancePage() = default;

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }
  // Writes every staged edit to appearance.kwe and, when the Global Theme
  // changed, regenerates ~/.config/kdeglobals from the selected scheme.
  // Returns false on failure (staged edits are kept for retry).
  bool applyAll();

  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_errorNotice.clear();
  }

  // Error message to surface when the last Apply failed (kept until the next
  // successful Apply or reload).
  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
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

  KineticweKwe m_config;
  bool m_loaded = false;
  std::map<std::string, Value> m_pending;
  std::string m_errorNotice;
};
