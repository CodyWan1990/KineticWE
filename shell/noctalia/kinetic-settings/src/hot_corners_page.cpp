#include "hot_corners_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
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
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;
  // Wider than the Window Behavior dropdowns: several action labels are long.
  constexpr float kControlWidth = 280.0F;

  constexpr std::string_view kNoAction = "None";

  // The action catalog. Ids are this page's canonical names for the actions
  // KWin exposes across the built-in borders and the effect border providers.
  struct ActionDef {
    std::string_view id;
    std::string_view label;
  };

  const std::vector<ActionDef>& actionCatalog() {
    static const std::vector<ActionDef> table{
        {"None", "No action"},
        {"ShowDesktop", "Show desktop"},
        {"LockScreen", "Lock screen"},
        {"KRunner", "Show KRunner"},
        {"ActivityManager", "Activity manager"},
        {"ApplicationLauncher", "Application launcher"},
        {"WindowViewAll", "Present windows (all desktops)"},
        {"WindowViewCurrent", "Present windows (current desktop)"},
        {"WindowViewClass", "Present windows (current application)"},
        {"Overview", "Overview"},
        {"Grid", "Grid"},
        {"TabBox", "Toggle window switching"},
        {"TabBoxAlternative", "Toggle alternative window switching"},
    };
    return table;
  }

  const std::vector<std::string>& actionIds() {
    static const std::vector<std::string> ids = [] {
      std::vector<std::string> out;
      out.reserve(actionCatalog().size());
      for (const auto& action : actionCatalog()) {
        out.emplace_back(action.id);
      }
      return out;
    }();
    return ids;
  }

  const std::vector<std::string>& actionLabels() {
    static const std::vector<std::string> labels = [] {
      std::vector<std::string> out;
      out.reserve(actionCatalog().size());
      for (const auto& action : actionCatalog()) {
        out.emplace_back(action.label);
      }
      return out;
    }();
    return labels;
  }

  // A KWin border provider: an IntList in the compositor config. The list order
  // is the KCM's load order — the last provider holding a corner wins.
  struct ProviderDef {
    std::string_view group;
    std::string_view key;
    std::string_view actionId;
    const std::vector<std::string>* defaultList = nullptr;
  };

  const std::vector<std::string>& emptyList() {
    static const std::vector<std::string> value;
    return value;
  }
  const std::vector<std::string>& topLeftList() {
    static const std::vector<std::string> value{"7"};
    return value;
  }
  const std::vector<std::string>& electricNoneList() {
    static const std::vector<std::string> value{"9"};
    return value;
  }

  const std::vector<ProviderDef>& providers() {
    static const std::vector<ProviderDef> table{
        {"Effect-windowview", "BorderActivateAll", "WindowViewAll", &emptyList()},
        {"Effect-windowview", "BorderActivate", "WindowViewCurrent", &emptyList()},
        {"Effect-windowview", "BorderActivateClass", "WindowViewClass", &emptyList()},
        {"Effect-overview", "BorderActivate", "Overview", &topLeftList()},
        {"Effect-overview", "GridBorderActivate", "Grid", &emptyList()},
        {"TabBox", "BorderActivate", "TabBox", &electricNoneList()},
        {"TabBox", "BorderAlternativeActivate", "TabBoxAlternative", &electricNoneList()},
    };
    return table;
  }

  // Built-in [ElectricBorders] actions; everything else is a provider action
  // and leaves the built-in key at None.
  bool isBuiltinAction(std::string_view id) {
    return id == "ShowDesktop"
        || id == "LockScreen"
        || id == "KRunner"
        || id == "ActivityManager"
        || id == "ApplicationLauncher";
  }

  std::string builtinActionId(std::string_view stored) {
    for (const std::string_view id :
         {"ShowDesktop", "LockScreen", "KRunner", "ActivityManager", "ApplicationLauncher"}) {
      if (StringUtils::equalsInsensitive(stored, id)) {
        return std::string(id);
      }
    }
    return std::string(kNoAction);
  }

  // ElectricBorder values (src/screenedge.h).
  constexpr int kElectricNone = 9;

  // Accepts a numeric entry or a KConfig enum name; -1 when unusable.
  int parseBorder(std::string_view entry) {
    const std::string trimmed = StringUtils::trim(entry);
    if (trimmed.empty()) {
      return -1;
    }
    struct NamedBorder {
      std::string_view name;
      int value;
    };
    static constexpr NamedBorder kNames[] = {
        {"ElectricTop", 0},         {"ElectricTopRight", 1}, {"ElectricRight", 2},
        {"ElectricBottomRight", 3}, {"ElectricBottom", 4},   {"ElectricBottomLeft", 5},
        {"ElectricLeft", 6},        {"ElectricTopLeft", 7},  {"ElectricNone", 9},
    };
    for (const auto& named : kNames) {
      if (StringUtils::equalsInsensitive(trimmed, named.name)) {
        return named.value;
      }
    }
    int value = 0;
    const auto [end, ec] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), value);
    if (ec == std::errc() && end == trimmed.data() + trimmed.size() && value >= 0 && value <= kElectricNone) {
      return value;
    }
    return -1;
  }

  // Canonical decimal entries, invalid items dropped, duplicates collapsed.
  std::vector<std::string> sanitizeBorderList(const std::vector<std::string>& raw) {
    std::vector<std::string> out;
    out.reserve(raw.size());
    for (const auto& entry : raw) {
      const int border = parseBorder(entry);
      if (border < 0) {
        continue;
      }
      const std::string encoded = std::to_string(border);
      if (std::ranges::find(out, encoded) == out.end()) {
        out.push_back(encoded);
      }
    }
    return out;
  }

  bool listHasBorder(const std::vector<std::string>& list, int border) {
    return std::ranges::find(list, std::to_string(border)) != list.end();
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

  // Status pill ("Override"); a module-local re-implementation of
  // SettingsControlFactory::makeStatusBadge (same as the Window Behavior page).
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
        "Override", colorSpecFromRole(ColorRole::Primary, 0.15F), colorSpecFromRole(ColorRole::Primary), scale
    );
  }

  // The two-stage Reset action shown next to overridden settings: the first
  // click arms it, the second confirms.
  [[nodiscard]] std::unique_ptr<Button>
  makeResetButton(std::string path, bool pendingConfirmation, float scale, const HotCornersPage::Callbacks& callbacks) {
    return ui::button({
        .text = pendingConfirmation ? "Confirm reset" : "Reset",
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

HotCornersPage::HotCornersPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: the field descriptor table — the single source of truth for file
// keys, defaults, ranges and presentation. Defaults mirror the compiled-in
// ones in src/kcms/screenedges/kwinscreenedgesettings.kcfg and the effect
// configs (src/plugins/overview/overviewconfig.kcfg,
// src/plugins/windowview/windowviewconfig.kcfg).
// ---------------------------------------------------------------------------

const std::vector<HotCornersPage::Field>& HotCornersPage::fields() {
  static const std::vector<Field> table = [] {
    std::vector<Field> f;
    // Hot Corners -----------------------------------------------------------
    f.push_back(
        Field{
            .group = "ScreenEdges",
            .key = "HotCornersEnabled",
            .title = "Enable hot corners",
            .subtitle = "Switches all four corner triggers off without losing the "
                        "actions configured below",
            .pageGroup = "Hot Corners",
            .fallback = Value{true},
        }
    );
    f.push_back(
        Field{
            .group = "Windows",
            .key = "ElectricBorderDelay",
            .control = ControlKind::Stepper,
            .title = "Activation delay",
            .subtitle = "How long the pointer has to rest in a corner before its "
                        "action triggers",
            .pageGroup = "Hot Corners",
            .minValue = 0,
            .maxValue = 1000,
            .step = 50,
            .valueSuffix = " ms",
            .fallback = Value{75},
        }
    );
    f.push_back(
        Field{
            .group = "Windows",
            .key = "ElectricBorderCooldown",
            .control = ControlKind::Stepper,
            .title = "Reactivation delay",
            .subtitle = "How long a corner stays inactive after it has triggered, so "
                        "the same action does not repeat",
            .pageGroup = "Hot Corners",
            .minValue = 0,
            .maxValue = 1000,
            .step = 50,
            .valueSuffix = " ms",
            .fallback = Value{350},
        }
    );
    // Corner Actions --------------------------------------------------------
    f.push_back(
        Field{
            .group = "ElectricBorders",
            .key = "TopLeft",
            .control = ControlKind::Corner,
            .title = "Top left corner",
            .subtitle = "Action when the pointer hits the top left corner.",
            .pageGroup = "Corner Actions",
            .fallback = Value{std::string{"Overview"}},
            .border = 7,
        }
    );
    f.push_back(
        Field{
            .group = "ElectricBorders",
            .key = "TopRight",
            .control = ControlKind::Corner,
            .title = "Top right corner",
            .subtitle = "Action when the pointer hits the top right corner.",
            .pageGroup = "Corner Actions",
            .fallback = Value{std::string{"None"}},
            .border = 1,
        }
    );
    f.push_back(
        Field{
            .group = "ElectricBorders",
            .key = "BottomLeft",
            .control = ControlKind::Corner,
            .title = "Bottom left corner",
            .subtitle = "Action when the pointer hits the bottom left corner.",
            .pageGroup = "Corner Actions",
            .fallback = Value{std::string{"None"}},
            .border = 5,
        }
    );
    f.push_back(
        Field{
            .group = "ElectricBorders",
            .key = "BottomRight",
            .control = ControlKind::Corner,
            .title = "Bottom right corner",
            .subtitle = "Action when the pointer hits the bottom right corner.",
            .pageGroup = "Corner Actions",
            .fallback = Value{std::string{"None"}},
            .border = 3,
        }
    );
    // Behavior --------------------------------------------------------------
    f.push_back(
        Field{
            .group = "Windows",
            .key = "ElectricBorderCornerRatio",
            .control = ControlKind::PercentStepper,
            .title = "Trigger quarter tiling in",
            .subtitle = "How far from the top and bottom edges a dragged window "
                        "counts as being in a screen corner, as a percentage of "
                        "the screen height",
            .pageGroup = "Behavior",
            .minValue = 1,
            .maxValue = 49,
            .step = 1,
            .valueSuffix = " %",
            .fallback = Value{0.25},
        }
    );
    f.push_back(
        Field{
            .group = "ScreenEdges",
            .key = "RemainActiveOnFullscreen",
            .title = "Remain active when windows are fullscreen",
            .subtitle = "Corners keep working while a fullscreen window covers the screen",
            .pageGroup = "Behavior",
            .fallback = Value{false},
        }
    );
    f.push_back(
        Field{
            .group = "Windows",
            .key = "ElectricBorderAllScreenCorner",
            .title = "All screen corners",
            .subtitle = "Create a corner at every screen instead of only at the "
                        "overall display boundary",
            .pageGroup = "Behavior",
            .fallback = Value{true},
        }
    );
    // Barriers --------------------------------------------------------------
    f.push_back(
        Field{
            .group = "EdgeBarrier",
            .key = "CornerBarrier",
            .title = "Corner barrier",
            .subtitle = "Stops the pointer from crossing to another screen when it is "
                        "already at a screen corner",
            .pageGroup = "Barriers",
            .fallback = Value{true},
        }
    );
    f.push_back(
        Field{
            .group = "EdgeBarrier",
            .key = "EdgeBarrier",
            .control = ControlKind::Stepper,
            .title = "Edge barrier",
            .subtitle = "Extra distance the pointer has to travel to cross the edge "
                        "between two screens. 0 turns the barrier off",
            .pageGroup = "Barriers",
            .minValue = 0,
            .maxValue = 1000,
            .step = 10,
            .valueSuffix = " px",
            .fallback = Value{0},
        }
    );
    return f;
  }();
  return table;
}

// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string HotCornersPage::keyPath(const Field& field) { return joinKeyPath(field.group, field.key); }

const HotCornersPage::Field* HotCornersPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

HotCornersPage::Value HotCornersPage::defaultValue(const Field& field) { return field.fallback; }

bool HotCornersPage::valuesEqual(const Field& /*field*/, const Value& a, const Value& b) {
  return a.index() == b.index() && a == b;
}

bool HotCornersPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(field, value, defaultValue(field));
}

void HotCornersPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

HotCornersPage::Value HotCornersPage::fileBackedValue(const Field& field) const {
  if (isCornerRow(field)) {
    return Value{cornerAction(field)};
  }
  if (field.control == ControlKind::PercentStepper) {
    const double fallback = std::get<double>(field.fallback);
    const double value = m_config.readDouble(field.group, field.key, fallback);
    const double minimum = static_cast<double>(field.minValue) / 100.0;
    const double maximum = static_cast<double>(field.maxValue) / 100.0;
    return Value{std::clamp(value, minimum, maximum)};
  }

  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else if constexpr (std::is_same_v<T, int>) {
          const auto parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, field.minValue, field.maxValue);
        } else if constexpr (std::is_same_v<T, double>) {
          const auto parsed = m_config.readDouble(field.group, field.key, fallbackValue);
          return std::clamp(parsed, static_cast<double>(field.minValue), static_cast<double>(field.maxValue));
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
          return m_config.readStringList(field.group, field.key);
        } else {
          return m_config.readString(field.group, field.key).value_or(fallbackValue);
        }
      },
      fallback
  );
}

HotCornersPage::Value HotCornersPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool HotCornersPage::overridden(const Field& field) const { return !equalsDefault(field, effectiveValue(field)); }

bool HotCornersPage::matchesFilter(const Field& field, const Filter& filter) const {
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

bool HotCornersPage::hotCornersEnabled() const {
  const Field* field = findField("ScreenEdges.HotCornersEnabled");
  return field != nullptr && std::get<bool>(effectiveValue(*field));
}

// ---------------------------------------------------------------------------
// Corner actions: one logical value stored across the built-in border entry and
// the effect border-provider lists.
// ---------------------------------------------------------------------------

std::string HotCornersPage::cornerAction(const Field& field) const {
  std::string actionId(kNoAction);
  if (const auto stored = m_config.readString(field.group, field.key); stored.has_value()) {
    actionId = builtinActionId(*stored);
  }
  for (const auto& provider : providers()) {
    std::vector<std::string> list;
    if (m_config.hasKey(provider.group, provider.key)) {
      list = sanitizeBorderList(m_config.readStringList(provider.group, provider.key));
    } else {
      list = *provider.defaultList;
    }
    if (listHasBorder(list, field.border)) {
      actionId = std::string(provider.actionId);
    }
  }
  return actionId;
}

void HotCornersPage::writeCorner(const Field& field, std::string_view actionId) {
  const std::string border = std::to_string(field.border);

  // Remove the corner from every provider first, then add it to the chosen one.
  std::vector<std::pair<const ProviderDef*, std::vector<std::string>>> lists;
  lists.reserve(providers().size());
  for (const auto& provider : providers()) {
    std::vector<std::string> list;
    if (m_config.hasKey(provider.group, provider.key)) {
      list = sanitizeBorderList(m_config.readStringList(provider.group, provider.key));
    } else {
      list = *provider.defaultList;
    }
    std::erase(list, border);
    lists.emplace_back(&provider, std::move(list));
  }
  for (auto& [provider, list] : lists) {
    if (provider->actionId == actionId && std::ranges::find(list, border) == list.end()) {
      list.push_back(border);
    }
  }

  for (const auto& [provider, list] : lists) {
    // A non-empty list must not keep the ElectricNone sentinel next to a real
    // border. When the provider's compiled default is non-empty, an empty list
    // is stored as ElectricNone so that default cannot reactivate the corner.
    std::vector<std::string> normalized = list;
    if (normalized.size() > 1) {
      std::erase(normalized, std::string("9"));
    }
    if (normalized.empty() && !provider->defaultList->empty()) {
      normalized.emplace_back("9");
    }
    if (normalized == *provider->defaultList) {
      m_config.removeKey(provider->group, provider->key);
    } else {
      m_config.setStringList(provider->group, provider->key, normalized);
    }
  }

  // Built-in [ElectricBorders] entry: a provider action leaves it at None.
  if (isBuiltinAction(actionId)) {
    m_config.setString(field.group, field.key, actionId);
  } else {
    m_config.removeKey(field.group, field.key);
  }
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the Window Behavior page's override semantics.
// ---------------------------------------------------------------------------

void HotCornersPage::writeValue(const Field& field, const Value& value) {
  if (isCornerRow(field)) {
    writeCorner(field, std::get<std::string>(value));
    return;
  }
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
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
          m_config.setStringList(field.group, field.key, v);
        } else {
          m_config.setString(field.group, field.key, v);
        }
      },
      value
  );
}

void HotCornersPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(field, value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void HotCornersPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> HotCornersPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void HotCornersPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void HotCornersPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
}

bool HotCornersPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  // The effect border lists update live through reconfigureEffect(); the
  // built-in [ElectricBorders] and TabBox entries follow the full reconfigure.
  bool overviewChanged = false;
  bool windowViewChanged = false;
  for (const auto& [path, value] : m_pending) {
    const Field* field = findField(path);
    if (field == nullptr) {
      continue;
    }
    if (isCornerRow(*field)) {
      // writeCorner() rewrites every provider list.
      overviewChanged = true;
      windowViewChanged = true;
    } else if (field->group == "Effect-overview") {
      overviewChanged = true;
    } else if (field->group == "Effect-windowview") {
      windowViewChanged = true;
    }
    writeValue(*field, value);
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    m_errorNotice = "Failed to write kineticwe.kwe. Your changes were kept; try Apply again.";
    return false;
  }

  m_pending.clear();
  m_errorNotice.clear();
  m_dbus.requestReconfigure();
  if (overviewChanged) {
    m_dbus.reconfigureEffect("overview");
  }
  if (windowViewChanged) {
    m_dbus.reconfigureEffect("windowview");
  }
  return true;
}

// ---------------------------------------------------------------------------
// Controls (Noctalia settings visuals, re-implemented module-locally).
// ---------------------------------------------------------------------------

std::unique_ptr<Node> HotCornersPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  switch (field.control) {
  case ControlKind::Toggle: {
    const bool checked = std::get<bool>(effectiveValue(field));
    return ui::toggle({
        .checked = checked,
        .scale = scale,
        .onChange = [this, field, callbacks](bool value) { stageAndRebuild(field, Value{value}, callbacks); },
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
  case ControlKind::PercentStepper: {
    const double stored = std::get<double>(effectiveValue(field));
    const int percent = std::clamp(static_cast<int>(std::lround(stored * 100.0)), field.minValue, field.maxValue);
    return ui::stepper({
        .minValue = field.minValue,
        .maxValue = field.maxValue,
        .step = field.step,
        .value = percent,
        .scale = scale,
        .valueSuffix = std::string(field.valueSuffix),
        .onValueCommitted = [this, field, callbacks](int committed) {
          const int clamped = std::clamp(committed, field.minValue, field.maxValue);
          stageAndRebuild(field, Value{static_cast<double>(clamped) / 100.0}, callbacks);
        },
    });
  }
  case ControlKind::Corner: {
    const auto& values = actionIds();
    const auto& labels = actionLabels();
    const std::string current = std::get<std::string>(effectiveValue(field));
    const auto it = std::ranges::find(values, current);
    const bool known = it != values.end();
    const std::optional<std::size_t> selectedIndex =
        known ? std::optional<std::size_t>{static_cast<std::size_t>(it - values.begin())} : std::nullopt;
    return ui::select({
        .options = labels,
        .selectedIndex = selectedIndex,
        .clearSelection = !known,
        .placeholder =
            known ? std::optional<std::string>{std::nullopt} : std::optional<std::string>{"Unknown value: " + current},
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .width = kControlWidth * scale,
        .height = Style::controlHeight * scale,
        .onSelectionChanged = [this, field, callbacks](std::size_t index, std::string_view /*label*/) {
          if (index < actionIds().size()) {
            stageAndRebuild(field, Value{actionIds()[index]}, callbacks);
          }
        },
    });
  }
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title and caption
// description on the left; [Override badge][Reset][control] on the right.
// Badges only appear while the value differs from the default.
void HotCornersPage::addSettingRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const bool isOverridden = overridden(field);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(
      makeLabel(field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
  );
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!field.subtitle.empty()) {
    copy->addChild(
        ui::label({
            .text = std::string(field.subtitle),
            .fontSize = Style::fontSizeCaption * scale,
            .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
            .maxLines = kSettingDescriptionMaxLines,
        })
    );
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

std::unique_ptr<Node>
HotCornersPage::build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  if (!m_errorNotice.empty()) {
    root->addChild(
        ui::row(
            {.align = FlexAlign::Center,
             .gap = Style::spaceSm * scale,
             .paddingV = Style::spaceXs * scale,
             .paddingH = Style::spaceMd * scale,
             .fill = colorSpecFromRole(ColorRole::Error, 0.15F),
             .radius = Style::scaledRadiusMd(scale)},
            makeLabel(m_errorNotice, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::Error))
        )
    );
  }

  // Section header: glyph + bold primary title (upstream makeSection).
  root->addChild(
      ui::row(
          {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
          ui::glyph({
              .glyph = "border-corners",
              .glyphSize = Style::fontSizeHeader * scale,
              .color = colorSpecFromRole(ColorRole::Primary),
          }),
          makeLabel(
              "Hot Corners", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
          )
      )
  );
  root->addChild(
      ui::label({
          .text = "Trigger an action by pushing the mouse pointer into a screen corner. "
                  "Choose No action to turn a corner off.",
          .fontSize = Style::fontSizeBody * scale,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = kSettingDescriptionMaxLines,
      })
  );

  // Each page group renders as a collapsible card. During a global search the
  // groups flatten to plain headers so every match stays visible (mirrors
  // Noctalia's settings behaviour).
  const bool collapsible = filter.searchQuery.empty();
  if (collapsible && !m_expandedInitialized) {
    m_expandedInitialized = true;
    for (const auto& field : fields()) {
      m_expandedGroups.insert(std::string(field.pageGroup));
    }
  }

  bool anyRows = false;
  bool firstGroup = true;
  Flex* currentBody = root.get();
  const auto beginGroup = [&](std::string_view title) {
    if (collapsible) {
      currentBody =
          kinetic_settings::addCollapsibleSection(*root, title, title, m_expandedGroups, scale, callbacks.jumpRow);
      firstGroup = false;
      return;
    }
    // Search mode: flat section header.
    if (!firstGroup) {
      root->addChild(
          ui::column(
              {.align = FlexAlign::Stretch,
               .gap = Style::spaceSm * scale,
               .configure = [scale](Flex& flex) { flex.setPadding(Style::spaceSm * scale, 0.0F, 0.0F, 0.0F); }},
              ui::separator(),
              makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
          )
      );
    } else {
      root->addChild(
          makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
      );
    }
    currentBody = root.get();
    firstGroup = false;
  };

  const bool cornersEnabled = hotCornersEnabled();
  const auto& table = fields();
  std::size_t i = 0;
  while (i < table.size()) {
    const std::string_view group = table[i].pageGroup;
    std::size_t end = i;
    while (end < table.size() && table[end].pageGroup == group) {
      ++end;
    }

    bool groupStarted = false;
    for (std::size_t j = i; j < end; ++j) {
      const Field& field = table[j];
      if (!matchesFilter(field, filter)) {
        continue;
      }
      if (!groupStarted) {
        groupStarted = true;
        beginGroup(group);
        if (collapsible && group == "Corner Actions" && !cornersEnabled) {
          currentBody->addChild(
              ui::row(
                  {.align = FlexAlign::Center,
                   .gap = Style::spaceSm * scale,
                   .paddingV = Style::spaceXs * scale,
                   .paddingH = Style::spaceMd * scale,
                   .fill = colorSpecFromRole(ColorRole::OnSurfaceVariant, 0.12F),
                   .radius = Style::scaledRadiusMd(scale)},
                  makeLabel(
                      "Hot corners are disabled. The configured actions below are kept.",
                      Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
                  )
              )
          );
        }
      }
      addSettingRow(*currentBody, field, scale, armedResetKey, callbacks);
      anyRows = true;
    }
    i = end;
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
