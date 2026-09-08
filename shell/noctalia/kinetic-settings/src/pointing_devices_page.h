#pragma once

#include "kinetic-settings/src/compositor_dbus.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;

// The pointing-devices settings page, shared by the sidebar's "Mouse" and
// "Touchpad" entries. It configures the compositor's pointer devices through
// the per-device org.kde.KWin.InputDevice D-Bus interface that the compositor
// already exposes (the same one Plasma's Mouse KCM talks to).
//
// A page instance is built for one Kind: the Mouse kind lists only mice
// (pointer devices that are not touchpads) and shows pointer settings; the
// Touchpad kind lists only touchpads and shows touchpad settings.
//
// Unlike the config-file pages, edits here are pushed straight to the
// compositor: each property WRITE persists the value to the compositor's input
// config AND applies it to libinput live (Device::writeEntry + setter). To keep
// the window's staged-Apply contract, the page stages edits in memory and only
// issues the D-Bus writes when applyAll() runs. Override badges compare the
// device's current value against the device's own *EnabledByDefault value.
class PointingDevicesPage {
public:
  enum class Kind {
    Mouse,
    Touchpad,
  };

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

  PointingDevicesPage(SessionBus& bus, Kind kind);

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // Property names of every resettable setting on the current device.
  [[nodiscard]] std::vector<std::string> resettableKeyPaths();

  // Stages one setting back at its device default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  void resetAll();
  [[nodiscard]] bool hasPending() const noexcept;
  bool applyAll();
  void reloadFromDisk() noexcept {
    m_pending.clear();
  }

private:
  // The settings surfaced for one device, keyed by the D-Bus property the
  // control writes to. `support` is a property that must be true for the row to
  // be shown (e.g. "supportsNaturalScroll"); empty means always shown.
  struct Field {
    std::string_view writeProperty;   // property set on Apply
    std::string_view readProperty;    // property read for the current value
    std::string_view defaultProperty; // property read for the "default" value
    std::string_view supportProperty; // gate; empty = always visible
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup;       // section header shown on the page
    bool isBool = false;              // false = numeric slider
    bool isSelect = false;            // profile select (writeProperty is synthetic)
    double minValue = 0.0;            // numeric slider range
    double maxValue = 1.0;
    double step = 0.05;
  };

  [[nodiscard]] const std::vector<Field>& fields() const;

  // Device state helpers (all read live over D-Bus each build).
  bool supports(const CompositorDbus::InputDeviceInfo& device, std::string_view supportProperty);
  bool currentBool(const CompositorDbus::InputDeviceInfo& device, std::string_view property);
  double currentDouble(const CompositorDbus::InputDeviceInfo& device, std::string_view property);
  bool isOverridden(const Field& field);
  bool rowVisible(const Field& field, const CompositorDbus::InputDeviceInfo& device);

  // Staging: values are stored as the raw control value (bool -> 1.0/0.0).
  void stage(std::string_view writeProperty, double value);
  [[nodiscard]] std::optional<double> staged(std::string_view writeProperty) const;
  // Device value for a field, encoded the same way as staged values.
  double currentDeviceValue(const Field& field);
  // The device's *EnabledByDefault (or default pointer-acceleration) value.
  double deviceDefaultValue(const Field& field);

  void addDevicePicker(Flex& parent, float scale, const Callbacks& callbacks);
  void addFieldRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );
  void addSectionHeader(Flex& parent, std::string_view title, bool& firstSection, float scale);

  Kind m_kind;
  CompositorDbus m_dbus;
  // sysName of the device being configured.
  std::string m_selectedDevice;
  // Cached device list of this kind (mice or touchpads).
  std::vector<CompositorDbus::InputDeviceInfo> m_devices;
  bool m_devicesLoaded = false;
  // Edits staged for the selected device, keyed by write property. Double holds
  // 1.0/0.0 for bool fields. Flushed by applyAll() via D-Bus.
  std::map<std::string, double> m_pending;
};
