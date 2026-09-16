#include "pointing_devices_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Page state (selected device per kind) inside the kineticwe config root.
  std::filesystem::path pointingDevicesStatePath() {
    return KineticweKwe::defaultPath().parent_path() / "kinetic-settings.kwe";
  }

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;

  // Markup-free label helper mirroring settings::makeLabel.
  [[nodiscard]] std::unique_ptr<Label>
  makeLabel(std::string_view text, float fontSize, const ColorSpec& color, FontWeight fontWeight = FontWeight::Normal) {
    return ui::label({
        .text = std::string(text),
        .fontSize = fontSize,
        .fontWeight = fontWeight,
        .color = color,
    });
  }

  // Status pill ("Override"); module-local re-implementation of
  // SettingsControlFactory::makeStatusBadge.
  [[nodiscard]] std::unique_ptr<Flex>
  makeStatusBadge(std::string_view label, const ColorSpec& fill, const ColorSpec& color, float scale) {
    return ui::row(
        {
            .align = FlexAlign::Center,
            .gap = 0.0F,
            .paddingV = 0.0F,
            .paddingH = Style::spaceXs * scale,
            .fill = fill,
            .radius = Style::scaledRadiusSm(scale),
        },
        makeLabel(label, Style::fontSizeCaption * scale, color, FontWeight::Bold)
    );
  }

  [[nodiscard]] std::unique_ptr<Flex> makeOverrideBadge(float scale) {
    return makeStatusBadge(
        i18n::tr("settings.badges.override"), colorSpecFromRole(ColorRole::Primary, 0.15F),
        colorSpecFromRole(ColorRole::Primary), scale
    );
  }

  // The two-stage Reset action shown next to overridden settings.
  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const PointingDevicesPage::Callbacks& callbacks
  ) {
    return ui::button({
        .text = i18n::tr(pendingConfirmation ? "settings.actions.confirm-reset" : "settings.actions.reset"),
        .fontSize = Style::fontSizeCaption * scale,
        .variant = pendingConfirmation ? ButtonVariant::Destructive : ButtonVariant::Ghost,
        .minHeight = Style::controlHeightSm * scale,
        .paddingV = Style::spaceXs * scale,
        .paddingH = Style::spaceSm * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = [path = std::move(path), pendingConfirmation, callbacks]() mutable {
          if (!pendingConfirmation) {
            if (callbacks.armReset) {
              callbacks.armReset(std::move(path));
            }
            return;
          }
          if (callbacks.confirmReset) {
            callbacks.confirmReset(std::move(path));
          }
        },
    });
  }

} // namespace

PointingDevicesPage::PointingDevicesPage(SessionBus& bus, Kind kind) : m_kind(kind), m_dbus(bus) {
  m_state.setPath(pointingDevicesStatePath());
  m_state.load();
  m_selectedDevice = m_state.readString("PointingDevices", m_kind == Kind::Mouse ? "Mouse" : "Touchpad").value_or("");
  if (m_kind == Kind::Mouse) {
    // The Mouse page also carries the compositor-wide [EdgeBarrier] setting.
    m_compositorConfig.load();
  }
}

// ---------------------------------------------------------------------------
// Schema: one entry per setting. Property names are the exact
// org.kde.KWin.InputDevice properties exported by src/backends/libinput/device.h.
// ---------------------------------------------------------------------------

const std::vector<PointingDevicesPage::Field>& PointingDevicesPage::fields() const {
  static const std::vector<Field> kMouseFields{
      // Pointer ---------------------------------------------------------
      Field{
          .writeProperty = "pointerAcceleration",
          .readProperty = "pointerAcceleration",
          .defaultProperty = "defaultPointerAcceleration",
          .supportProperty = "supportsPointerAcceleration",
          .title = "Pointer acceleration",
          .subtitle = "How much the pointer speeds up as the mouse moves faster "
                      "(negative slows the pointer, positive speeds it up)",
          .pageGroup = "Pointer",
          .isBool = false,
          .minValue = -1.0,
          .maxValue = 1.0,
          .step = 0.05,
      },
      Field{
          .writeProperty = "AccelerationProfile",
          .readProperty = "pointerAccelerationProfileAdaptive",
          .defaultProperty = "defaultPointerAccelerationProfileAdaptive",
          .supportProperty = "ProfileBoth",
          .title = "Acceleration profile",
          .subtitle = "Adaptive scales speed with movement; flat keeps a constant factor",
          .pageGroup = "Pointer",
          .isSelect = true,
      },
      // Buttons ---------------------------------------------------------
      Field{
          .writeProperty = "leftHanded",
          .readProperty = "leftHanded",
          .defaultProperty = "leftHandedEnabledByDefault",
          .supportProperty = "supportsLeftHanded",
          .title = "Left handed",
          .subtitle = "Swap the primary and secondary mouse buttons",
          .pageGroup = "Buttons",
          .isBool = true,
      },
      Field{
          .writeProperty = "middleEmulation",
          .readProperty = "middleEmulation",
          .defaultProperty = "middleEmulationEnabledByDefault",
          .supportProperty = "supportsMiddleEmulation",
          .title = "Middle button emulation",
          .subtitle = "Simulate the middle mouse button by pressing the left and "
                      "right buttons together",
          .pageGroup = "Buttons",
          .isBool = true,
      },
      // Scrolling -------------------------------------------------------
      Field{
          .writeProperty = "naturalScroll",
          .readProperty = "naturalScroll",
          .defaultProperty = "naturalScrollEnabledByDefault",
          .supportProperty = "supportsNaturalScroll",
          .title = "Natural scrolling",
          .subtitle = "Reverse the mouse wheel's scroll direction",
          .pageGroup = "Scrolling",
          .isBool = true,
      },
      // Screen edges (compositor-wide [EdgeBarrier] group in kineticwe.kwe) ---
      Field{
          .writeProperty = "EdgeBarrier",
          .readProperty = "EdgeBarrier",
          .defaultProperty = "",
          .supportProperty = "",
          .title = "Edge barrier",
          .subtitle = "Extra distance the pointer must travel to cross onto another "
                      "screen. Set to None to move freely between screens.",
          .pageGroup = "Screen Edges",
          .isBool = false,
          .minValue = 0.0,
          .maxValue = 1000.0,
          .step = 1.0,
          .isGlobal = true,
          .integerValue = true,
          .zeroLabel = "None",
      },
  };

  static const std::vector<Field> kTouchpadFields{
      // General ---------------------------------------------------------
      Field{
          .writeProperty = "disableWhileTyping",
          .readProperty = "disableWhileTyping",
          .defaultProperty = "disableWhileTypingEnabledByDefault",
          .supportProperty = "supportsDisableWhileTyping",
          .title = "Disable while typing",
          .subtitle = "Ignore accidental touchpad input while typing on the keyboard",
          .pageGroup = "General",
          .isBool = true,
      },
      Field{
          .writeProperty = "disableEventsOnExternalMouse",
          .readProperty = "disableEventsOnExternalMouse",
          .defaultProperty = "disableEventsOnExternalMouseEnabledByDefault",
          .supportProperty = "supportsDisableEventsOnExternalMouse",
          .title = "Disable on external mouse",
          .subtitle = "Turn the touchpad off while a USB or Bluetooth mouse is connected",
          .pageGroup = "General",
          .isBool = true,
      },
      // Tapping ---------------------------------------------------------
      Field{
          .writeProperty = "tapToClick",
          .readProperty = "tapToClick",
          .defaultProperty = "tapToClickEnabledByDefault",
          .supportProperty = "TapSupported",
          .title = "Tap to click",
          .subtitle = "Tap the touchpad to click instead of pressing a physical button",
          .pageGroup = "Tapping",
          .isBool = true,
      },
      Field{
          .writeProperty = "tapAndDrag",
          .readProperty = "tapAndDrag",
          .defaultProperty = "tapAndDragEnabledByDefault",
          .supportProperty = "TapSupported",
          .title = "Tap and drag",
          .subtitle = "Drag items by tapping and dragging across the touchpad",
          .pageGroup = "Tapping",
          .isBool = true,
      },
      Field{
          .writeProperty = "tapDragLock",
          .readProperty = "tapDragLock",
          .defaultProperty = "tapDragLockEnabledByDefault",
          .supportProperty = "TapSupported",
          .title = "Tap drag lock",
          .subtitle = "Lock a drag after a quick tap so the finger can rest while "
                      "dragging",
          .pageGroup = "Tapping",
          .isBool = true,
      },
      // Scrolling -------------------------------------------------------
      Field{
          .writeProperty = "naturalScroll",
          .readProperty = "naturalScroll",
          .defaultProperty = "naturalScrollEnabledByDefault",
          .supportProperty = "supportsNaturalScroll",
          .title = "Natural scrolling",
          .subtitle = "Scroll content in the same direction the fingers move, like "
                      "a touchscreen",
          .pageGroup = "Scrolling",
          .isBool = true,
      },
      Field{
          .writeProperty = "scrollTwoFinger",
          .readProperty = "scrollTwoFinger",
          .defaultProperty = "scrollTwoFingerEnabledByDefault",
          .supportProperty = "supportsScrollTwoFinger",
          .title = "Two-finger scrolling",
          .subtitle = "Scroll by dragging two fingers across the touchpad",
          .pageGroup = "Scrolling",
          .isBool = true,
      },
      Field{
          .writeProperty = "scrollEdge",
          .readProperty = "scrollEdge",
          .defaultProperty = "scrollEdgeEnabledByDefault",
          .supportProperty = "supportsScrollEdge",
          .title = "Edge scrolling",
          .subtitle = "Scroll by dragging along the right or bottom edge of the touchpad",
          .pageGroup = "Scrolling",
          .isBool = true,
      },
  };

  return m_kind == Kind::Mouse ? kMouseFields : kTouchpadFields;
}

// ---------------------------------------------------------------------------
// Device reads (live D-Bus).
// ---------------------------------------------------------------------------

bool PointingDevicesPage::supports(const CompositorDbus::InputDeviceInfo& device, std::string_view supportProperty) {
  if (supportProperty == "ProfileBoth") {
    return m_dbus.inputDeviceBool(device.sysName, "supportsPointerAccelerationProfileAdaptive").value_or(false)
        && m_dbus.inputDeviceBool(device.sysName, "supportsPointerAccelerationProfileFlat").value_or(false);
  }
  if (supportProperty == "TapSupported") {
    return m_dbus.inputDeviceUint(device.sysName, "tapFingerCount").value_or(0) > 0;
  }
  if (supportProperty.empty()) {
    return true;
  }
  return m_dbus.inputDeviceBool(device.sysName, supportProperty).value_or(false);
}

bool PointingDevicesPage::currentBool(const CompositorDbus::InputDeviceInfo& device, std::string_view property) {
  return m_dbus.inputDeviceBool(device.sysName, property).value_or(false);
}

double PointingDevicesPage::currentDouble(const CompositorDbus::InputDeviceInfo& device, std::string_view property) {
  return m_dbus.inputDeviceDouble(device.sysName, property).value_or(0.0);
}

bool PointingDevicesPage::rowVisible(const Field& field, const CompositorDbus::InputDeviceInfo& device) {
  return field.supportProperty.empty() || supports(device, field.supportProperty);
}

// ---------------------------------------------------------------------------
// Staging.
// ---------------------------------------------------------------------------

std::optional<double> PointingDevicesPage::staged(std::string_view writeProperty) const {
  const auto it = m_pending.find(std::string(writeProperty));
  if (it == m_pending.end()) {
    return std::nullopt;
  }
  return it->second;
}

bool PointingDevicesPage::isOverridden(const Field& field) {
  const double effective = staged(field.writeProperty).value_or(currentDeviceValue(field));
  return std::abs(effective - deviceDefaultValue(field)) > 1e-9;
}

void PointingDevicesPage::stage(std::string_view writeProperty, double value) {
  // Editing back to the device's current value un-stages (the row stops being
  // dirty and Apply becomes a no-op for it).
  const auto it = std::find_if(fields().cbegin(), fields().cend(), [&](const Field& f) {
    return f.writeProperty == writeProperty;
  });
  if (it != fields().cend() && std::abs(value - currentDeviceValue(*it)) < 1e-9) {
    m_pending.erase(std::string(writeProperty));
  } else {
    m_pending[std::string(writeProperty)] = value;
  }
}

void PointingDevicesPage::resetKeyPath(std::string_view keyPath) {
  for (const auto& field : fields()) {
    if (field.writeProperty == keyPath) {
      // Stage the device default so Apply pushes the device back to it.
      m_pending[std::string(keyPath)] = deviceDefaultValue(field);
      return;
    }
  }
}

void PointingDevicesPage::resetAll() {
  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  for (const auto& field : fields()) {
    if (field.isGlobal) {
      m_pending[std::string(field.writeProperty)] = deviceDefaultValue(field);
    } else if (it != m_devices.cend() && rowVisible(field, *it)) {
      m_pending[std::string(field.writeProperty)] = deviceDefaultValue(field);
    }
  }
}

bool PointingDevicesPage::hasPending() const noexcept {
  return !m_pending.empty();
}

std::vector<std::string> PointingDevicesPage::resettableKeyPaths() {
  std::vector<std::string> paths;
  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  for (const auto& field : fields()) {
    if (field.isGlobal) {
      paths.emplace_back(field.writeProperty);
    } else if (it != m_devices.cend() && rowVisible(field, *it)) {
      paths.emplace_back(field.writeProperty);
    }
  }
  return paths;
}

// ---------------------------------------------------------------------------
// Device value helpers (encoded like staged values: bools are 1.0/0.0).
// ---------------------------------------------------------------------------

double PointingDevicesPage::currentDeviceValue(const Field& field) {
  if (field.isGlobal) {
    // Compositor-wide setting in kineticwe.kwe; clamped to the schema range.
    const int value = m_compositorConfig.readInt("EdgeBarrier", field.readProperty, 0);
    return std::clamp(static_cast<double>(value), field.minValue, field.maxValue);
  }
  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  if (it == m_devices.cend()) {
    return 0.0;
  }
  if (field.isBool || field.isSelect) {
    return currentBool(*it, field.readProperty) ? 1.0 : 0.0;
  }
  return currentDouble(*it, field.readProperty);
}

double PointingDevicesPage::deviceDefaultValue(const Field& field) {
  if (field.isGlobal) {
    // [EdgeBarrier] EdgeBarrier defaults to 0 (None) in src/kwin.kcfg.
    return 0.0;
  }
  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  if (it == m_devices.cend()) {
    return 0.0;
  }
  if (field.isBool || field.isSelect) {
    return currentBool(*it, field.defaultProperty) ? 1.0 : 0.0;
  }
  return currentDouble(*it, field.defaultProperty);
}

// ---------------------------------------------------------------------------
// Persistence: Apply pushes staged edits to the compositor over D-Bus (each
// write persists to the compositor's input config and applies live).
// ---------------------------------------------------------------------------

bool PointingDevicesPage::applyAll() {
  if (m_pending.empty()) {
    return true;
  }
  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  const bool haveDevice = it != m_devices.cend();

  bool edgeBarrierPending = false;
  for (const auto& [key, value] : m_pending) {
    if (key == "EdgeBarrier") {
      // Compositor-wide setting, written to kineticwe.kwe below.
      edgeBarrierPending = true;
      continue;
    }
    if (!haveDevice) {
      continue;
    }
    if (key == "AccelerationProfile") {
      // Adaptive == value != 0; write the matching profile bool (the device's
      // setter switches the active profile and persists it).
      const bool adaptive = value != 0.0;
      m_dbus.setInputDeviceBool(
          it->sysName, adaptive ? "pointerAccelerationProfileAdaptive" : "pointerAccelerationProfileFlat", true
      );
      continue;
    }
    const auto fIt = std::find_if(fields().cbegin(), fields().cend(), [&](const Field& f) {
      return f.writeProperty == key;
    });
    if (fIt == fields().cend()) {
      continue;
    }
    if (fIt->isBool) {
      m_dbus.setInputDeviceBool(it->sysName, key, value != 0.0);
    } else {
      m_dbus.setInputDeviceDouble(it->sysName, key, value);
    }
  }

  if (edgeBarrierPending) {
    const int barrier = static_cast<int>(std::lround(m_pending.at("EdgeBarrier")));
    if (barrier == 0) {
      // 0 (None) is the default: drop the key so the compositor falls back to it.
      m_compositorConfig.removeKey("EdgeBarrier", "EdgeBarrier");
    } else {
      m_compositorConfig.setInt("EdgeBarrier", "EdgeBarrier", barrier);
    }
    if (!m_compositorConfig.save()) {
      kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
      // Keep the staged edits so the user can retry Apply.
      return false;
    }
  }

  m_pending.clear();
  if (edgeBarrierPending) {
    m_dbus.requestReconfigure();
  }
  return true;
}

// ---------------------------------------------------------------------------
// Device selection persistence.
// ---------------------------------------------------------------------------

std::string PointingDevicesPage::pickDefaultDevice() const {
  if (m_devices.empty()) {
    return {};
  }
  // Prefer the most feature-complete device (largest button set): a full mouse
  // beats a keyboard's companion "Mouse" interface or a game-controller puck.
  const auto it = std::max_element(m_devices.cbegin(), m_devices.cend(), [](const auto& a, const auto& b) {
    return a.supportedButtons < b.supportedButtons;
  });
  return it->sysName;
}

void PointingDevicesPage::persistSelectedDevice() {
  // Re-read first so the sibling page's saved key is not overwritten.
  m_state.load();
  m_state.setString("PointingDevices", m_kind == Kind::Mouse ? "Mouse" : "Touchpad", m_selectedDevice);
  if (!m_state.save()) {
    kLog.warn("kinetic-settings: failed to write {}", pointingDevicesStatePath().string());
  }
}

// ---------------------------------------------------------------------------
// Controls + layout.
// ---------------------------------------------------------------------------

void PointingDevicesPage::addDevicePicker(Flex& parent, float scale, const Callbacks& callbacks) {
  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(
      "Device", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  titleRow->addChild(ui::spacer());

  std::vector<std::string> options;
  std::size_t selectedIndex = 0;
  for (std::size_t i = 0; i < m_devices.size(); ++i) {
    const auto& device = m_devices[i];
    std::string label = device.name;
    if (device.touchpad) {
      label += " (Touchpad)";
    } else if (device.pointer) {
      label += " (Mouse)";
    }
    options.push_back(std::move(label));
    if (device.sysName == m_selectedDevice) {
      selectedIndex = i;
    }
  }

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  actions->addChild(ui::select({
      .options = options,
      .selectedIndex = selectedIndex,
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .width = 280.0F * scale,
      .height = Style::controlHeight * scale,
      .onSelectionChanged = [this, callbacks](std::size_t index, std::string_view /*label*/) {
        if (index < m_devices.size() && m_devices[index].sysName != m_selectedDevice) {
          m_selectedDevice = m_devices[index].sysName;
          persistSelectedDevice();
          // Device edits are dropped on switch; the compositor-wide Edge barrier
          // is not tied to a device, so keep its staged edit.
          const auto edgeBarrier = m_pending.find("EdgeBarrier");
          const std::optional<double> stagedEdgeBarrier =
              edgeBarrier != m_pending.end() ? std::optional<double>{edgeBarrier->second} : std::nullopt;
          m_pending.clear();
          if (stagedEdgeBarrier.has_value()) {
            m_pending["EdgeBarrier"] = *stagedEdgeBarrier;
          }
          if (callbacks.requestRebuild) {
            callbacks.requestRebuild();
          }
        }
      },
  }));

  auto row = ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceXs * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true}
  );
  row->addChild(std::move(titleRow));
  row->addChild(std::move(actions));
  parent.addChild(std::move(row));
}

void PointingDevicesPage::addSectionHeader(Flex& parent, std::string_view title, bool& firstSection, float scale) {
  auto label = makeLabel(
      title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
  );
  if (!firstSection) {
    parent.addChild(ui::column(
        {.align = FlexAlign::Stretch,
         .gap = Style::spaceSm * scale,
         .configure = [scale](Flex& flex) { flex.setPadding(Style::spaceSm * scale, 0.0F, 0.0F, 0.0F); }},
        ui::separator(),
        std::move(label)
    ));
  } else {
    parent.addChild(std::move(label));
  }
  firstSection = false;
}

void PointingDevicesPage::addFieldRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = std::string(field.writeProperty);
  const bool rowOverridden = isOverridden(field);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!field.subtitle.empty()) {
    copy->addChild(ui::label({
        .text = std::string(field.subtitle),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = kSettingDescriptionMaxLines,
    }));
  }

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  if (rowOverridden) {
    actions->addChild(makeOverrideBadge(scale));
    actions->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
  }

  // Build the control.
  if (field.isSelect) {
    // Adaptive / Flat profile selector.
    const bool adaptive = [&]() {
      const auto stagedValue = staged(field.writeProperty);
      if (stagedValue.has_value()) {
        return *stagedValue != 0.0;
      }
      const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
        return d.sysName == m_selectedDevice;
      });
      return it != m_devices.cend() && currentBool(*it, field.readProperty);
    }();
    const std::vector<std::string> profileLabels{"Adaptive", "Flat"};
    actions->addChild(ui::select({
        .options = profileLabels,
        .selectedIndex = adaptive ? 0u : 1u,
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .width = 150.0F * scale,
        .height = Style::controlHeight * scale,
        .onSelectionChanged = [this, field, callbacks](std::size_t index, std::string_view /*label*/) {
          stage(field.writeProperty, index == 0 ? 1.0 : 0.0);
          if (callbacks.requestRebuild) {
            callbacks.requestRebuild();
          }
        },
    }));
  } else if (field.isBool) {
    const bool checked = [&]() {
      const auto stagedValue = staged(field.writeProperty);
      if (stagedValue.has_value()) {
        return *stagedValue != 0.0;
      }
      const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
        return d.sysName == m_selectedDevice;
      });
      return it != m_devices.cend() && currentBool(*it, field.readProperty);
    }();
    actions->addChild(ui::toggle({
        .checked = checked,
        .scale = scale,
        .onChange = [this, field, callbacks](bool value) {
          stage(field.writeProperty, value ? 1.0 : 0.0);
          if (callbacks.requestRebuild) {
            callbacks.requestRebuild();
          }
        },
    }));
  } else {
    // Numeric slider.
    const auto stagedValue = staged(field.writeProperty);
    const double value = stagedValue.has_value() ? *stagedValue : currentDeviceValue(field);
    // Integer-valued rows (e.g. Edge barrier) hide the decimals and can name
    // their zero position (e.g. "None").
    const auto formatValue = [](const Field& f, double v) -> std::string {
      if (!f.integerValue) {
        return std::format("{:.2f}", v);
      }
      const int rounded = static_cast<int>(std::lround(v));
      if (rounded == 0 && !f.zeroLabel.empty()) {
        return std::string(f.zeroLabel);
      }
      return std::to_string(rounded);
    };
    Slider* sliderPtr = nullptr;
    Label* valuePtr = nullptr;
    auto slider = ui::slider({
        .out = &sliderPtr,
        .minValue = field.minValue,
        .maxValue = field.maxValue,
        .step = field.step,
        .value = value,
        .trackHeight = Style::sliderTrackHeight * scale,
        .thumbSize = Style::sliderThumbSize * scale,
        .controlHeight = Style::controlHeight * scale,
        .width = Style::sliderDefaultWidth * scale,
    });
    auto valueLabel = ui::label({
        .out = &valuePtr,
        .text = formatValue(field, value),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .minWidth = 52.0F * scale,
        .textAlign = TextAlign::End,
    });
    slider->setOnValueChanged([valuePtr, field, formatValue](double next) {
      if (valuePtr != nullptr) {
        valuePtr->setText(formatValue(field, next));
      }
    });
    slider->setOnDragEnd([this, field, sliderPtr, callbacks]() {
      if (sliderPtr != nullptr) {
        const double clamped = std::clamp(sliderPtr->value(), field.minValue, field.maxValue);
        stage(field.writeProperty, clamped);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      }
    });
    actions->addChild(std::move(slider));
    actions->addChild(std::move(valueLabel));
  }

  auto row = ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceXs * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true}
  );
  row->addChild(std::move(copy));
  row->addChild(std::move(actions));
  parent.addChild(std::move(row));
}

std::unique_ptr<Node> PointingDevicesPage::build(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const bool isTouchpad = m_kind == Kind::Touchpad;
  const std::string_view pageTitle = isTouchpad ? "Touchpad" : "Mouse";
  const char* pageGlyph = isTouchpad ? "mouse-2" : "mouse";

  if (!m_devicesLoaded) {
    m_devices = m_dbus.inputDevices();
    // Keep only real devices of this page's kind. The compositor's
    // ListPointers() returns every pointer-capable device: touchpads, plus
    // game-controller/keyboard companion interfaces (the Valve pucks also
    // report a keyboard capability) and uinput/virtual devices. None of those
    // is a mouse, so the mouse page keeps only pointer devices that are not
    // touchpads, not keyboards and not virtual.
    m_devices.erase(
        std::remove_if(m_devices.begin(), m_devices.end(), [isTouchpad](const auto& d) {
          if (d.isVirtual) {
            return true;
          }
          return isTouchpad ? !d.touchpad : (d.touchpad || d.keyboard);
        }),
        m_devices.end()
    );
    m_devicesLoaded = true;
  }

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  // Section header: glyph + bold primary title.
  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = pageGlyph,
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel(pageTitle, Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  const bool searchActive = !filter.searchQuery.empty();

  // Fall back to a sensible device when nothing is selected yet or the saved
  // device is gone (e.g. unplugged).
  const bool selectedPresent = std::any_of(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  if (!m_devices.empty() && !selectedPresent) {
    const bool hadSavedSelection = !m_selectedDevice.empty();
    m_selectedDevice = pickDefaultDevice();
    // Pin the first-run default so a later device addition cannot silently
    // change it. A saved device that is merely unplugged is left untouched so
    // it is restored when it comes back.
    if (!hadSavedSelection) {
      persistSelectedDevice();
    }
  }

  // The device picker is page chrome, not a setting; hide it while searching so
  // merged results only show the matching rows.
  if (!searchActive && !m_devices.empty()) {
    addDevicePicker(*root, scale, callbacks);
  }

  const auto it = std::find_if(m_devices.cbegin(), m_devices.cend(), [this](const auto& d) {
    return d.sysName == m_selectedDevice;
  });
  const CompositorDbus::InputDeviceInfo* device = it != m_devices.cend() ? &*it : nullptr;

  const auto searchMatches = [&filter](std::string_view text) {
    return filter.searchQuery.empty() || StringUtils::containsInsensitive(text, filter.searchQuery);
  };

  bool anyRows = false;
  bool firstSection = true;
  std::string_view lastSection;
  for (const auto& field : fields()) {
    // Compositor-wide fields (e.g. Edge barrier) render even without a device;
    // per-device fields need a selected device that supports them.
    if (!field.isGlobal && (device == nullptr || !rowVisible(field, *device))) {
      continue;
    }
    if (filter.showOverriddenOnly && !isOverridden(field)) {
      continue;
    }
    if (!filter.searchQuery.empty() && !searchMatches(field.title) && !searchMatches(field.subtitle)) {
      continue;
    }
    if (field.pageGroup != lastSection) {
      lastSection = field.pageGroup;
      addSectionHeader(*root, field.pageGroup, firstSection, scale);
    }
    addFieldRow(*root, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    // Global search drops pages with no hits.
    if (searchActive) {
      return nullptr;
    }
    root->addChild(makeLabel(
        m_devices.empty() ? (isTouchpad ? "No touchpads detected." : "No mice detected.")
                          : (isTouchpad ? "No touchpad settings match the current filters."
                                        : "No mouse settings match the current filters."),
        Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
