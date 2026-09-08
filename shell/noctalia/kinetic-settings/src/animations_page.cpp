#include "animations_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;

  // Logarithmic animation-speed steps, mirroring KWin's animations KCM:
  // 4x (slowest) down to 0 (instant). AnimationDurationFactor multiplies an
  // animation's duration, so larger values are slower and 0 disables timing.
  constexpr std::size_t kSpeedStepCount = 7;
  constexpr std::array<double, kSpeedStepCount> kSpeedFactors{4.0, 2.0, 1.5, 1.0, 0.75, 0.5, 0.0};
  constexpr std::array<std::string_view, kSpeedStepCount> kSpeedLabels{
      "4×", "2×", "1.5×", "1×", "0.75×", "0.5×", "Instant"};

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

  // Status pill ("Override"); same as the Desktop Effects page.
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
      std::string path, bool pendingConfirmation, float scale, const AnimationsPage::Callbacks& callbacks
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

AnimationsPage::AnimationsPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// State: file-backed (config) vs effective (staged) enabled state.
// ---------------------------------------------------------------------------

void AnimationsPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

bool AnimationsPage::hasPending() const noexcept {
  return !m_pending.empty() || m_pendingSpeed.has_value();
}

bool AnimationsPage::fileBackedEnabled(const Effect& effect) const {
  return m_config.readBool("Plugins", effect.name + "Enabled", effect.enabledByDefault);
}

bool AnimationsPage::effectiveEnabled(const Effect& effect) const {
  const auto staged = m_pending.find(effect.name);
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedEnabled(effect);
}

bool AnimationsPage::overridden(const Effect& effect) const {
  return effectiveEnabled(effect) != effect.enabledByDefault;
}

bool AnimationsPage::matchesFilter(const Effect& effect, const Filter& filter) const {
  if (filter.showOverriddenOnly && !overridden(effect)) {
    return false;
  }
  if (!filter.searchQuery.empty()) {
    const auto matches = [&filter](std::string_view text) {
      return StringUtils::containsInsensitive(text, filter.searchQuery);
    };
    if (!matches(effect.title) && !matches(effect.description) && !matches(effect.category)) {
      return false;
    }
  }
  return true;
}

double AnimationsPage::fileBackedSpeed() const {
  return m_config.readDouble("KDE", "AnimationDurationFactor", 1.0);
}

double AnimationsPage::effectiveSpeed() const {
  return m_pendingSpeed.value_or(fileBackedSpeed());
}

bool AnimationsPage::speedOverridden() const {
  return effectiveSpeed() != 1.0;
}

std::size_t AnimationsPage::speedToIndex(double factor) {
  for (std::size_t i = 0; i < kSpeedStepCount; ++i) {
    if (kSpeedFactors[i] <= factor) {
      return i;
    }
  }
  return kSpeedStepCount - 1; // factor < 0: treat as instant
}

double AnimationsPage::indexToSpeed(std::size_t index) {
  return kSpeedFactors[index < kSpeedStepCount ? index : kSpeedStepCount - 1];
}

std::string_view AnimationsPage::speedLabel(std::size_t index) {
  return kSpeedLabels[index < kSpeedStepCount ? index : kSpeedStepCount - 1];
}

// ---------------------------------------------------------------------------
// Staging.
// ---------------------------------------------------------------------------

void AnimationsPage::stageSpeed(double factor) {
  ensureLoaded();
  if (factor == fileBackedSpeed()) {
    m_pendingSpeed.reset();
  } else {
    m_pendingSpeed = factor;
  }
}

void AnimationsPage::stage(const Effect& effect, bool value) {
  ensureLoaded();
  if (value == fileBackedEnabled(effect)) {
    m_pending.erase(effect.name);
  } else {
    m_pending[effect.name] = value;
  }
}

void AnimationsPage::stageGroup(const std::vector<const Effect*>& members, const Effect* selected) {
  ensureLoaded();
  for (const Effect* member : members) {
    const bool value = (member == selected);
    if (value == fileBackedEnabled(*member)) {
      m_pending.erase(member->name);
    } else {
      m_pending[member->name] = value;
    }
  }
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply.
// ---------------------------------------------------------------------------

std::vector<std::string> AnimationsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.emplace_back(kSpeedKeyPath);
  for (const Effect* effect : effects_catalog::visibleEffects()) {
    if (effects_catalog::isAnimationEffect(*effect)) {
      paths.push_back(effects_catalog::keyPath(*effect));
    }
  }
  return paths;
}

void AnimationsPage::resetKeyPath(std::string_view keyPath) {
  if (keyPath == kSpeedKeyPath) {
    stageSpeed(1.0);
    return;
  }
  if (const Effect* effect = effects_catalog::findEffect(keyPath)) {
    stage(*effect, effect->enabledByDefault);
  }
}

void AnimationsPage::resetAll() {
  stageSpeed(1.0);
  for (const Effect* effect : effects_catalog::visibleEffects()) {
    if (effects_catalog::isAnimationEffect(*effect)) {
      stage(*effect, effect->enabledByDefault);
    }
  }
}

bool AnimationsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty() && !m_pendingSpeed.has_value()) {
    return true;
  }

  // Animation-speed first (same file write as the effect toggles below).
  if (m_pendingSpeed.has_value()) {
    if (*m_pendingSpeed == 1.0) {
      m_config.removeKey("KDE", "AnimationDurationFactor");
    } else {
      m_config.setDouble("KDE", "AnimationDurationFactor", *m_pendingSpeed);
    }
  }

  for (const auto& [name, enabled] : m_pending) {
    const Effect* effect = effects_catalog::findEffect(name);
    if (effect == nullptr) {
      continue;
    }
    if (enabled == effect->enabledByDefault) {
      m_config.removeKey("Plugins", name + "Enabled");
    } else {
      m_config.setBool("Plugins", name + "Enabled", enabled);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  // Apply each changed effect live, then reconfigure so the animation-speed
  // factor is picked up by the compositor's effect timers.
  for (const auto& [name, enabled] : m_pending) {
    if (enabled) {
      m_dbus.loadEffect(name);
    } else {
      m_dbus.unloadEffect(name);
    }
  }
  m_dbus.requestReconfigure();

  m_pending.clear();
  m_pendingSpeed.reset();
  return true;
}

// ---------------------------------------------------------------------------
// Controls + layout.
// ---------------------------------------------------------------------------

void AnimationsPage::addSpeedRow(
    Flex& parent, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const double current = effectiveSpeed();
  const bool overridden = speedOverridden();
  const std::size_t index = speedToIndex(current);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(
      makeLabel("Animation Speed", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
  );
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  copy->addChild(ui::label({
      .text = "Global speed multiplier for window and desktop animations",
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = kSettingDescriptionMaxLines,
  }));

  Slider* sliderPtr = nullptr;
  Label* valuePtr = nullptr;
  auto slider = ui::slider({
      .out = &sliderPtr,
      .minValue = 0.0,
      .maxValue = static_cast<double>(kSpeedStepCount - 1),
      .step = 1.0,
      .value = static_cast<double>(index),
      .trackHeight = Style::sliderTrackHeight * scale,
      .thumbSize = Style::sliderThumbSize * scale,
      .controlHeight = Style::controlHeight * scale,
      .width = Style::sliderDefaultWidth * scale,
  });
  auto valueLabel = ui::label({
      .out = &valuePtr,
      .text = std::string(speedLabel(index)),
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .minWidth = 52.0F * scale,
      .textAlign = TextAlign::End,
  });
  slider->setOnValueChanged([valuePtr](double next) {
    if (valuePtr != nullptr) {
      valuePtr->setText(std::string(speedLabel(static_cast<std::size_t>(next))));
    }
  });
  slider->setOnDragEnd([this, sliderPtr, callbacks]() {
    if (sliderPtr != nullptr) {
      stageSpeed(indexToSpeed(static_cast<std::size_t>(sliderPtr->value())));
      if (callbacks.requestRebuild) {
        callbacks.requestRebuild();
      }
    }
  });

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  if (overridden) {
    actions->addChild(makeOverrideBadge(scale));
    actions->addChild(makeResetButton(std::string(kSpeedKeyPath), armedResetKey == kSpeedKeyPath, scale, callbacks));
  }
  actions->addChild(std::move(slider));
  actions->addChild(std::move(valueLabel));

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

void AnimationsPage::addEffectRow(
    Flex& parent, std::string_view title, std::string_view description, const Effect& effect,
    float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = effects_catalog::keyPath(effect);
  const bool isOverridden = overridden(effect);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!description.empty()) {
    copy->addChild(ui::label({
        .text = std::string(description),
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
  actions->addChild(ui::toggle({
      .checked = effectiveEnabled(effect),
      .scale = scale,
      .onChange = [this, effect, callbacks](bool value) {
        stage(effect, value);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
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
  row->addChild(std::move(copy));
  row->addChild(std::move(actions));
  parent.addChild(std::move(row));
}

void AnimationsPage::addExclusiveGroupRow(
    Flex& parent, std::string_view group, const std::vector<const Effect*>& members,
    float scale, const Callbacks& callbacks
) {
  const effects_catalog::GroupInfo* info = effects_catalog::exclusiveGroupInfo(group);
  const std::string title = info != nullptr ? std::string(info->title) : std::string(group);
  const std::string description = info != nullptr ? std::string(info->description) : std::string();

  // Dropdown options: a "None" entry (index 0) then one entry per member.
  std::vector<std::string> options;
  options.reserve(members.size() + 1);
  options.push_back("None");
  for (const Effect* member : members) {
    options.push_back(member->title);
  }

  // The currently-enabled member (first one wins); index 0 means none.
  std::size_t selectedIndex = 0;
  for (std::size_t i = 0; i < members.size(); ++i) {
    if (effectiveEnabled(*members[i])) {
      selectedIndex = i + 1;
      break;
    }
  }

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!description.empty()) {
    copy->addChild(ui::label({
        .text = description,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = kSettingDescriptionMaxLines,
    }));
  }

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  actions->addChild(ui::select({
      .options = std::move(options),
      .selectedIndex = selectedIndex,
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeightSm * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .width = 150.0F * scale,
      .height = Style::controlHeightSm * scale,
      .onSelectionChanged = [this, members, callbacks](std::size_t index, std::string_view /*label*/) {
        const Effect* selected = nullptr;
        if (index > 0 && index <= members.size()) {
          selected = members[index - 1];
        }
        stageGroup(members, selected);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
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
  row->addChild(std::move(copy));
  row->addChild(std::move(actions));
  parent.addChild(std::move(row));
}

std::unique_ptr<Node> AnimationsPage::build(
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

  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = "movie",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Animations", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  const auto searchMatches = [&filter](std::string_view text) {
    return filter.searchQuery.empty() || StringUtils::containsInsensitive(text, filter.searchQuery);
  };

  bool anyRows = false;

  // Global animation speed (always the first row).
  const bool speedMatches = searchMatches("Animation Speed") || searchMatches("animation speed");
  if (speedMatches && !(filter.showOverriddenOnly && !speedOverridden())) {
    addSpeedRow(*root, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  // Partition animation effects into exclusive groups vs standalone toggles.
  std::map<std::string, std::vector<const Effect*>> exclusiveGroups;
  std::vector<const Effect*> standalone;
  for (const Effect* effect : effects_catalog::visibleEffects()) {
    if (!effects_catalog::isAnimationEffect(*effect)) {
      continue;
    }
    if (effect->exclusiveGroup.empty()) {
      standalone.push_back(effect);
    } else {
      exclusiveGroups[effect->exclusiveGroup].push_back(effect);
    }
  }

  // Fixed presentation order for the animation-type selectors: the
  // multi-effect (dropdown) groups first, then the single-effect (toggle)
  // groups — Maximize and Full Screen — so the toggles sit together at the end.
  static const std::vector<std::string_view> kGroupOrder{
      "toplevel-open-close-animation",
      "minimize",
      "show-desktop",
      "desktop-animations",
      "maximize",
      "fullscreen",
  };

  for (const std::string_view group : kGroupOrder) {
    const auto it = exclusiveGroups.find(std::string(group));
    if (it == exclusiveGroups.end() || it->second.empty()) {
      continue;
    }
    const auto& members = it->second;

    // A group passes the overridden filter when any member is overridden.
    if (filter.showOverriddenOnly) {
      bool anyOverridden = false;
      for (const Effect* member : members) {
        if (overridden(*member)) {
          anyOverridden = true;
          break;
        }
      }
      if (!anyOverridden) {
        continue;
      }
    }
    // A group passes the search filter when the group label or any member matches.
    const effects_catalog::GroupInfo* info = effects_catalog::exclusiveGroupInfo(group);
    bool searchHit = (info != nullptr)
                  && (searchMatches(info->title) || searchMatches(info->description));
    for (const Effect* member : members) {
      if (searchMatches(member->title) || searchMatches(member->description)
          || searchMatches(member->category)) {
        searchHit = true;
        break;
      }
    }
    if (!searchHit) {
      continue;
    }

    if (members.size() >= 2) {
      addExclusiveGroupRow(*root, group, members, scale, callbacks);
    } else {
      const std::string_view title = info != nullptr ? info->title : members[0]->title;
      const std::string_view description = info != nullptr ? info->description : members[0]->description;
      addEffectRow(*root, title, description, *members[0], scale, armedResetKey, callbacks);
    }
    anyRows = true;
  }

  // Standalone animation effects (currently just Geometry Change).
  for (const Effect* effect : standalone) {
    if (!matchesFilter(*effect, filter)) {
      continue;
    }
    addEffectRow(*root, effect->title, effect->description, *effect, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    root->addChild(makeLabel(
        "No animations match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
