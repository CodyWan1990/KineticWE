#include "window_behavior_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "kinetic-settings/src/section_card.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/controls/stepper.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;
  // Dropdown width, mirroring the Appearance page's select control.
  constexpr float kControlWidth = 220.0F;

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

  // Status pill ("Override" / "Advanced"); a module-local re-implementation of
  // SettingsControlFactory::makeStatusBadge (same as the Workspaces page).
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

  [[nodiscard]] std::unique_ptr<Flex> makeAdvancedBadge(float scale) {
    return makeStatusBadge(
        i18n::tr("settings.badges.advanced"), colorSpecFromRole(ColorRole::OnSurfaceVariant, 0.12F),
        colorSpecFromRole(ColorRole::OnSurfaceVariant), scale
    );
  }

  // The two-stage Reset action shown next to overridden settings: the first
  // click arms it, the second confirms (upstream makeGroupedResetButton).
  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const WindowBehaviorPage::Callbacks& callbacks
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

  [[nodiscard]] std::string joinKeyPath(std::string_view group, std::string_view key) {
    return std::string(group) + '.' + std::string(key);
  }

} // namespace

WindowBehaviorPage::WindowBehaviorPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: option catalogs + the field descriptor table — the single source of
// truth for file keys, defaults, ranges and presentation. Defaults mirror
// src/kcms/options/kwinoptions_settings.kcfg [Windows] group; keys are written
// verbatim, which is what the compositor parses natively.
// ---------------------------------------------------------------------------

// Focus-policy option values: the EXACT strings the compositor reads
// (kwinoptions_settings.kcfg FocusPolicy enum, no value= attribute).
const std::vector<std::string>& WindowBehaviorPage::focusPolicyValues() {
  static const std::vector<std::string> v{"ClickToFocus", "FocusFollowsMouse", "FocusUnderMouse", "FocusStrictlyUnderMouse"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::focusPolicyLabels() {
  static const std::vector<std::string> v{
      "Click to focus", "Focus follows pointer", "Focus under pointer", "Focus strictly under pointer"};
  return v;
}
// Activation desktop policy values (kcfg ActivationDesktopPolicy enum).
const std::vector<std::string>& WindowBehaviorPage::activationPolicyValues() {
  static const std::vector<std::string> v{"SwitchToOtherDesktop", "BringToCurrentDesktop"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::activationPolicyLabels() {
  static const std::vector<std::string> v{"Switch to window's Virtual Desktop", "Bring window to current Virtual Desktop"};
  return v;
}
// Focus-stealing prevention levels: stored as int 0..4, shown as names.
const std::vector<std::string>& WindowBehaviorPage::stealingLevelValues() {
  static const std::vector<std::string> v{"0", "1", "2", "3", "4"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::stealingLevelLabels() {
  static const std::vector<std::string> v{"None", "Low", "Medium", "High", "Extreme"};
  return v;
}
// Titlebar double-click command (kcfg TitlebarDoubleClickCommand; the vertical/
// horizontal choices store a value= string that differs from the choice name).
const std::vector<std::string>& WindowBehaviorPage::titlebarDoubleClickValues() {
  static const std::vector<std::string> v{
      "Maximize", "Maximize (vertical only)", "Maximize (horizontal only)", "Minimize", "Lower", "Close", "OnAllDesktops", "Nothing"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::titlebarDoubleClickLabels() {
  static const std::vector<std::string> v{
      "Maximize", "Vertically maximize", "Horizontally maximize", "Minimize", "Lower", "Close", "Show on all desktops", "Do nothing"};
  return v;
}
// Mouse wheel on the titlebar (kcfg CommandTitlebarWheel).
const std::vector<std::string>& WindowBehaviorPage::titlebarWheelValues() {
  static const std::vector<std::string> v{
      "Raise/Lower", "Maximize/Restore", "Above/Below", "Previous/Next Desktop", "Change Opacity", "Nothing"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::titlebarWheelLabels() {
  static const std::vector<std::string> v{
      "Raise/lower", "Maximize/restore", "Keep above/below", "Move to previous/next desktop", "Change opacity", "Do nothing"};
  return v;
}
// Clicks on a FOCUSED titlebar (kcfg CommandActiveTitlebar1/2/3).
const std::vector<std::string>& WindowBehaviorPage::activeTitlebarValues() {
  static const std::vector<std::string> v{
      "Raise", "Lower", "Toggle raise and lower", "Minimize", "Close", "Operations menu", "Nothing"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::activeTitlebarLabels() {
  static const std::vector<std::string> v{
      "Raise", "Lower", "Raise/lower", "Minimize", "Close", "Window menu", "Do nothing"};
  return v;
}
// Clicks on an UNFOCUSED titlebar (kcfg CommandInactiveTitlebar1/2/3).
const std::vector<std::string>& WindowBehaviorPage::inactiveTitlebarValues() {
  static const std::vector<std::string> v{
      "Activate and raise", "Activate and lower", "Activate", "Raise", "Lower", "Toggle raise and lower", "Minimize", "Close", "Operations menu", "Nothing"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::inactiveTitlebarLabels() {
  static const std::vector<std::string> v{
      "Focus and raise", "Focus and lower", "Focus", "Raise", "Lower", "Raise/lower", "Minimize", "Close", "Window menu", "Do nothing"};
  return v;
}
// The three maximize-button click commands (kcfg MaximizeButtonLeft/Middle/RightClickCommand).
const std::vector<std::string>& WindowBehaviorPage::maximizeButtonValues() {
  static const std::vector<std::string> v{"Maximize", "Maximize (vertical only)", "Maximize (horizontal only)"};
  return v;
}
const std::vector<std::string>& WindowBehaviorPage::maximizeButtonLabels() {
  static const std::vector<std::string> v{"Maximize", "Vertically maximize", "Horizontally maximize"};
  return v;
}
const std::vector<WindowBehaviorPage::Field>& WindowBehaviorPage::fields() {
  static const std::vector<Field> table = [] {
    std::vector<Field> f;
    // Focus ----------------------------------------------------------------
    f.push_back(Field{
        .group = "Windows",
        .key = "FocusPolicy",
        .control = ControlKind::Select,
        .title = "Window focus policy",
        .subtitle = "How a window receives keyboard focus: on click, or as the "
                    "pointer moves over it",
        .pageGroup = "Focus",
        .optionValues = &focusPolicyValues(),
        .optionLabels = &focusPolicyLabels(),
        .fallback = Value{std::string{"ClickToFocus"}},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "NextFocusPrefersMouse",
        .title = "After closing a window, move focus to the window under the pointer",
        .subtitle = "When enabled the next focus lands under the pointer; "
                    "otherwise the previously focused window is refocused",
        .pageGroup = "Focus",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "DelayFocusInterval",
        .control = ControlKind::Stepper,
        .title = "Delay focus",
        .subtitle = "How long the compositor waits before focusing a window under "
                    "the pointer",
        .pageGroup = "Focus",
        .minValue = 0,
        .maxValue = 3000,
        .step = 100,
        .valueSuffix = "ms",
        .fallback = Value{300},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "FocusStealingPreventionLevel",
        .control = ControlKind::Select,
        .title = "Focus stealing prevention",
        .subtitle = "How aggressively the compositor prevents windows from "
                    "stealing focus while you work elsewhere",
        .pageGroup = "Focus",
        .minValue = 0,
        .maxValue = 4,
        .step = 1,
        .optionValues = &stealingLevelValues(),
        .optionLabels = &stealingLevelLabels(),
        .fallback = Value{1},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "ActivationDesktopPolicy",
        .control = ControlKind::Select,
        .title = "When focusing a window on a different Virtual Desktop",
        .subtitle = "What happens when you focus a window that lives on another "
                    "workspace",
        .pageGroup = "Focus",
        .optionValues = &activationPolicyValues(),
        .optionLabels = &activationPolicyLabels(),
        .fallback = Value{std::string{"SwitchToOtherDesktop"}},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "ClickRaise",
        .title = "Click raises focused window",
        .subtitle = "A click on the window also raises it above the others",
        .pageGroup = "Focus",
        .fallback = Value{true},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "AutoRaise",
        .title = "Raise on hover",
        .subtitle = "Raises the window under the pointer after a short delay, "
                    "without requiring a click",
        .pageGroup = "Focus",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "AutoRaiseInterval",
        .control = ControlKind::Stepper,
        .title = "Raise delay",
        .subtitle = "How long the pointer must stay over a window before it is "
                    "raised",
        .pageGroup = "Focus",
        .minValue = 0,
        .maxValue = 3000,
        .step = 100,
        .valueSuffix = "ms",
        .fallback = Value{750},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "SeparateScreenFocus",
        .title = "Separate screen focus",
        .subtitle = "Each screen keeps its own focused window instead of all "
                    "screens sharing one focus",
        .pageGroup = "Focus",
        .fallback = Value{true},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "WarpPointerOnScreenSwitch",
        .title = "Move pointer when switching screens",
        .subtitle = "Warp the pointer to the center of the screen focused by "
                    "the Switch to Screen shortcuts, instead of leaving it on "
                    "the previous screen",
        .pageGroup = "Focus",
        .fallback = Value{false},
    });
    // Titlebar Actions -------------------------------------------------------
    f.push_back(Field{
        .group = "Windows",
        .key = "TitlebarDoubleClickCommand",
        .control = ControlKind::Select,
        .title = "Double-click",
        .subtitle = "What a double-click on the titlebar does",
        .pageGroup = "Titlebar Actions",
        .optionValues = &titlebarDoubleClickValues(),
        .optionLabels = &titlebarDoubleClickLabels(),
        .fallback = Value{std::string{"Maximize"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandTitlebarWheel",
        .control = ControlKind::Select,
        .title = "Mouse wheel",
        .subtitle = "What scrolling over the titlebar does",
        .pageGroup = "Titlebar Actions",
        .optionValues = &titlebarWheelValues(),
        .optionLabels = &titlebarWheelLabels(),
        .fallback = Value{std::string{"Nothing"}},
    });
    // Titlebar and Frame Actions ---------------------------------------------
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandActiveTitlebar1",
        .control = ControlKind::Select,
        .title = "Left click on focused titlebar",
        .subtitle = "What a left click does when the titlebar is already focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &activeTitlebarValues(),
        .optionLabels = &activeTitlebarLabels(),
        .fallback = Value{std::string{"Raise"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandInactiveTitlebar1",
        .control = ControlKind::Select,
        .title = "Left click on unfocused titlebar",
        .subtitle = "What a left click does when the titlebar is not focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &inactiveTitlebarValues(),
        .optionLabels = &inactiveTitlebarLabels(),
        .fallback = Value{std::string{"Activate and raise"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandActiveTitlebar2",
        .control = ControlKind::Select,
        .title = "Middle click on focused titlebar",
        .subtitle = "What a middle click does when the titlebar is already focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &activeTitlebarValues(),
        .optionLabels = &activeTitlebarLabels(),
        .fallback = Value{std::string{"Nothing"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandInactiveTitlebar2",
        .control = ControlKind::Select,
        .title = "Middle click on unfocused titlebar",
        .subtitle = "What a middle click does when the titlebar is not focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &inactiveTitlebarValues(),
        .optionLabels = &inactiveTitlebarLabels(),
        .fallback = Value{std::string{"Nothing"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandActiveTitlebar3",
        .control = ControlKind::Select,
        .title = "Right click on focused titlebar",
        .subtitle = "What a right click does when the titlebar is already focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &activeTitlebarValues(),
        .optionLabels = &activeTitlebarLabels(),
        .fallback = Value{std::string{"Operations menu"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "CommandInactiveTitlebar3",
        .control = ControlKind::Select,
        .title = "Right click on unfocused titlebar",
        .subtitle = "What a right click does when the titlebar is not focused",
        .pageGroup = "Titlebar and Frame Actions",
        .optionValues = &inactiveTitlebarValues(),
        .optionLabels = &inactiveTitlebarLabels(),
        .fallback = Value{std::string{"Operations menu"}},
    });
    f.push_back(Field{
        .group = "MouseBindings",
        .key = "DoubleClickBorderToMaximize",
        .title = "Maximize by double-clicking the frame",
        .subtitle = "A double-click on the window frame (not the titlebar button) "
                    "maximizes the window",
        .pageGroup = "Titlebar and Frame Actions",
        .fallback = Value{true},
    });
    // Maximize Button Actions -------------------------------------------------
    f.push_back(Field{
        .group = "Windows",
        .key = "MaximizeButtonLeftClickCommand",
        .control = ControlKind::Select,
        .title = "Maximize button left click",
        .subtitle = "What a left click on the maximize button does",
        .pageGroup = "Maximize Button Actions",
        .optionValues = &maximizeButtonValues(),
        .optionLabels = &maximizeButtonLabels(),
        .fallback = Value{std::string{"Maximize"}},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "MaximizeButtonMiddleClickCommand",
        .control = ControlKind::Select,
        .title = "Maximize button middle click",
        .subtitle = "What a middle click on the maximize button does",
        .pageGroup = "Maximize Button Actions",
        .optionValues = &maximizeButtonValues(),
        .optionLabels = &maximizeButtonLabels(),
        .fallback = Value{std::string{"Maximize (vertical only)"}},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "MaximizeButtonRightClickCommand",
        .control = ControlKind::Select,
        .title = "Maximize button right click",
        .subtitle = "What a right click on the maximize button does",
        .pageGroup = "Maximize Button Actions",
        .optionValues = &maximizeButtonValues(),
        .optionLabels = &maximizeButtonLabels(),
        .fallback = Value{std::string{"Maximize (horizontal only)"}},
    });
    // Clipboard --------------------------------------------------------------
    // The compositor only creates the zwp_primary_selection global at startup
    // (src/wayland_server.cpp), so this takes effect on the next session.
    f.push_back(Field{
        .group = "Wayland",
        .key = "EnablePrimarySelection",
        .title = "Middle-click paste (primary selection)",
        .subtitle = "Selecting text copies it to a separate clipboard that a "
                    "middle click pastes. Takes effect after logging out and "
                    "back in",
        .pageGroup = "Clipboard",
        .fallback = Value{true},
    });
    return f;
  }();
  return table;
}
// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string WindowBehaviorPage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const WindowBehaviorPage::Field* WindowBehaviorPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

WindowBehaviorPage::Value WindowBehaviorPage::defaultValue(const Field& field) {
  return field.fallback;
}

bool WindowBehaviorPage::valuesEqual(const Field& /*field*/, const Value& a, const Value& b) {
  return a.index() == b.index() && a == b;
}

bool WindowBehaviorPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(field, value, defaultValue(field));
}

std::string WindowBehaviorPage::valueAsString(const Field& field, const Value& value) {
  (void)field;
  return std::visit(
      [](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return v;
        } else if constexpr (std::is_same_v<T, int>) {
          return std::to_string(v);
        } else {
          return v ? "true" : "false";
        }
      },
      value
  );
}

WindowBehaviorPage::Value WindowBehaviorPage::valueFromString(const Field& field, std::string_view s) {
  // The target Value type is derived from the field's fallback (string enum
  // names vs. int levels).
  return std::visit(
      [s](const auto& fallback) -> Value {
        using T = std::decay_t<decltype(fallback)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return Value{std::string(s)};
        } else if constexpr (std::is_same_v<T, int>) {
          int parsed = 0;
          const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), parsed);
          if (ec == std::errc() && end == s.data() + s.size()) {
            return Value{parsed};
          }
          return Value{fallback};
        } else {
          return Value{s == "true"};
        }
      },
      field.fallback
  );
}

void WindowBehaviorPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

WindowBehaviorPage::Value WindowBehaviorPage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else if constexpr (std::is_same_v<T, int>) {
          const auto parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, field.minValue, field.maxValue);
        } else {
          return m_config.readString(field.group, field.key).value_or(fallbackValue);
        }
      },
      fallback
  );
}

WindowBehaviorPage::Value WindowBehaviorPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool WindowBehaviorPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool WindowBehaviorPage::matchesFilter(const Field& field, const Filter& filter) const {
  if (!filter.showAdvanced && field.advanced) {
    return false;
  }
  if (filter.showOverriddenOnly && equalsDefault(field, effectiveValue(field))) {
    return false;
  }
  if (!filter.searchQuery.empty()) {
    const auto matches = [&filter](std::string_view text) {
      return StringUtils::containsInsensitive(text, filter.searchQuery);
    };
    if (!matches(field.title) && !matches(field.subtitle) && !matches(field.pageGroup)) {
      return false;
    }
  }
  return true;
}
// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the Workspaces page's override semantics.
// ---------------------------------------------------------------------------

void WindowBehaviorPage::writeValue(const Field& field, const Value& value) {
  if (equalsDefault(field, value)) {
    m_config.removeKey(field.group, field.key);
  } else {
    std::visit(
        [this, &field](const auto& v) {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, bool>) {
            m_config.setBool(field.group, field.key, v);
          } else if constexpr (std::is_same_v<T, int>) {
            m_config.setInt(field.group, field.key, v);
          } else {
            m_config.setString(field.group, field.key, v);
          }
        },
        value
    );
  }
}

void WindowBehaviorPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(field, value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void WindowBehaviorPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> WindowBehaviorPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void WindowBehaviorPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void WindowBehaviorPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
}

bool WindowBehaviorPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  for (const auto& [path, value] : m_pending) {
    if (const Field* field = findField(path)) {
      writeValue(*field, value);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  m_pending.clear();
  m_dbus.requestReconfigure();
  return true;
}
// ---------------------------------------------------------------------------
// Controls (Noctalia settings visuals, re-implemented module-locally).
// ---------------------------------------------------------------------------

std::unique_ptr<Node>
WindowBehaviorPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  switch (field.control) {
  case ControlKind::Toggle: {
    const bool checked = std::get<bool>(effectiveValue(field));
    return ui::toggle({
        .checked = checked,
        .scale = scale,
        .onChange = [this, field, callbacks](bool value) {
          stageAndRebuild(field, Value{value}, callbacks);
        },
    });
  }
  case ControlKind::Stepper: {
    const int value = std::get<int>(effectiveValue(field));
    return ui::stepper({
        .minValue = field.minValue,
        .maxValue = field.maxValue,
        .step = field.step,
        .value = std::clamp(value, field.minValue, field.maxValue),
        .scale = scale,
        .valueSuffix = std::string(field.valueSuffix),
        // Commit-only (Enter / focus loss / arrow clicks): dragging to a value
        // only stages on the final value, never mid-interaction.
        .onValueCommitted = [this, field, callbacks](int committed) {
          stageAndRebuild(field, Value{std::clamp(committed, field.minValue, field.maxValue)}, callbacks);
        },
    });
  }
  case ControlKind::Select: {
    const auto& values = *field.optionValues;
    const auto& labels = *field.optionLabels;
    const std::string current = valueAsString(field, effectiveValue(field));
    const auto it = std::ranges::find(values, current);
    const bool known = it != values.end();
    const std::optional<std::size_t> selectedIndex =
        known ? std::optional<std::size_t>{static_cast<std::size_t>(it - values.begin())} : std::nullopt;
    return ui::select({
        .options = labels,
        .selectedIndex = selectedIndex,
        .clearSelection = !known,
        .placeholder = known ? std::optional<std::string>{std::nullopt}
                             : std::optional<std::string>{"Unknown value: " + current},
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .width = kControlWidth * scale,
        .height = Style::controlHeight * scale,
        .onSelectionChanged = [this, field, callbacks, values](std::size_t index, std::string_view /*label*/) {
          if (index < values.size()) {
            stageAndRebuild(field, valueFromString(field, values[index]), callbacks);
          }
        },
    });
  }
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title (+ Advanced badge)
// and caption description on the left; [Override badge][Reset][control] on the
// right. Badges only appear while the value differs from the default.
void WindowBehaviorPage::addSettingRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const bool isOverridden = overridden(field);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  if (field.advanced) {
    titleRow->addChild(makeAdvancedBadge(scale));
  }
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
  if (isOverridden) {
    actions->addChild(makeOverrideBadge(scale));
    actions->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
  }
  actions->addChild(buildControl(field, scale, callbacks));

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

std::unique_ptr<Node> WindowBehaviorPage::build(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  // Section header: glyph + bold primary title (upstream makeSection).
  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = "window",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Window Behavior", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  // Each page group renders as a collapsible card. During a global search the
  // groups flatten to plain headers so every match stays visible (mirrors
  // Noctalia's settings behaviour).
  const bool collapsible = filter.searchQuery.empty();
  if (collapsible && !m_expandedInitialized) {
    m_expandedInitialized = true;
    for (const auto& field : fields()) {
      if (!field.pageGroup.empty()) {
        m_expandedGroups.insert(std::string(field.pageGroup));
        break;
      }
    }
  }

  bool anyRows = false;
  bool firstGroup = true;
  std::string_view lastGroup;
  Flex* currentBody = root.get();
  const auto beginGroup = [&](std::string_view title) {
    if (title.empty()) {
      currentBody = root.get();
      return;
    }
    if (collapsible) {
      currentBody = kinetic_settings::addCollapsibleSection(
          *root, title, title, m_expandedGroups, scale, callbacks.jumpRow
      );
      firstGroup = false;
      return;
    }
    // Search mode: flat section header.
    if (!firstGroup) {
      root->addChild(ui::column(
          {.align = FlexAlign::Stretch,
           .gap = Style::spaceSm * scale,
           .configure = [scale](Flex& flex) { flex.setPadding(Style::spaceSm * scale, 0.0F, 0.0F, 0.0F); }},
          ui::separator(),
          makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
      ));
    } else {
      root->addChild(
          makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
      );
    }
    currentBody = root.get();
    firstGroup = false;
  };

  for (const auto& field : fields()) {
    if (!matchesFilter(field, filter)) {
      continue;
    }
    if (field.pageGroup != lastGroup) {
      lastGroup = field.pageGroup;
      beginGroup(lastGroup);
    }
    addSettingRow(*currentBody, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    // Global search merges hits from every page and drops pages with none, so
    // an empty page must not render its own empty-state notice.
    if (!filter.searchQuery.empty()) {
      return nullptr;
    }
    root->addChild(makeLabel(
        "No settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
