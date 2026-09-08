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

// The Keyboard settings page: edits the key-repeat keys in
// ~/.config/kineticwe/kineticwe.kwe. The compositor reads these from the
// [Keyboard] group (src/keyboard_input.cpp) and applies them when the session
// starts or on org.kde.KWin.reconfigure(); this page writes the .kwe file and
// triggers one reconfigure() so the change applies live, the same contract as
// the Window Behavior page.
//
// Edits are STAGED: controls update an in-memory pending map and nothing
// touches disk or the compositor until the window's Apply button calls
// applyAll(). Values that differ from the compositor's compiled-in defaults
// show an Override badge and a two-stage Reset button.
class KeyboardPage {
public:
  using Value = std::variant<bool, int, double>;

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

  explicit KeyboardPage(SessionBus& bus);

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // "group.key" paths of every resettable setting on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  void resetKeyPath(std::string_view keyPath);
  void resetAll();
  [[nodiscard]] bool hasPending() const noexcept;
  bool applyAll();
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
  }

private:
  // One editable key-repeat setting.
  struct Field {
    std::string_view key;        // .kwe key inside the [Keyboard] group
    std::string_view title;
    std::string_view subtitle;
    bool isToggle = false;       // toggle; false = numeric stepper
    bool isSelect = false;       // select (dropdown); takes precedence over isToggle/stepper
    std::vector<int> selectValues;         // select: numeric value per option index
    std::vector<std::string> selectLabels; // select: label per option index
    Value fallback;              // compositor's compiled-in default
    // Numeric fields (stepper):
    int minValue = 0;
    int maxValue = 100;
    int step = 1;
    std::string_view valueSuffix;
  };

  static const std::vector<Field>& fields();
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);

  void ensureLoaded();
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool equalsDefault(const Field& field, const Value& value) const;
  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);
  // Writes a staged value into the config model (no save — applyAll() owns that).
  void writeValue(const Field& field, const Value& value);

  [[nodiscard]] std::unique_ptr<Node> buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by "Keyboard.<key>"; flushed by applyAll().
  std::map<std::string, Value> m_pending;
};
