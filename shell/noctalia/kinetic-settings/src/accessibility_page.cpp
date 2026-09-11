#include "accessibility_page.h"

#include "kinetic-settings/src/effects_catalog.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/controls/slider.h"
#include "ui/controls/stepper.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;
  // Dropdown/text width, mirroring the other pages' controls.
  constexpr float kControlWidth = 220.0F;
  // Left-hand subcategory list width (same as the Keybinds component list).
  constexpr float kCategoryListWidth = 200.0F;

  [[nodiscard]] std::string formatDouble(double value, int decimals) {
    switch (decimals) {
    case 0:
      return std::format("{:.0f}", value);
    case 1:
      return std::format("{:.1f}", value);
    case 2:
    default:
      return std::format("{:.2f}", value);
    }
  }

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
  // SettingsControlFactory::makeStatusBadge (same as the other pages).
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
      std::string path, bool pendingConfirmation, float scale, const AccessibilityPage::Callbacks& callbacks
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

AccessibilityPage::AccessibilityPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: the field descriptor table — the single source of truth for file
// keys, defaults, ranges and presentation. Defaults mirror the compositor's
// effect kcfgs (src/plugins/zoom/zoom.kcfg, src/plugins/shakecursor/
// shakecursorconfig.kcfg, src/plugins/magnifier/magnifier.kcfg).
// ---------------------------------------------------------------------------

const std::vector<AccessibilityPage::Category>& AccessibilityPage::categories() {
  static const std::vector<Category> v{Category::ZoomAndMagnifier, Category::ShakePointer};
  return v;
}

std::string_view AccessibilityPage::categoryTitle(Category category) {
  switch (category) {
  case Category::ZoomAndMagnifier:
    return "Zoom & Magnifier";
  case Category::ShakePointer:
    return "Shake Pointer";
  }
  return {};
}

std::string_view AccessibilityPage::categoryDescription(Category category) {
  switch (category) {
  case Category::ZoomAndMagnifier:
    return "Magnify the desktop: the full-screen Zoom effect, or the region Magnifier. "
           "Both are toggled with a shortcut (see Keybinds).";
  case Category::ShakePointer:
    return "Enlarge the pointer when it is quickly shaken back and forth.";
  }
  return {};
}

// Pointer-tracking option values: the exact ints the compositor reads (the
// MouseTrackingType enum in src/plugins/zoom/zoom.h). The list order is the
// display order, not the numeric order.
const std::vector<int>& AccessibilityPage::mouseTrackingValues() {
  static const std::vector<int> v{3, 0, 1, 4, 2}; // Disabled, Proportional, Centered, Centered Strict, Push
  return v;
}
const std::vector<std::string>& AccessibilityPage::mouseTrackingLabels() {
  static const std::vector<std::string> v{"Disabled", "Proportional", "Centered", "Centered Strict", "Push"};
  return v;
}

const std::vector<AccessibilityPage::Field>& AccessibilityPage::fields() {
  static const std::vector<Field> table = [] {
    // Effect enable defaults come from the effect metadata (they are marked
    // EnabledByDefault true upstream), so an omitted [Plugins] key reads
    // correctly.
    const auto enabledDefault = [](std::string_view name) {
      if (const effects_catalog::Effect* effect = effects_catalog::findEffect(name)) {
        return effect->enabledByDefault;
      }
      return true;
    };

    std::vector<Field> f;

    // Zoom & Magnifier ------------------------------------------------------
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Plugins",
        .key = "zoomEnabled",
        .control = ControlKind::Toggle,
        .title = "Enable Zoom",
        .subtitle = "Turn on the full-screen desktop magnifier. Activate it with the "
                    "Zoom shortcuts on the Keybinds page.",
        .fallback = Value{enabledDefault("zoom")},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "ZoomFactor",
        .control = ControlKind::Slider,
        .title = "Zoom factor",
        .subtitle = "How much each zoom step magnifies the desktop.",
        .minValue = 1.0,
        .maxValue = 10.0,
        .step = 0.1,
        .decimals = 2,
        .fallback = Value{1.2},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "MouseTracking",
        .control = ControlKind::Select,
        .title = "Pointer tracking",
        .subtitle = "How the magnified view follows the pointer.",
        .optionValues = &mouseTrackingValues(),
        .optionLabels = &mouseTrackingLabels(),
        .fallback = Value{0},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "EnableFocusTracking",
        .control = ControlKind::Toggle,
        .title = "Track the focused window",
        .subtitle = "Center the magnified view on the window that has keyboard focus.",
        .fallback = Value{false},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "EnableTextCaretTracking",
        .control = ControlKind::Toggle,
        .title = "Track the text caret",
        .subtitle = "Keep the magnified view centered on the text cursor while typing.",
        .fallback = Value{false},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "PixelGridZoom",
        .control = ControlKind::Slider,
        .title = "Show pixel grid at zoom level",
        .subtitle = "Draw a grid between pixels once the zoom level reaches this value.",
        .advanced = true,
        .minValue = 1.0,
        .maxValue = 50.0,
        .step = 1.0,
        .decimals = 1,
        .fallback = Value{15.0},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "UsePatternUpscaler",
        .control = ControlKind::Toggle,
        .title = "Sharpen screen content while zoomed in",
        .subtitle = "Use a pattern upscaler so magnified content stays crisp.",
        .fallback = Value{true},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "PointerAxisGestureModifiers",
        .control = ControlKind::Input,
        .title = "Scroll gesture modifier keys",
        .subtitle = "Hold these modifiers while scrolling to zoom (for example "
                    "\"Meta+Control\"). Accepted keys: Meta, Ctrl/Control, Alt, Shift.",
        .advanced = true,
        .fallback = Value{std::string{"Meta+Control"}},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "FocusDelay",
        .control = ControlKind::Stepper,
        .title = "Focus tracking delay",
        .subtitle = "How long focus must rest in one place before the view follows it.",
        .advanced = true,
        .minValue = 0.0,
        .maxValue = 2000.0,
        .step = 50.0,
        .valueSuffix = "ms",
        .fallback = Value{350},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-zoom",
        .key = "MoveFactor",
        .control = ControlKind::Slider,
        .title = "Move factor",
        .subtitle = "How fast the view scrolls when panning the magnified area.",
        .advanced = true,
        .minValue = 1.0,
        .maxValue = 50.0,
        .step = 1.0,
        .decimals = 1,
        .fallback = Value{20.0},
        .effectName = "zoom",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-magnifier",
        .key = "Width",
        .control = ControlKind::Stepper,
        .title = "Magnify region width",
        .subtitle = "Width of the region magnified by the Magnifier effect.",
        .minValue = 1.0,
        .maxValue = 5000.0,
        .step = 10.0,
        .valueSuffix = "px",
        .fallback = Value{200},
        .effectName = "magnifier",
    });
    f.push_back(Field{
        .category = Category::ZoomAndMagnifier,
        .group = "Effect-magnifier",
        .key = "Height",
        .control = ControlKind::Stepper,
        .title = "Magnify region height",
        .subtitle = "Height of the region magnified by the Magnifier effect.",
        .minValue = 1.0,
        .maxValue = 5000.0,
        .step = 10.0,
        .valueSuffix = "px",
        .fallback = Value{200},
        .effectName = "magnifier",
    });

    // Shake Pointer ---------------------------------------------------------
    f.push_back(Field{
        .category = Category::ShakePointer,
        .group = "Plugins",
        .key = "shakecursorEnabled",
        .control = ControlKind::Toggle,
        .title = "Enable Shake Pointer",
        .subtitle = "Enlarge the pointer when it is shaken quickly from side to side.",
        .fallback = Value{enabledDefault("shakecursor")},
        .effectName = "shakecursor",
    });
    f.push_back(Field{
        .category = Category::ShakePointer,
        .group = "Effect-shakecursor",
        .key = "Sensitivity",
        .control = ControlKind::Slider,
        .title = "Sensitivity",
        .subtitle = "How eagerly a shake is detected. Higher values react to smaller "
                    "movements.",
        .minValue = 1.0,
        .maxValue = 10.0,
        .step = 0.5,
        .decimals = 1,
        .fallback = Value{4.0},
        .effectName = "shakecursor",
    });
    f.push_back(Field{
        .category = Category::ShakePointer,
        .group = "Effect-shakecursor",
        .key = "Magnification",
        .control = ControlKind::Slider,
        .title = "Magnification",
        .subtitle = "How large the pointer grows while the shake is active.",
        .minValue = 1.0,
        .maxValue = 10.0,
        .step = 0.5,
        .decimals = 1,
        .fallback = Value{3.0},
        .effectName = "shakecursor",
    });
    f.push_back(Field{
        .category = Category::ShakePointer,
        .group = "Effect-shakecursor",
        .key = "OverMagnification",
        .control = ControlKind::Slider,
        .title = "Over magnification",
        .subtitle = "Extra growth applied on top of the magnification while shaking.",
        .advanced = true,
        .minValue = 1.0,
        .maxValue = 10.0,
        .step = 0.5,
        .decimals = 1,
        .fallback = Value{1.0},
        .effectName = "shakecursor",
    });
    f.push_back(Field{
        .category = Category::ShakePointer,
        .group = "Effect-shakecursor",
        .key = "TimeInterval",
        .control = ControlKind::Stepper,
        .title = "Time interval",
        .subtitle = "The window of time a shake must happen within to be detected.",
        .advanced = true,
        .minValue = 100.0,
        .maxValue = 5000.0,
        .step = 100.0,
        .valueSuffix = "ms",
        .fallback = Value{1000},
        .effectName = "shakecursor",
    });

    return f;
  }();
  return table;
}

// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string AccessibilityPage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const AccessibilityPage::Field* AccessibilityPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

AccessibilityPage::Value AccessibilityPage::defaultValue(const Field& field) {
  return field.fallback;
}

bool AccessibilityPage::valuesEqual(const Value& a, const Value& b) {
  return a.index() == b.index() && a == b;
}

bool AccessibilityPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(value, defaultValue(field));
}

void AccessibilityPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

AccessibilityPage::Value AccessibilityPage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else if constexpr (std::is_same_v<T, int>) {
          const int parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, static_cast<int>(field.minValue), static_cast<int>(field.maxValue));
        } else if constexpr (std::is_same_v<T, double>) {
          const double parsed = m_config.readDouble(field.group, field.key, fallbackValue);
          return std::clamp(parsed, field.minValue, field.maxValue);
        } else {
          return m_config.readString(field.group, field.key).value_or(fallbackValue);
        }
      },
      fallback
  );
}

AccessibilityPage::Value AccessibilityPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool AccessibilityPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool AccessibilityPage::matchesFilter(const Field& field, const Filter& filter) const {
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
    if (!matches(field.title) && !matches(field.subtitle) && !matches(categoryTitle(field.category))) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the other pages' override semantics.
// ---------------------------------------------------------------------------

void AccessibilityPage::writeValue(const Field& field, const Value& value) {
  if (equalsDefault(field, value)) {
    m_config.removeKey(field.group, field.key);
    return;
  }
  std::visit(
      [this, &field](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          m_config.setBool(field.group, field.key, v);
        } else if constexpr (std::is_same_v<T, int>) {
          m_config.setInt(field.group, field.key, v);
        } else if constexpr (std::is_same_v<T, double>) {
          m_config.setDouble(field.group, field.key, v);
        } else {
          m_config.setString(field.group, field.key, v);
        }
      },
      value
  );
}

void AccessibilityPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void AccessibilityPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> AccessibilityPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void AccessibilityPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void AccessibilityPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
}

bool AccessibilityPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  // [Plugins] enable toggles drive loadEffect/unloadEffect; every other field
  // belongs to an [Effect-*] group that reconfigureEffect() re-reads.
  std::map<std::string, bool> enableChanges;
  std::set<std::string> touchedEffects;
  for (const auto& [path, value] : m_pending) {
    const Field* field = findField(path);
    if (field == nullptr) {
      continue;
    }
    writeValue(*field, value);
    if (!field->effectName.empty()) {
      touchedEffects.insert(std::string(field->effectName));
    }
    if (field->group == "Plugins") {
      enableChanges[std::string(field->effectName)] = std::get<bool>(value);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  for (const auto& [name, enabled] : enableChanges) {
    if (enabled) {
      m_dbus.loadEffect(name);
    } else {
      m_dbus.unloadEffect(name);
    }
  }
  // reconfigureEffect() reparses kineticwe.kwe and re-applies the effect's
  // [Effect-*] group, so it must run after any load/unload.
  for (const auto& name : touchedEffects) {
    m_dbus.reconfigureEffect(name);
  }

  m_pending.clear();
  return true;
}

// ---------------------------------------------------------------------------
// Controls (Noctalia settings visuals, re-implemented module-locally).
// ---------------------------------------------------------------------------

std::unique_ptr<Node> AccessibilityPage::buildControl(
    const Field& field, float scale, const Callbacks& callbacks
) {
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
    const int minValue = static_cast<int>(field.minValue);
    const int maxValue = static_cast<int>(field.maxValue);
    const int value = std::get<int>(effectiveValue(field));
    return ui::stepper({
        .minValue = minValue,
        .maxValue = maxValue,
        .step = static_cast<int>(field.step),
        .value = std::clamp(value, minValue, maxValue),
        .scale = scale,
        .valueSuffix = std::string(field.valueSuffix),
        .onValueCommitted = [this, field, minValue, maxValue, callbacks](int committed) {
          stageAndRebuild(field, Value{std::clamp(committed, minValue, maxValue)}, callbacks);
        },
    });
  }
  case ControlKind::Slider: {
    const double value = std::get<double>(effectiveValue(field));
    const double clamped = std::clamp(value, field.minValue, field.maxValue);
    Slider* sliderPtr = nullptr;
    Label* valuePtr = nullptr;
    auto slider = ui::slider({
        .out = &sliderPtr,
        .minValue = field.minValue,
        .maxValue = field.maxValue,
        .step = field.step,
        .value = clamped,
        .trackHeight = Style::sliderTrackHeight * scale,
        .thumbSize = Style::sliderThumbSize * scale,
        .controlHeight = Style::controlHeight * scale,
        .width = Style::sliderDefaultWidth * scale,
    });
    auto valueLabel = ui::label({
        .out = &valuePtr,
        .text = formatDouble(clamped, field.decimals),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .minWidth = 52.0F * scale,
        .textAlign = TextAlign::End,
    });
    slider->setOnValueChanged([valuePtr, decimals = field.decimals](double next) {
      if (valuePtr != nullptr) {
        valuePtr->setText(formatDouble(next, decimals));
      }
    });
    slider->setOnDragEnd([this, field, sliderPtr, callbacks]() {
      if (sliderPtr != nullptr) {
        stageAndRebuild(field, Value{sliderPtr->value()}, callbacks);
      }
    });
    return ui::row(
        {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
        std::move(slider), std::move(valueLabel)
    );
  }
  case ControlKind::Select: {
    const auto& values = *field.optionValues;
    const auto& labels = *field.optionLabels;
    const int current = std::get<int>(effectiveValue(field));
    const auto it = std::ranges::find(values, current);
    const bool known = it != values.end();
    const std::optional<std::size_t> selectedIndex =
        known ? std::optional<std::size_t>{static_cast<std::size_t>(it - values.begin())} : std::nullopt;
    return ui::select({
        .options = labels,
        .selectedIndex = selectedIndex,
        .clearSelection = !known,
        .placeholder = known ? std::optional<std::string>{std::nullopt}
                             : std::optional<std::string>{"Unknown value: " + std::to_string(current)},
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .width = kControlWidth * scale,
        .height = Style::controlHeight * scale,
        .onSelectionChanged = [this, field, callbacks, values](std::size_t index, std::string_view /*label*/) {
          if (index < values.size()) {
            stageAndRebuild(field, Value{values[index]}, callbacks);
          }
        },
    });
  }
  case ControlKind::Input: {
    const std::string value = std::get<std::string>(effectiveValue(field));
    return ui::input({
        .value = value,
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .horizontalPadding = Style::spaceSm * scale,
        .width = kControlWidth * scale,
        .height = Style::controlHeight * scale,
        .onSubmit = [this, field, callbacks](const std::string& text) {
          stageAndRebuild(field, Value{StringUtils::trim(text)}, callbacks);
        },
        .submitOnFocusLoss = true,
    });
  }
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title (+ Advanced badge)
// and caption description on the left; [Override badge][Reset][control] on the
// right. Badges only appear while the value differs from the default.
void AccessibilityPage::addSettingRow(
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

std::unique_ptr<Node> AccessibilityPage::buildCategoryList(float scale, const Callbacks& callbacks) {
  auto list = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceXs * scale,
      .width = kCategoryListWidth * scale,
  });
  for (const Category category : categories()) {
    const bool selected = category == m_selectedCategory;
    list->addChild(ui::button({
        .text = std::string(categoryTitle(category)),
        .fontSize = Style::fontSizeCaption * scale,
        .contentAlign = ButtonContentAlign::Start,
        .variant = selected ? ButtonVariant::TabActive : ButtonVariant::Tab,
        .minHeight = Style::controlHeight * scale,
        .paddingV = Style::spaceXs * scale,
        .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick =
            [this, category, callbacks]() {
              m_selectedCategory = category;
              if (callbacks.requestRebuild) {
                callbacks.requestRebuild();
              }
            },
        .configure = [selected](Button& button) {
          if (selected && button.label() != nullptr) {
            button.label()->setFontWeight(FontWeight::Bold);
          }
          button.setTabStop(false);
        },
    }));
  }
  return list;
}

std::unique_ptr<Node> AccessibilityPage::build(
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
          .glyph = "accessible",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Accessibility", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  // Global search: the two-pane layout only surfaces matches in the selected
  // subcategory, so instead scan every subcategory and list its matching rows.
  if (!filter.searchQuery.empty()) {
    bool anyRows = false;
    for (const Category category : categories()) {
      std::vector<const Field*> matching;
      for (const auto& field : fields()) {
        if (field.category == category && matchesFilter(field, filter)) {
          matching.push_back(&field);
        }
      }
      if (matching.empty()) {
        continue;
      }
      if (anyRows) {
        root->addChild(ui::separator());
      }
      root->addChild(makeLabel(
          categoryTitle(category), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary),
          FontWeight::Bold
      ));
      for (const Field* field : matching) {
        addSettingRow(*root, *field, scale, armedResetKey, callbacks);
      }
      anyRows = true;
    }
    if (!anyRows) {
      // Global search drops pages with no hits, so don't render the notice.
      return nullptr;
    }
    return root;
  }

  // Subcategory list on the left, settings on the right (Keybinds layout).
  auto body = ui::row({
      .align = FlexAlign::Start,
      .gap = Style::spaceMd * scale,
      .fillWidth = true,
  });
  body->addChild(buildCategoryList(scale, callbacks));
  body->addChild(ui::separator({
      .thickness = 1.0F,
      .spacing = Style::spaceXs * scale,
      .orientation = SeparatorOrientation::VerticalRule,
  }));

  auto settings = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .flexGrow = 1.0F,
  });
  settings->addChild(makeLabel(
      categoryTitle(m_selectedCategory), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary),
      FontWeight::Bold
  ));
  if (!categoryDescription(m_selectedCategory).empty()) {
    settings->addChild(ui::label({
        .text = std::string(categoryDescription(m_selectedCategory)),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = kSettingDescriptionMaxLines,
    }));
  }

  bool anyRows = false;
  for (const auto& field : fields()) {
    if (field.category != m_selectedCategory) {
      continue;
    }
    if (!matchesFilter(field, filter)) {
      continue;
    }
    addSettingRow(*settings, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }
  if (!anyRows) {
    settings->addChild(makeLabel(
        "No settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  body->addChild(std::move(settings));
  root->addChild(std::move(body));
  return root;
}
