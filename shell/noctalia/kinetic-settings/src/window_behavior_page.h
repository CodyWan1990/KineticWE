#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>
#include <vector>

class Flex;
class Node;

// The Window Behavior settings page: edits the window-behavior keys in
// ~/.config/kineticwe/kineticwe.kwe. This is the mirror of the KDE "Window
// Behavior" KCM; the Focus tab is implemented first (the remaining tabs —
// Titlebar Actions, Window Actions, Movement & Placement — land as further
// page sections later).
//
// The compositor reads these keys from the [Windows] group (src/options.cpp
// via KWinOptionsSettings in kwinoptions_settings.kcfg) and applies them on
// org.kde.KWin.reconfigure(); this page writes the .kwe file and triggers one
// reconfigure(), the same contract as the Workspaces/Tiling pages.
//
// Edits are STAGED: controls update an in-memory pending map and nothing
// touches disk or the compositor until the window's Apply button calls
// applyAll(), which saves kineticwe.kwe and reloads the compositor once.
// Closing the window discards staged edits. Values that differ from the
// compiled-in defaults show an Override badge and a two-stage Reset button,
// mirroring the Workspaces/Appearance pages.
class WindowBehaviorPage {
public:
  using Value = std::variant<bool, int, std::string>;

  // Filters driven by the window chrome (search field, Advanced toggle,
  // Overridden toggle).
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as WorkspacesPage::Callbacks.
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
    // Window-level fixed section jump row; when non-null each collapsible
    // section adds a pill to it. Owned by the window.
    Flex* jumpRow = nullptr;
  };

  explicit WindowBehaviorPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the "group.key" path whose Reset
  // button was clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // "group.key" paths of every resettable setting on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one setting back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  // Stages every persisted setting back at its default. The changes land only
  // when the user hits Apply.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

  // Writes every staged edit to the .kwe file and reloads the compositor ONCE.
  // Returns false when the (first, gating) file write failed — the staged edits
  // are kept for retry and nothing reaches the compositor.
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
  }

private:
  enum class ControlKind {
    Toggle,
    Stepper,
    Select,
  };

  struct Field {
    std::string_view group;      // .kwe group: "Windows"
    std::string_view key;        // .kwe key
    ControlKind control = ControlKind::Toggle;
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup;  // group header shown on the page
    bool advanced = false;
    int minValue = 0;            // steppers
    int maxValue = 100;
    int step = 1;
    std::string_view valueSuffix;
    // Select controls: the file-stored option values and the labels shown to
    // the user. Values must match the exact strings the compositor reads
    // (kwinoptions_settings.kcfg), including explicit value= attribute strings.
    const std::vector<std::string>* optionValues = nullptr;
    const std::vector<std::string>* optionLabels = nullptr;
    Value fallback;              // compiled-in default (override detection)
  };

  static const std::vector<Field>& fields();
  static const std::vector<std::string>& focusPolicyValues();
  static const std::vector<std::string>& focusPolicyLabels();
  static const std::vector<std::string>& activationPolicyValues();
  static const std::vector<std::string>& activationPolicyLabels();
  static const std::vector<std::string>& stealingLevelValues();
  static const std::vector<std::string>& stealingLevelLabels();
  static const std::vector<std::string>& titlebarDoubleClickValues();
  static const std::vector<std::string>& titlebarDoubleClickLabels();
  static const std::vector<std::string>& titlebarWheelValues();
  static const std::vector<std::string>& titlebarWheelLabels();
  static const std::vector<std::string>& activeTitlebarValues();
  static const std::vector<std::string>& activeTitlebarLabels();
  static const std::vector<std::string>& inactiveTitlebarValues();
  static const std::vector<std::string>& inactiveTitlebarLabels();
  static const std::vector<std::string>& maximizeButtonValues();
  static const std::vector<std::string>& maximizeButtonLabels();

  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);
  [[nodiscard]] static bool valuesEqual(const Field& field, const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);
  // Select helpers: map a stored Value to the string used to match an option
  // (and back). Needed because the Focus stealing level is stored as an int but
  // presented as a named dropdown, while FocusPolicy stores string enum names.
  [[nodiscard]] static std::string valueAsString(const Field& field, const Value& value);
  [[nodiscard]] static Value valueFromString(const Field& field, std::string_view s);

  void ensureLoaded();
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: the staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;

  // Writes `value` into the config model (or removes the key when it equals the
  // default). No save / no compositor reload — applyAll() owns those.
  void writeValue(const Field& field, const Value& value);
  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node>
  buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by "group.key"; flushed by applyAll().
  std::map<std::string, Value> m_pending;
  // Page-group keys whose collapsible card is expanded. Owned here so the state
  // survives the window's per-change scene rebuilds; the first group is
  // expanded on the first non-search build.
  std::unordered_set<std::string> m_expandedGroups;
  bool m_expandedInitialized = false;
};
