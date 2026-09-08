#include "keyboard_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"
#include "ui/controls/stepper.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"

#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

  constexpr Logger kLog("kinetic-settings");

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
      std::string path, bool pendingConfirmation, float scale, const KeyboardPage::Callbacks& callbacks
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

KeyboardPage::KeyboardPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: the key-repeat settings and their compositor defaults. Defaults and
// units mirror src/keyboard_input.cpp ([Keyboard] RepeatDelay=600,
// RepeatRate=25, KeyRepeat="repeat"); ranges mirror the upstream Plasma
// keyboard KCM (delay 100..5000 ms, rate shown in repeats/s).
// ---------------------------------------------------------------------------

const std::vector<KeyboardPage::Field>& KeyboardPage::fields() {
  static const std::vector<Field> table{
      Field{
          .key = "KeyRepeat",
          .title = "Key repeat",
          .subtitle = "When enabled, pressing and holding a key sends it repeatedly",
          .isToggle = true,
          .fallback = Value{true},
      },
      Field{
          .key = "RepeatDelay",
          .title = "Repeat delay",
          .subtitle = "How long a key must be held before it starts repeating",
          .isToggle = false,
          .fallback = Value{600},
          .minValue = 100,
          .maxValue = 5000,
          .step = 50,
          .valueSuffix = " ms",
      },
      Field{
          .key = "RepeatRate",
          .title = "Repeat rate",
          .subtitle = "How many times per second a held key repeats",
          .isToggle = false,
          .fallback = Value{25},
          .minValue = 1,
          .maxValue = 100,
          .step = 1,
          .valueSuffix = " repeats/s",
      },
  };
  return table;
}

std::string KeyboardPage::keyPath(const Field& field) {
  return std::string("Keyboard") + '.' + std::string(field.key);
}

const KeyboardPage::Field* KeyboardPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// State.
// ---------------------------------------------------------------------------

void KeyboardPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

KeyboardPage::Value KeyboardPage::fileBackedValue(const Field& field) const {
  if (field.isToggle) {
    const bool repeat = m_config.readString("Keyboard", field.key).value_or("repeat") == "repeat";
    return Value{repeat};
  }
  if (field.key == "RepeatRate") {
    return Value{static_cast<int>(std::lround(m_config.readDouble("Keyboard", field.key, 25.0)))};
  }
  return Value{m_config.readInt("Keyboard", field.key, std::get<int>(field.fallback))};
}

KeyboardPage::Value KeyboardPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool KeyboardPage::equalsDefault(const Field& field, const Value& value) const {
  return value == field.fallback;
}

bool KeyboardPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

// ---------------------------------------------------------------------------
// Staging + persistence.
// ---------------------------------------------------------------------------

void KeyboardPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (value == fileBackedValue(field)) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void KeyboardPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> KeyboardPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void KeyboardPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, field->fallback);
  }
}

void KeyboardPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, field.fallback);
  }
}

bool KeyboardPage::hasPending() const noexcept {
  return !m_pending.empty();
}

void KeyboardPage::writeValue(const Field& field, const Value& value) {
  if (field.isToggle) {
    const bool repeat = std::get<bool>(value);
    if (repeat == true) {
      // Back to the compositor default ("repeat") keeps the file tidy.
      m_config.removeKey("Keyboard", field.key);
    } else {
      m_config.setString("Keyboard", field.key, "no-repeat");
    }
    return;
  }
  if (field.key == "RepeatRate") {
    const int number = std::get<int>(value);
    if (number == std::get<int>(field.fallback)) {
      m_config.removeKey("Keyboard", field.key);
    } else {
      m_config.setDouble("Keyboard", field.key, static_cast<double>(number));
    }
    return;
  }
  const int number = std::get<int>(value);
  if (number == std::get<int>(field.fallback)) {
    m_config.removeKey("Keyboard", field.key);
  } else {
    m_config.setInt("Keyboard", field.key, number);
  }
}

bool KeyboardPage::applyAll() {
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
// Controls + layout.
// ---------------------------------------------------------------------------

std::unique_ptr<Node>
KeyboardPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  if (field.isToggle) {
    const bool checked = std::get<bool>(effectiveValue(field));
    return ui::toggle({
        .checked = checked,
        .scale = scale,
        .onChange = [this, field, callbacks](bool value) {
          stageAndRebuild(field, Value{value}, callbacks);
        },
    });
  }
  const int value = std::get<int>(effectiveValue(field));
  return ui::stepper({
      .minValue = field.minValue,
      .maxValue = field.maxValue,
      .step = field.step,
      .value = std::clamp(value, field.minValue, field.maxValue),
      .scale = scale,
      .valueSuffix = std::string(field.valueSuffix),
      .onValueCommitted = [this, field, callbacks](int committed) {
        stageAndRebuild(field, Value{std::clamp(committed, field.minValue, field.maxValue)}, callbacks);
      },
  });
}

// One settings row in the exact Noctalia layout: bold title and caption
// description on the left; [Override badge][Reset][control] on the right.
// Badges only appear while the value differs from the default.
void KeyboardPage::addSettingRow(
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

std::unique_ptr<Node> KeyboardPage::build(
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

  // Section header: glyph + bold primary title.
  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = "keyboard",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Keyboard", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  bool anyRows = false;
  for (const auto& field : fields()) {
    if (!filter.searchQuery.empty()) {
      const auto matches = [&filter](std::string_view text) {
        return StringUtils::containsInsensitive(text, filter.searchQuery);
      };
      if (!matches(field.title) && !matches(field.subtitle)) {
        continue;
      }
    }
    if (filter.showOverriddenOnly && !overridden(field)) {
      continue;
    }
    addSettingRow(*root, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    root->addChild(makeLabel(
        "No keyboard settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
