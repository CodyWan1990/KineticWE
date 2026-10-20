#include "blur_page.h"

#include "kinetic-settings/src/effects_catalog.h"
#include "kinetic-settings/src/section_card.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
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
      std::string path, bool pendingConfirmation, float scale, const BlurPage::Callbacks& callbacks
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

BlurPage::BlurPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: the field descriptor table — the single source of truth for file
// keys, defaults, ranges and presentation. Defaults mirror the compositor's
// effect kcfg (src/plugins/blur/blur.kcfg).
// ---------------------------------------------------------------------------

const std::vector<BlurPage::Field>& BlurPage::fields() {
  static const std::vector<Field> table = [] {
    // The effect enable default comes from the effect metadata (blur is marked
    // EnabledByDefault true), so an omitted [Plugins] key reads correctly.
    const auto enabledDefault = [](std::string_view name) {
      if (const effects_catalog::Effect* effect = effects_catalog::findEffect(name)) {
        return effect->enabledByDefault;
      }
      return true;
    };

    std::vector<Field> f;

    f.push_back(Field{
        .group = "Plugins",
        .key = "blurEnabled",
        .control = ControlKind::Toggle,
        .title = "Enable blur",
        .subtitle = "Blur the area behind translucent windows using the compositor's blur effect.",
        .fallback = Value{enabledDefault("blur")},
        .effectName = "blur",
    });
    f.push_back(Field{
        .group = "Effect-blur",
        .key = "ForceBlurAll",
        .control = ControlKind::Toggle,
        .title = "Blur all windows",
        .subtitle = "Force blur behind every application window even if it does not request it. "
                    "Windows must be translucent for the blur to show — lower Active/Inactive opacity "
                    "in Window Behavior to see it.",
        .fallback = Value{false},
        .effectName = "blur",
    });
    f.push_back(Field{
        .group = "Effect-blur",
        .key = "BlurStrength",
        .control = ControlKind::Slider,
        .title = "Blur strength",
        .subtitle = "How strong the blur is behind translucent windows.",
        .minValue = 1.0,
        .maxValue = 15.0,
        .step = 1.0,
        .decimals = 0,
        .fallback = Value{15},
        .effectName = "blur",
    });
    f.push_back(Field{
        .group = "Effect-blur",
        .key = "NoiseStrength",
        .control = ControlKind::Slider,
        .title = "Noise strength",
        .subtitle = "Noise added to the blur to reduce banding and keep gradients smooth.",
        .minValue = 0.0,
        .maxValue = 14.0,
        .step = 1.0,
        .decimals = 0,
        .fallback = Value{5},
        .effectName = "blur",
    });
    f.push_back(Field{
        .group = "Effect-blur",
        .key = "Saturation",
        .control = ControlKind::Slider,
        .title = "Saturation",
        .subtitle = "Color saturation of the blurred area. Higher values make the blurred colors richer.",
        .minValue = 100.0,
        .maxValue = 500.0,
        .step = 10.0,
        .decimals = 0,
        .fallback = Value{150},
        .effectName = "blur",
    });

    return f;
  }();
  return table;
}

// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string BlurPage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const BlurPage::Field* BlurPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

BlurPage::Value BlurPage::defaultValue(const Field& field) {
  return field.fallback;
}

bool BlurPage::valuesEqual(const Value& a, const Value& b) {
  return a.index() == b.index() && a == b;
}

bool BlurPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(value, defaultValue(field));
}

void BlurPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

BlurPage::Value BlurPage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else {
          const int parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, static_cast<int>(field.minValue), static_cast<int>(field.maxValue));
        }
      },
      fallback
  );
}

BlurPage::Value BlurPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool BlurPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool BlurPage::matchesFilter(const Field& field, const Filter& filter) const {
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
    if (!matches(field.title) && !matches(field.subtitle)) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the other pages' override semantics.
// ---------------------------------------------------------------------------

void BlurPage::writeValue(const Field& field, const Value& value) {
  if (equalsDefault(field, value)) {
    m_config.removeKey(field.group, field.key);
    return;
  }
  std::visit(
      [this, &field](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          m_config.setBool(field.group, field.key, v);
        } else {
          m_config.setInt(field.group, field.key, v);
        }
      },
      value
  );
}

void BlurPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void BlurPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> BlurPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void BlurPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void BlurPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
}

bool BlurPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  // [Plugins] enable toggles drive loadEffect/unloadEffect; every other field
  // belongs to the [Effect-blur] group that reconfigureEffect() re-reads.
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

std::unique_ptr<Node> BlurPage::buildControl(
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
  case ControlKind::Slider: {
    const int value = std::get<int>(effectiveValue(field));
    const double clamped = std::clamp(static_cast<double>(value), field.minValue, field.maxValue);
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
        const double next = std::clamp(sliderPtr->value(), field.minValue, field.maxValue);
        stageAndRebuild(field, Value{static_cast<int>(std::llround(next))}, callbacks);
      }
    });
    return ui::row(
        {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
        std::move(slider), std::move(valueLabel)
    );
  }
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title (+ Advanced badge)
// and caption description on the left; [Override badge][Reset][control] on the
// right. Badges only appear while the value differs from the default.
void BlurPage::addSettingRow(
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

std::unique_ptr<Node> BlurPage::build(
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
          .glyph = "sparkles",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Blur", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  std::vector<const Field*> matching;
  for (const auto& field : fields()) {
    if (matchesFilter(field, filter)) {
      matching.push_back(&field);
    }
  }

  if (matching.empty()) {
    // Global search drops pages with no hits, so don't render the notice.
    if (!filter.searchQuery.empty()) {
      return nullptr;
    }
    root->addChild(makeLabel(
        "No settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
    return root;
  }

  if (filter.searchQuery.empty()) {
    if (!m_expandedInitialized) {
      m_expandedInitialized = true;
      m_expandedGroups.insert("Blur");
    }
    Flex* body = kinetic_settings::addCollapsibleSection(*root, "Blur", "Blur", m_expandedGroups, scale);
    for (const Field* field : matching) {
      addSettingRow(*body, *field, scale, armedResetKey, callbacks);
    }
  } else {
    // Search mode: flatten the card to a plain header so every match stays
    // visible (mirrors the other pages).
    root->addChild(makeLabel(
        "Blur", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
    ));
    for (const Field* field : matching) {
      addSettingRow(*root, *field, scale, armedResetKey, callbacks);
    }
  }

  return root;
}
