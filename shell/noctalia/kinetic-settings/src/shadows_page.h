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

// The Appearance -> Shadows settings page: the window drop shadow drawn by the
// compositor around tiled windows. It owns the six [Tiling] TilingShadow* keys
// (mode, size, color, opacity, offset X/Y) even though it lives under
// Appearance: the compositor's tiling controller is what draws the shadow, so
// the storage group stays [Tiling] and existing users keep their values.
//
// Edits are STAGED (in-memory) and only reach kineticwe.kwe when the window's
// Apply button calls applyAll(), which also asks the compositor to reconfigure
// once. Values that differ from the compiled-in defaults show an Override badge
// and a two-stage Reset button; resetting stages the default and removes the
// key on Apply.
class ShadowsPage {
public:
  using Value = std::variant<int, std::string>;

  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit ShadowsPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the "group.key" path whose Reset
  // button was clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // "group.key" paths of every resettable setting on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one setting back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view path);
  // Stages every setting back at its default.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

  // Writes every staged edit to kineticwe.kwe and asks the compositor to
  // reconfigure so the tiling controller re-reads the shadow settings. Returns
  // false when the file write failed (staged edits kept for retry).
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
  }

private:
  enum class ControlKind {
    Select,
    Slider,
    Text,
  };

  struct Field {
    std::string_view group; // .kwe group: "Tiling"
    std::string_view key;   // .kwe key
    ControlKind control = ControlKind::Slider;
    std::string_view title;
    std::string_view subtitle;
    // Sliders (values are stored as ints).
    double minValue = 0.0;
    double maxValue = 100.0;
    std::string_view valueSuffix;
    const std::vector<std::string>* optionValues = nullptr; // selects: values stored in the file
    const std::vector<std::string>* optionLabels = nullptr; // selects: labels shown
    std::string_view placeholder;                           // text inputs
    Value fallback;                                         // compiled-in default (override detection)
  };

  static const std::vector<Field>& fields();
  static const std::vector<std::string>& shadowModeValues();
  static const std::vector<std::string>& shadowModeLabels();

  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view path);
  // Typed equality with per-key normalization (the custom color compares
  // case-insensitively); equalsDefault() is valuesEqual() against the fallback.
  [[nodiscard]] static bool valuesEqual(const Field& field, const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);

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
  // The collapsible card's expanded state. Owned here so the state survives the
  // window's per-change scene rebuilds; expanded on the first non-search build.
  std::unordered_set<std::string> m_expandedGroups;
  bool m_expandedInitialized = false;
};
