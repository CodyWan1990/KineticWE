#include "shadows_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "kinetic-settings/src/section_card.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/slider.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;

  constexpr float kDefaultControlWidth = 190.0F;
  constexpr float kSliderValueInputWidth = 50.0F;
  constexpr float kSuffixSlotWidth = 36.0F;

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

  // The two-stage Reset action shown next to overridden settings: the first
  // click arms it, the second confirms (upstream makeGroupedResetButton).
  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const ShadowsPage::Callbacks& callbacks
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

  // Accepts the QColor text forms KConfig round-trips: #rgb, #rrggbb, #aarrggbb.
  [[nodiscard]] bool isHexColorText(std::string_view text) {
    const auto trimmed = StringUtils::trim(text);
    if (trimmed.size() != 4 && trimmed.size() != 7 && trimmed.size() != 9) {
      return false;
    }
    return trimmed.front() == '#'
        && std::ranges::all_of(trimmed.substr(1), [](unsigned char c) { return std::isxdigit(c) != 0; });
  }

  [[nodiscard]] std::string normalizedColorText(std::string_view text) {
    return StringUtils::toLower(StringUtils::trim(text));
  }

  [[nodiscard]] std::string joinKeyPath(std::string_view group, std::string_view key) {
    return std::string(group) + '.' + std::string(key);
  }

} // namespace

ShadowsPage::ShadowsPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: option catalogs + the field descriptor table — the single source of
// truth for file keys, defaults, ranges and presentation. Defaults mirror
// src/kcms/tiling/tilingsettings.kcfg (and the compositor's TilingController
// fallbacks when the key is absent).
// ---------------------------------------------------------------------------

const std::vector<std::string>& ShadowsPage::shadowModeValues() {
  static const std::vector<std::string> values{"None", "AllTiled", "ActiveOnly", "AllWindows"};
  return values;
}

const std::vector<std::string>& ShadowsPage::shadowModeLabels() {
  static const std::vector<std::string> labels{"None", "All tiled windows", "Active window only", "All windows"};
  return labels;
}

const std::vector<ShadowsPage::Field>& ShadowsPage::fields() {
  static const std::vector<Field> table{
      Field{
          .group = "Tiling",
          .key = "TilingShadowMode",
          .control = ControlKind::Select,
          .title = "Shadow mode",
          .subtitle = "Shadow display mode for windows",
          .optionValues = &shadowModeValues(),
          .optionLabels = &shadowModeLabels(),
          .fallback = Value{std::string("None")},
      },
      Field{
          .group = "Tiling",
          .key = "TilingShadowSize",
          .control = ControlKind::Slider,
          .title = "Shadow size",
          .subtitle = "Blur radius of the shadow (in pixels)",
          .maxValue = 64.0,
          .fallback = Value{16},
      },
      Field{
          .group = "Tiling",
          .key = "TilingShadowColor",
          .control = ControlKind::Text,
          .title = "Shadow color",
          .subtitle = "Color of the shadow drawn around tiled windows (#aarrggbb or #rrggbb)",
          .placeholder = "#78000000",
          .fallback = Value{std::string("#78000000")},
      },
      Field{
          .group = "Tiling",
          .key = "TilingShadowOpacity",
          .control = ControlKind::Slider,
          .title = "Shadow opacity",
          .subtitle = "Opacity of the shadow, applied on top of the color's alpha",
          .maxValue = 100.0,
          .valueSuffix = "%",
          .fallback = Value{100},
      },
      Field{
          .group = "Tiling",
          .key = "TilingShadowOffsetX",
          .control = ControlKind::Slider,
          .title = "Shadow offset X",
          .subtitle = "How far the shadow is shifted sideways; negative is left (in pixels)",
          .minValue = -32.0,
          .maxValue = 32.0,
          .fallback = Value{0},
      },
      Field{
          .group = "Tiling",
          .key = "TilingShadowOffsetY",
          .control = ControlKind::Slider,
          .title = "Shadow offset Y",
          .subtitle = "How far the shadow is shifted downward (in pixels)",
          .maxValue = 32.0,
          .fallback = Value{4},
      },
  };
  return table;
}

// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string ShadowsPage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const ShadowsPage::Field* ShadowsPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

ShadowsPage::Value ShadowsPage::defaultValue(const Field& field) {
  return field.fallback;
}

bool ShadowsPage::valuesEqual(const Field& field, const Value& a, const Value& b) {
  if (a.index() != b.index()) {
    return false;
  }
  if (const auto* text = std::get_if<std::string>(&a)) {
    if (field.key == "TilingShadowColor") {
      return normalizedColorText(*text) == normalizedColorText(std::get<std::string>(b));
    }
  }
  return a == b;
}

bool ShadowsPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(field, value, defaultValue(field));
}

void ShadowsPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

ShadowsPage::Value ShadowsPage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return m_config.readString(field.group, field.key).value_or(fallbackValue);
        } else {
          const int parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, static_cast<int>(field.minValue), static_cast<int>(field.maxValue));
        }
      },
      fallback
  );
}

ShadowsPage::Value ShadowsPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool ShadowsPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool ShadowsPage::matchesFilter(const Field& field, const Filter& filter) const {
  if (filter.showOverriddenOnly && !overridden(field)) {
    return false;
  }
  if (!filter.searchQuery.empty()) {
    const auto matches = [&filter](std::string_view text) {
      return StringUtils::containsInsensitive(text, filter.searchQuery);
    };
    if (!matches(field.title) && !matches(field.subtitle) && !matches("Shadows")) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the other pages' override semantics.
// ---------------------------------------------------------------------------

void ShadowsPage::writeValue(const Field& field, const Value& value) {
  if (equalsDefault(field, value)) {
    m_config.removeKey(field.group, field.key);
    return;
  }
  std::visit(
      [this, &field](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::string>) {
          m_config.setString(field.group, field.key, v);
        } else {
          m_config.setInt(field.group, field.key, v);
        }
      },
      value
  );
}

void ShadowsPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(field, value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void ShadowsPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> ShadowsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void ShadowsPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void ShadowsPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
}

bool ShadowsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  // Reload first so edits applied from another page in the meantime (global
  // search can stage edits on several pages at once) are preserved: save()
  // rewrites the whole file, not just the touched keys.
  m_config.load();
  for (const auto& [path, value] : m_pending) {
    if (const Field* field = findField(path)) {
      writeValue(*field, value);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false; // keep the staged edits so the user can retry Apply
  }

  m_pending.clear();
  // The tiling controller reads the shadow keys on reconfigure().
  m_dbus.requestReconfigure();
  return true;
}

// ---------------------------------------------------------------------------
// Controls (Noctalia settings visuals, re-implemented module-locally).
// ---------------------------------------------------------------------------

std::unique_ptr<Node> ShadowsPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  switch (field.control) {
  case ControlKind::Select: {
    const auto& values = *field.optionValues;
    const auto& labels = *field.optionLabels;
    const auto selected = std::ranges::find(values, std::get<std::string>(effectiveValue(field)));
    const bool known = selected != values.end();
    const std::optional<std::size_t> selectedIndex =
        known ? std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())} : std::nullopt;
    return ui::select({
        .options = labels,
        .selectedIndex = selectedIndex,
        .clearSelection = !known,
        .placeholder = known ? std::optional<std::string>{std::nullopt}
                             : std::optional<std::string>{"Unknown value: "
                                                          + std::get<std::string>(effectiveValue(field))},
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .width = kDefaultControlWidth * scale,
        .height = Style::controlHeight * scale,
        .onSelectionChanged = [this, field, callbacks, values](std::size_t index, std::string_view /*label*/) {
          if (index < values.size()) {
            stageAndRebuild(field, Value{values[index]}, callbacks);
          }
        },
    });
  }

  case ControlKind::Slider: {
    const int current = std::get<int>(effectiveValue(field));
    auto wrap = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});

    Input* valueInputPtr = nullptr;
    auto valueInput = ui::input({
        .out = &valueInputPtr,
        .value = std::to_string(current),
        .fontSize = Style::fontSizeCaption * scale,
        .controlHeight = Style::controlHeightSm * scale,
        .horizontalPadding = Style::spaceXs * scale,
        .width = kSliderValueInputWidth * scale,
        .height = Style::controlHeightSm * scale,
    });

    Slider* sliderPtr = nullptr;
    auto slider = ui::slider({
        .out = &sliderPtr,
        .minValue = field.minValue,
        .maxValue = field.maxValue,
        .step = 1.0,
        .value = static_cast<double>(current),
        .width = Style::sliderDefaultWidth * scale,
        .onValueChanged = [valueInputPtr](double v) {
          valueInputPtr->setInvalid(false);
          valueInputPtr->setValue(std::to_string(static_cast<int>(std::llround(v))));
        },
    });
    // Wire drag-end AFTER construction: a lambda in the props struct would
    // capture sliderPtr while it is still null (out is assigned by the builder).
    slider->setOnDragEnd([this, field, callbacks, sliderPtr]() {
      if (sliderPtr != nullptr) {
        stageAndRebuild(field, Value{static_cast<int>(std::llround(sliderPtr->value()))}, callbacks);
      }
    });

    const auto commitInputText = [this, field, callbacks, sliderPtr, valueInputPtr](const std::string& text) {
      const auto parsed = StringUtils::parseDotDecimal<double>(text);
      if (!parsed.has_value() || *parsed < field.minValue || *parsed > field.maxValue) {
        valueInputPtr->setInvalid(true);
        return false;
      }
      valueInputPtr->setInvalid(false);
      sliderPtr->setValue(*parsed);
      const int snapped = static_cast<int>(std::llround(sliderPtr->value()));
      valueInputPtr->setValue(std::to_string(snapped));
      stageAndRebuild(field, Value{snapped}, callbacks);
      return true;
    };

    valueInput->setOnChange([valueInputPtr](const std::string& /*text*/) { valueInputPtr->setInvalid(false); });
    valueInput->setOnSubmit([commitInputText](const std::string& text) { (void)commitInputText(text); });
    valueInput->setOnFocusLoss([commitInputText, valueInputPtr]() { (void)commitInputText(valueInputPtr->value()); });

    wrap->addChild(std::move(slider));
    wrap->addChild(std::move(valueInput));
    if (!field.valueSuffix.empty()) {
      wrap->addChild(ui::row(
          {.align = FlexAlign::Center, .justify = FlexJustify::Start, .width = kSuffixSlotWidth * scale},
          makeLabel(field.valueSuffix, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant))
      ));
    }
    return wrap;
  }

  case ControlKind::Text: {
    Input* inputPtr = nullptr;
    auto input = ui::input({
        .out = &inputPtr,
        .value = std::get<std::string>(effectiveValue(field)),
        .placeholder = std::string(field.placeholder),
        .fontSize = Style::fontSizeBody * scale,
        .controlHeight = Style::controlHeight * scale,
        .horizontalPadding = Style::spaceSm * scale,
        .width = kDefaultControlWidth * scale,
        .height = Style::controlHeight * scale,
        .submitOnFocusLoss = true,
    });
    input->setOnChange([inputPtr](const std::string& /*text*/) { inputPtr->setInvalid(false); });
    input->setOnSubmit([this, field, callbacks, inputPtr](const std::string& value) {
      const std::string trimmed = StringUtils::trim(value);
      if (!isHexColorText(trimmed)) {
        inputPtr->setInvalid(true);
        return;
      }
      inputPtr->setInvalid(false);
      stageAndRebuild(field, Value{trimmed}, callbacks);
    });
    return input;
  }
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title and caption
// description on the left; [Override badge][Reset][control] on the right.
void ShadowsPage::addSettingRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const bool isOverridden = overridden(field);

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
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

std::unique_ptr<Node> ShadowsPage::build(
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
          .glyph = "shadow",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Shadows", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
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
      m_expandedGroups.insert("Shadows");
    }
    Flex* body = kinetic_settings::addCollapsibleSection(*root, "Shadows", "Shadows", m_expandedGroups, scale);
    for (const Field* field : matching) {
      addSettingRow(*body, *field, scale, armedResetKey, callbacks);
    }
  } else {
    // Search mode: flatten the card to a plain header so every match stays
    // visible (mirrors the other pages).
    root->addChild(makeLabel(
        "Shadows", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
    ));
    for (const Field* field : matching) {
      addSettingRow(*root, *field, scale, armedResetKey, callbacks);
    }
  }

  return root;
}
