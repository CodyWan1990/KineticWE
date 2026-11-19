#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

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

// The Hot Corners settings page: edits the screen-corner actions and timing in
// ~/.config/kineticwe/kineticwe.kwe and applies them with one
// org.kde.KWin.reconfigure(), the same contract as the Window Behavior page.
//
// Keys read by the compositor:
//   [ScreenEdges] HotCornersEnabled, RemainActiveOnFullscreen
//   [Windows] ElectricBorderDelay, ElectricBorderCooldown,
//             ElectricBorderCornerRatio, ElectricBorderAllScreenCorner
//   [EdgeBarrier] CornerBarrier, EdgeBarrier
//   [ElectricBorders] TopLeft/TopRight/BottomLeft/BottomRight (built-in actions)
//   [Effect-windowview] BorderActivateAll/BorderActivate/BorderActivateClass
//   [Effect-overview] BorderActivate/GridBorderActivate
//   [TabBox] BorderActivate/BorderAlternativeActivate
//
// A corner has exactly one action but KWin stores it in several places: the
// built-in [ElectricBorders] entry and the IntList border providers of the
// Overview, Present Windows and Tab Box effects. Reading follows the KCM's
// precedence (built-in first, then each provider in order, a later provider
// wins). Writing removes the corner from every provider list, clears the
// built-in entry, and then stores the action in exactly one place. Provider
// lists are normalized: when a provider's compiled default is non-empty, an
// empty list is written as ElectricNone (9) so that default cannot silently
// reactivate a corner; a key is removed only when its normalized list equals
// the provider's default.
//
// Edits are STAGED: controls update an in-memory pending map and nothing
// touches disk or the compositor until the window's Apply button calls
// applyAll(), which saves kineticwe.kwe and reloads the compositor once.
// Closing the window discards staged edits. Values that differ from the
// compiled-in defaults show an Override badge and a two-stage Reset button,
// mirroring the Window Behavior page.
class HotCornersPage {
public:
  using Value = std::variant<bool, int, double, std::string, std::vector<std::string>>;

  // Filters driven by the window chrome (search field, Overridden toggle).
  struct Filter {
    std::string searchQuery;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as WindowBehaviorPage::Callbacks.
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
    // Window-level fixed section jump row; when non-null each collapsible
    // section adds a pill to it. Owned by the window.
    Flex* jumpRow = nullptr;
  };

  explicit HotCornersPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the "group.key" path whose Reset
  // button was clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // "group.key" paths of every resettable setting on the page (Reset Page scope),
  // including the four logical corner paths (ElectricBorders.TopLeft etc.).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one setting back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  // Stages every persisted setting back at its default. The changes land only
  // when the user hits Apply.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

  // Writes every staged edit to the .kwe file and reloads the compositor ONCE.
  // Returns false when the file write failed — the staged edits are kept for
  // retry and nothing reaches the compositor.
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_errorNotice.clear();
  }

private:
  enum class ControlKind {
    Toggle,
    Stepper,
    // A double stored as a fraction but edited as an integer percentage.
    PercentStepper,
    // One of the four screen corners; the value is an action id (see the
    // action catalog in the .cpp) stored across the border providers.
    Corner,
  };

  struct Field {
    std::string_view group; // .kwe group: "Windows"
    std::string_view key;   // .kwe key
    ControlKind control = ControlKind::Toggle;
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup; // group header shown on the page
    int minValue = 0;           // steppers
    int maxValue = 100;
    int step = 1;
    std::string_view valueSuffix;
    Value fallback; // compiled-in default (override detection)
    int border = 0; // corner rows: ElectricBorder value
  };

  static const std::vector<Field>& fields();

  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);
  [[nodiscard]] static bool valuesEqual(const Field& field, const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);
  [[nodiscard]] static bool isCornerRow(const Field& field) { return field.control == ControlKind::Corner; }

  void ensureLoaded();
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: the staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;
  [[nodiscard]] bool hotCornersEnabled() const;

  // Corner action resolution and storage across the border providers.
  [[nodiscard]] std::string cornerAction(const Field& field) const;
  void writeCorner(const Field& field, std::string_view actionId);

  // Writes `value` into the config model (or removes the key when it equals the
  // default). No save / no compositor reload — applyAll() owns those.
  void writeValue(const Field& field, const Value& value);
  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node> buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by "group.key"; flushed by applyAll().
  // Corner rows use their logical path ("ElectricBorders.TopLeft") and store
  // the action id.
  std::map<std::string, Value> m_pending;
  // Page-group keys whose collapsible card is expanded. Owned here so the state
  // survives the window's per-change scene rebuilds; all groups are expanded on
  // the first non-search build.
  std::unordered_set<std::string> m_expandedGroups;
  bool m_expandedInitialized = false;
  std::string m_errorNotice;
};
