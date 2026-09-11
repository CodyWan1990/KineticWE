#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <cstddef>
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

// The Accessibility settings page: a Keybinds-style page (subcategory list on
// the left, settings on the right) that surfaces the compositor's accessibility
// effects, which are marked internal in their metadata and therefore hidden on
// the Desktop Effects page. Two subcategories are implemented:
//
//   * Zoom & Magnifier — the [Effect-zoom] keys shared by the full-screen Zoom
//     effect and the region Magnifier effect (they form the "magnifiers"
//     exclusive group), plus the magnifier's [Effect-magnifier] region size.
//   * Shake Pointer    — the [Effect-shakecursor] keys.
//
// Each subcategory has an enable toggle backed by [Plugins] <name>Enabled —
// except Zoom & Magnifier, which uses an exclusive mode selector (full-screen
// Zoom vs region Magnifier, the "magnifiers" group).
// Edits are STAGED (in-memory) and only reach kineticwe.kwe when the window's
// Apply button calls applyAll(); applyAll then reloads/unloads the effect for
// enable changes and calls reconfigureEffect() for the [Effect-*] groups, the
// same live-apply contract as the Desktop Effects page. Closing the window
// discards staged edits. Values that differ from the compiled-in defaults show
// an Override badge and a two-stage Reset button.
class AccessibilityPage {
public:
  using Value = std::variant<bool, int, double, std::string>;

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

  explicit AccessibilityPage(class SessionBus& bus);

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
  [[nodiscard]] bool hasPending() const noexcept {
    return !m_pending.empty() || m_pendingZoomMode.has_value();
  }

  // Writes every staged edit to kineticwe.kwe, then applies it live: load/
  // unloadEffect() for [Plugins] enable toggles and reconfigureEffect() for the
  // [Effect-*] groups. Returns false when the file write failed — staged edits
  // are kept for retry and nothing reaches the compositor.
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_pendingZoomMode.reset();
  }

private:
  enum class Category {
    ZoomAndMagnifier,
    ShakePointer,
  };

  // The Zoom & Magnifier subcategory is one exclusive group: either the
  // full-screen Zoom effect or the region Magnifier effect is enabled.
  enum class ZoomMode {
    FullScreen,
    MagnifyRegion,
  };

  // Which Zoom mode(s) a setting applies to. Shared keys live in [Effect-zoom]
  // and are read by both effects; the rest are effect-specific.
  enum class ZoomScope {
    Both,
    FullScreen,
    MagnifyRegion,
  };

  enum class ControlKind {
    Toggle,
    Stepper,
    Slider,
    Select,
    Input,
  };

  struct Field {
    Category category = Category::ZoomAndMagnifier;
    std::string_view group;  // .kwe group: "Effect-zoom", "Effect-shakecursor", "Plugins"
    std::string_view key;    // .kwe key
    ControlKind control = ControlKind::Toggle;
    std::string_view title;
    std::string_view subtitle;
    bool advanced = false;
    // Full-screen-only or magnify-region-only visibility (Both = always shown).
    ZoomScope scope = ZoomScope::Both;
    // Slider/stepper bounds (steppers cast to int).
    double minValue = 0.0;
    double maxValue = 100.0;
    double step = 1.0;
    int decimals = 2;  // decimal places shown next to a slider
    std::string_view valueSuffix;
    // Select controls: stored values and the labels shown to the user.
    const std::vector<int>* optionValues = nullptr;
    const std::vector<std::string>* optionLabels = nullptr;
    Value fallback;                    // compiled-in default (override detection)
    std::string_view effectName;       // compositor effect id for live apply
  };

  static const std::vector<Field>& fields();
  static const std::vector<Category>& categories();
  static std::string_view categoryTitle(Category category);
  static std::string_view categoryDescription(Category category);

  static const std::vector<int>& mouseTrackingValues();
  static const std::vector<std::string>& mouseTrackingLabels();

  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view path);
  [[nodiscard]] static bool valuesEqual(const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);

  void ensureLoaded();
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: the staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;

  // Writes `value` into the config model (or removes the key when it equals the
  // default). No save / no compositor apply — applyAll() owns those.
  void writeValue(const Field& field, const Value& value);
  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node> buildCategoryList(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node>
  buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  // --- Zoom mode (exclusive full-screen / magnify-region selector) ---------
  [[nodiscard]] static bool effectEnabledDefault(std::string_view effect, bool fallback);
  [[nodiscard]] static ZoomMode defaultZoomMode();
  [[nodiscard]] static std::string zoomModeKeyPath() { return "Plugins.zoomMode"; }
  [[nodiscard]] ZoomMode fileBackedZoomMode() const;
  [[nodiscard]] ZoomMode effectiveZoomMode() const;
  [[nodiscard]] bool zoomModeOverridden() const;
  // Whether a setting is relevant for the effective zoom mode.
  [[nodiscard]] bool modeAllows(const Field& field) const;
  [[nodiscard]] bool zoomModeMatchesFilter(const Filter& filter) const;
  // Stages a mode; clears the staged value when it matches disk.
  void stageZoomMode(ZoomMode mode);
  void stageZoomModeAndRebuild(ZoomMode mode, const Callbacks& callbacks);
  // Writes both [Plugins] enable keys for the mode (or removes them at default).
  void writeZoomMode(ZoomMode mode);
  [[nodiscard]] std::unique_ptr<Node>
  buildZoomModeRow(float scale, std::string_view armedResetKey, const Callbacks& callbacks);

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by "group.key"; flushed by applyAll().
  std::map<std::string, Value> m_pending;
  // Staged full-screen vs magnify-region choice (the [Plugins] enable pair).
  std::optional<ZoomMode> m_pendingZoomMode;
  // Selected subcategory (left-hand list).
  Category m_selectedCategory = Category::ZoomAndMagnifier;
};
