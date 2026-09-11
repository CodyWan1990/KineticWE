#include "desktop_effects_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace effects_catalog;

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

  // Status pill ("Override" / "Advanced"); same as the Workspaces/Window
  // Behavior pages.
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

  // The two-stage Reset action shown next to overridden effects.
  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const DesktopEffectsPage::Callbacks& callbacks
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

DesktopEffectsPage::DesktopEffectsPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// State: file-backed (config) vs effective (staged) enabled state.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

bool DesktopEffectsPage::fileBackedEnabled(const Effect& effect) const {
  return m_config.readBool("Plugins", effect.name + "Enabled", effect.enabledByDefault);
}

bool DesktopEffectsPage::effectiveEnabled(const Effect& effect) const {
  const auto staged = m_pending.find(effect.name);
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedEnabled(effect);
}

bool DesktopEffectsPage::overridden(const Effect& effect) const {
  return effectiveEnabled(effect) != effect.enabledByDefault;
}

bool DesktopEffectsPage::matchesFilter(const Effect& effect, const Filter& filter) const {
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

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the other pages' override semantics.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::stage(const Effect& effect, bool value) {
  ensureLoaded();
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (value == fileBackedEnabled(effect)) {
    m_pending.erase(effect.name);
  } else {
    m_pending[effect.name] = value;
  }
}

void DesktopEffectsPage::stageAndRebuild(const Effect& effect, bool value, const Callbacks& callbacks) {
  stage(effect, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> DesktopEffectsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(effects().size());
  for (const Effect* effect : visibleEffects()) {
    paths.push_back(keyPath(*effect));
  }
  return paths;
}

void DesktopEffectsPage::resetKeyPath(std::string_view keyPath) {
  if (const Effect* effect = findEffect(keyPath)) {
    stage(*effect, effect->enabledByDefault);
  }
}

void DesktopEffectsPage::resetAll() {
  for (const Effect* effect : visibleEffects()) {
    stage(*effect, effect->enabledByDefault);
  }
}

bool DesktopEffectsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  for (const auto& [name, enabled] : m_pending) {
    const Effect* effect = findEffect(name);
    if (effect == nullptr) {
      continue;
    }
    if (enabled == effect->enabledByDefault) {
      // Back to the compiled-in default: drop the key so the file stays tidy.
      m_config.removeKey("Plugins", name + "Enabled");
    } else {
      m_config.setBool("Plugins", name + "Enabled", enabled);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  // Apply each changed effect live via org.kde.kwin.Effects so the user does
  // not have to restart the session (or wait on the config watcher).
  for (const auto& [name, enabled] : m_pending) {
    if (enabled) {
      m_dbus.loadEffect(name);
    } else {
      m_dbus.unloadEffect(name);
    }
  }

  m_pending.clear();
  return true;
}

// ---------------------------------------------------------------------------
// Controls + layout.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::addEffectRow(
    Flex& parent, const Effect& effect, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(effect);
  const bool isOverridden = overridden(effect);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(
      makeLabel(effect.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
  );
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!effect.description.empty()) {
    copy->addChild(ui::label({
        .text = effect.description,
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
        stageAndRebuild(effect, value, callbacks);
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

void DesktopEffectsPage::addExclusiveGroupRow(
    Flex& parent, const std::string& group, const std::vector<const Effect*>& members,
    float scale, const Callbacks& callbacks
) {
  const GroupInfo* info = exclusiveGroupInfo(group);
  const std::string title = info != nullptr ? std::string(info->title) : group;
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
  titleRow->addChild(
      makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
  );
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

void DesktopEffectsPage::stageGroup(const std::vector<const Effect*>& members, const Effect* selected) {
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

std::unique_ptr<Node> DesktopEffectsPage::build(
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
          .glyph = "sparkles",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Desktop Effects", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  // Precompute mutually-exclusive groups that have >= 2 visible members so they
  // can be rendered as a single dropdown instead of individual toggles.
  std::map<std::string, std::vector<const Effect*>> exclusiveGroups;
  std::set<std::string> groupedEffectNames;
  for (const Effect* effect : visibleEffects()) {
    if (effect->exclusiveGroup.empty()) {
      continue;
    }
    exclusiveGroups[effect->exclusiveGroup].push_back(effect);
  }
  for (auto it = exclusiveGroups.begin(); it != exclusiveGroups.end();) {
    if (it->second.size() < 2) {
      it = exclusiveGroups.erase(it);
    } else {
      for (const Effect* member : it->second) {
        groupedEffectNames.insert(member->name);
      }
      ++it;
    }
  }

  bool anyRows = false;
  bool firstGroup = true;
  std::string_view lastCategory;
  std::string lastRenderedGroup;
  const auto addGroupHeader = [&](std::string_view title) {
    if (title.empty()) {
      return;
    }
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
    firstGroup = false;
  };

  for (const Effect* effect : visibleEffects()) {
    if (isAnimationEffect(*effect)) {
      continue; // animation selectors + Geometry Change live on the Animations page
    }
    if (!matchesFilter(*effect, filter)) {
      continue;
    }
    if (effect->category != lastCategory) {
      lastCategory = effect->category;
      addGroupHeader(lastCategory);
    }
    if (groupedEffectNames.contains(effect->name)) {
      if (lastRenderedGroup != effect->exclusiveGroup) {
        lastRenderedGroup = effect->exclusiveGroup;
        addExclusiveGroupRow(*root, effect->exclusiveGroup, exclusiveGroups[effect->exclusiveGroup], scale, callbacks);
        anyRows = true;
      }
    } else {
      addEffectRow(*root, *effect, scale, armedResetKey, callbacks);
      anyRows = true;
    }
  }

  if (!anyRows) {
    // Global search merges hits from every page and drops pages with none, so
    // an empty page must not render its own empty-state notice.
    if (!filter.searchQuery.empty()) {
      return nullptr;
    }
    root->addChild(makeLabel(
        "No effects match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
