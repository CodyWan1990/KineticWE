#include "keybinds_page.h"

#include "core/input/key_chord.h"
#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "keybind_codec.h"
#include "keybinds_kwe.h"
#include "notification/notifications.h"
#include "system/desktop_entry.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/search_picker.h"
#include "ui/controls/separator.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  constexpr float kComponentListWidth = 200.0F;
  constexpr float kChordRecorderWidth = 260.0F;
  // Per-window shortcuts (Window Close, Move, ...) are per-instance, not
  // rebindable globals; hide them like the KDE Shortcuts KCM.
  constexpr std::string_view kSessionActionPrefix = "_k_session:";
  // Components whose binds stay active but are not shown in the page
  // (the user's own apps; their binds persist in the daemon's config).
  const std::vector<std::string>& hiddenComponents() {
    // uniqueName is the id the component registered with (dot-form for
    // desktop-file components); the daemon's dbusPath() form uses '_'.
    static const std::vector<std::string> kHidden{
        "plasmashell",
        "org.kde.dolphin.desktop",
        "org.kde.konsole.desktop",
    };
    return kHidden;
  }

  [[nodiscard]] bool isHiddenComponent(std::string_view uniqueName) {
    const auto underscoreForm = [&uniqueName]() {
      std::string form(uniqueName);
      std::ranges::replace(form, '.', '_');
      return form;
    };
    return std::ranges::any_of(hiddenComponents(), [&](const std::string& hidden) {
      return uniqueName == hidden || underscoreForm() == hidden;
    });
  }

  [[nodiscard]] std::unique_ptr<Label>
  makeLabel(std::string_view text, float fontSize, const ColorSpec& color, FontWeight fontWeight = FontWeight::Normal) {
    return ui::label({
        .text = std::string(text),
        .fontSize = fontSize,
        .fontWeight = fontWeight,
        .color = color,
    });
  }

  // Status pill ("Override"), same visual as the other pages.
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

  // kwin is branded "KWin"; everything else uses the daemon's friendly name
  // (which for the compositor's own components defaults to the unique name).
  [[nodiscard]] std::string componentDisplayName(const KeybindDbus::Component& component) {
    if (component.uniqueName == "kwin") {
      return "KWin";
    }
    if (!component.friendlyName.empty()) {
      return component.friendlyName;
    }
    if (component.uniqueName.empty()) {
      return {};
    }
    std::string name = component.uniqueName;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
  }

  // kwin first, then noctalia, then the rest alphabetically (the daemon's
  // registration order is not a stable UI order).
  void orderComponents(std::vector<KeybindDbus::Component>& components) {
    std::stable_sort(components.begin(), components.end(), [](const auto& a, const auto& b) {
      const auto rank = [](const std::string& name) {
        if (name == "kwin") {
          return 0;
        }
        if (name == "noctalia") {
          return 1;
        }
        return 2;
      };
      if (rank(a.uniqueName) != rank(b.uniqueName)) {
        return rank(a.uniqueName) < rank(b.uniqueName);
      }
      return a.uniqueName < b.uniqueName;
    });
  }

  void filterComponents(std::vector<KeybindDbus::Component>& components) {
    std::erase_if(components, [](const KeybindDbus::Component& component) {
      return isHiddenComponent(component.uniqueName);
    });
  }

} // namespace

KeybindsPage::KeybindsPage(SessionBus& bus, CustomKeybinds& customKeybinds)
    : m_bus(bus), m_customKeybinds(customKeybinds), m_dbus(bus) {}

std::string KeybindsPage::keyPath(std::string_view component, std::string_view action) {
  return std::string(component) + '.' + std::string(action);
}

bool KeybindsPage::splitKeyPath(std::string_view keyPath, std::string& component, std::string& action) {
  const auto dot = keyPath.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot + 1 >= keyPath.size()) {
    return false;
  }
  component = keyPath.substr(0, dot);
  action = keyPath.substr(dot + 1);
  return true;
}

void KeybindsPage::ensureLoaded() {
  if (!m_loaded) {
    m_components = m_dbus.components();
    filterComponents(m_components);
    orderComponents(m_components);
    m_loaded = true;
  }
}

const KeybindDbus::Action* KeybindsPage::findAction(std::string_view component, std::string_view action) const {
  const auto componentIt = std::ranges::find_if(
      m_components, [component](const KeybindDbus::Component& c) { return c.uniqueName == component; }
  );
  if (componentIt == m_components.end()) {
    return nullptr;
  }
  const auto actionIt = std::ranges::find_if(
      componentIt->actions, [action](const KeybindDbus::Action& a) { return a.actionUnique == action; }
  );
  return actionIt == componentIt->actions.end() ? nullptr : &*actionIt;
}

std::vector<KeyChord> KeybindsPage::effectiveChords(std::string_view component, std::string_view action) const {
  const auto staged = m_pending.find(keyPath(component, action));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  if (const auto* daemonAction = findAction(component, action)) {
    return daemonAction->active;
  }
  return {};
}

bool KeybindsPage::overridden(const std::string& component, const KeybindDbus::Action& action) const {
  return effectiveChords(component, action.actionUnique) != action.defaults;
}

bool KeybindsPage::matchesFilter(
    const KeybindDbus::Component& component, const KeybindDbus::Action& action, const Filter& filter
) const {
  if (filter.showOverriddenOnly && !overridden(component.uniqueName, action)) {
    return false;
  }
  if (filter.searchQuery.empty()) {
    return true;
  }
  const auto matches = [&filter](std::string_view text) {
    return StringUtils::containsInsensitive(text, filter.searchQuery);
  };
  if (matches(action.friendlyName) || matches(action.actionUnique)) {
    return true;
  }
  for (const auto& chord : effectiveChords(component.uniqueName, action.actionUnique)) {
    if (matches(KeybindCodec::toChordString(chord))) {
      return true;
    }
  }
  return false;
}

void KeybindsPage::stage(const std::string& component, const std::string& action, std::vector<KeyChord> chords) {
  ensureLoaded();
  const std::string path = keyPath(component, action);
  // Editing back to what the daemon has un-stages (the row stops being dirty).
  if (const auto* daemonAction = findAction(component, action); daemonAction != nullptr
      && chords == daemonAction->active) {
    m_pending.erase(path);
  } else {
    m_pending[path] = std::move(chords);
  }
}

void KeybindsPage::stageAndRebuild(
    const std::string& component, const std::string& action, std::vector<KeyChord> chords,
    const Callbacks& callbacks
) {
  stage(component, action, std::move(chords));
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> KeybindsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  for (const auto& component : m_components) {
    for (const auto& action : component.actions) {
      if (!action.actionUnique.starts_with(kSessionActionPrefix)) {
        paths.push_back(keyPath(component.uniqueName, action.actionUnique));
      }
    }
  }
  return paths;
}

void KeybindsPage::resetAll() {
  for (const auto& component : m_components) {
    for (const auto& action : component.actions) {
      if (action.actionUnique.starts_with(kSessionActionPrefix)) {
        continue;
      }
      if (effectiveChords(component.uniqueName, action.actionUnique) != action.defaults) {
        stage(component.uniqueName, action.actionUnique, action.defaults);
      }
    }
  }
}

bool KeybindsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }
  for (const auto& [path, chords] : m_pending) {
    std::string component;
    std::string action;
    if (!splitKeyPath(path, component, action)) {
      continue;
    }
    // actionId needs all four entries: [component, action, component
    // friendlyName, action friendlyName]; the daemon rejects shorter ids.
    std::string componentFriendlyName = component;
    std::string actionFriendlyName = action;
    const auto componentIt = std::ranges::find_if(
        m_components, [&component](const KeybindDbus::Component& c) { return c.uniqueName == component; }
    );
    if (componentIt != m_components.end() && !componentIt->friendlyName.empty()) {
      componentFriendlyName = componentIt->friendlyName;
      const auto actionIt = std::ranges::find_if(
          componentIt->actions, [&action](const KeybindDbus::Action& a) { return a.actionUnique == action; }
      );
      if (actionIt != componentIt->actions.end() && !actionIt->friendlyName.empty()) {
        actionFriendlyName = actionIt->friendlyName;
      }
    }
    if (!m_dbus.setActionShortcuts(component, action, actionFriendlyName, componentFriendlyName, chords)) {
      kLog.warn("keybinds apply failed for {}.{}; staged edits kept", component, action);
      return false;
    }
  }
  m_pending.clear();

  // Re-read the daemon (the source of truth) and rewrite the mirror with the
  // binds that actually differ from the defaults. The daemon persists its own
  // state to kglobalshortcutsrc itself; a failed mirror write is only a
  // warning — the binds are already live.
  m_components = m_dbus.components();
  filterComponents(m_components);
  orderComponents(m_components);
  std::vector<KeybindsKwe::Entry> entries;
  for (const auto& component : m_components) {
    for (const auto& action : component.actions) {
      if (action.active != action.defaults) {
        entries.push_back(KeybindsKwe::Entry{
            .component = component.uniqueName,
            .action = action.actionUnique,
            .chords = action.active,
        });
      }
    }
  }
  if (!KeybindsKwe::save(KeybindsKwe::defaultPath(), entries)) {
    kLog.warn("keybinds mirror write failed; binds are live but the mirror is stale");
  }
  return true;
}

void KeybindsPage::commitChord(
    const std::string& component, const std::string& action, std::vector<KeyChord> chords, std::size_t index,
    KeyChord chord, const Callbacks& callbacks
) {
  // Canonicalize through the daemon's integer encoding so staged chords compare
  // equal to what the daemon reports (letters uppercase, consumed Shift gone).
  const auto qt = KeybindCodec::toQtKey(chord);
  const auto canonical = qt.has_value() ? KeybindCodec::fromQtKey(*qt) : std::nullopt;
  if (!canonical.has_value()) {
    kLog.warn("keybinds commit dropped unrepresentable chord for {}.{}", component, action);
    return;
  }
  // A chord already bound to another action is a conflict; the action's own
  // current bindings are fine (re-committing a value or moving it between
  // slots of the same action). The daemon's globalShortcutsByKey method is
  // unavailable on this fork, so conflicts are computed from the loaded
  // components (all actions with their active chords).
  const auto conflicts = [&] {
    std::vector<std::tuple<std::string, std::string, std::string>> owners;
    for (const auto& candidate : m_components) {
      for (const auto& candidateAction : candidate.actions) {
        if (std::ranges::find(candidateAction.active, *canonical) != candidateAction.active.end()) {
          owners.emplace_back(candidate.uniqueName, candidateAction.actionUnique, candidateAction.friendlyName);
        }
      }
    }
    return owners;
  }();
  for (const auto& [ownerComponent, ownerAction, ownerFriendly] : conflicts) {
    if (ownerComponent != component || ownerAction != action) {
      notify::error(
          i18n::tr("notifications.internal.keybind-app"),
          i18n::tr("notifications.internal.keybind-conflict-title"),
          i18n::tr(
              "notifications.internal.keybind-conflict-body", "{keybind}", KeybindCodec::toChordString(*canonical),
              "{action}", ownerFriendly.empty() ? ownerAction : ownerFriendly,
              "{component}", componentDisplayName(
                                 *std::ranges::find_if(
                                     m_components,
                                     [&ownerComponent](const KeybindDbus::Component& c) {
                                       return c.uniqueName == ownerComponent;
                                     }
                                 )
                             )
          )
      );
      return;
    }
  }
  if (index < chords.size()) {
    chords[index] = *canonical;
  } else {
    chords.push_back(*canonical);
  }
  stageAndRebuild(component, action, std::move(chords), callbacks);
}

void KeybindsPage::removeChord(
    const std::string& component, const std::string& action, std::vector<KeyChord> chords, std::size_t index,
    const Callbacks& callbacks
) {
  if (index >= chords.size()) {
    return;
  }
  chords.erase(chords.begin() + static_cast<std::ptrdiff_t>(index));
  stageAndRebuild(component, action, std::move(chords), callbacks);
}

void KeybindsPage::buildChordRow(
    Flex& parent, std::vector<KeyChord> chords, std::size_t index, const std::string& component,
    const std::string& action, float scale, const Callbacks& callbacks
) {
  const bool isAddSlot = index >= chords.size();
  const bool custom = isCustomComponent(component);
  // Custom actions are deleted as a whole; every other bind drops just this
  // chord. The trash sits to the right of the recorder; an unbound custom
  // action still gets one (its only way to delete the action).
  const bool showTrash = !isAddSlot || custom;

  auto row = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceXs * scale,
  });
  row->addChild(ui::keybindRecorder({
      .chord = isAddSlot ? std::optional<KeyChord>{} : std::optional<KeyChord>(chords[index]),
      .scale = scale,
      .unsetPlaceholder = i18n::tr("settings.controls.keybind.unset-placeholder"),
      .recordingPlaceholder = i18n::tr("settings.controls.keybind.recording-placeholder"),
      .modifierPolicy = ModifierPolicy::Required,
      .allowSuper = true,
      .escapeCancels = true,
      .width = kChordRecorderWidth * scale,
      .onCommit =
          [this, component, action, chords, index, callbacks](KeyChord chord) {
            commitChord(component, action, chords, index, chord, callbacks);
          },
      .onRecordingChanged = [this](bool recording) {
        m_recordingCount += recording ? 1 : -1;
        if (m_recordingCount < 0) {
          m_recordingCount = 0;
        }
      },
  }));
  if (showTrash) {
    row->addChild(ui::button({
        .glyph = "trash",
        .fontSize = Style::fontSizeCaption * scale,
        .glyphSize = Style::fontSizeBody * scale,
        .variant = ButtonVariant::Ghost,
        .minWidth = Style::controlHeight * scale,
        .minHeight = Style::controlHeight * scale,
        .padding = Style::spaceXs * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick =
            [this, component, action, chords, index, callbacks, custom]() {
              if (custom) {
                removeCustomAction(component, action, callbacks);
              } else {
                removeChord(component, action, chords, index, callbacks);
              }
            },
    }));
  }
  parent.addChild(std::move(row));
}

void KeybindsPage::buildChordsColumn(
    Flex& parent, const std::string& component, const std::string& action, std::vector<KeyChord> chords,
    float scale, const Callbacks& callbacks
) {
  // One recorder per bound chord, an unset one when nothing is bound.
  const std::size_t slots = std::max<std::size_t>(1, chords.size());
  for (std::size_t i = 0; i < slots; ++i) {
    buildChordRow(parent, chords, i, component, action, scale, callbacks);
  }
}

void KeybindsPage::buildActionRow(
    Flex& parent, const KeybindDbus::Component& component, const KeybindDbus::Action& action, float scale,
    const Callbacks& callbacks
) {
  const bool isOverridden = overridden(component.uniqueName, action);
  const auto chords = effectiveChords(component.uniqueName, action.actionUnique);

  // One column per action: a title line ([name] ... [Override badge]) and one
  // line per chord ([recorder] [trash]).
  auto column = ui::column({
      .align = FlexAlign::Start,
      .gap = Style::spaceXs * scale,
      .paddingV = 2.0F * scale,
      .fillWidth = true,
  });
  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(makeLabel(
      action.friendlyName.empty() ? action.actionUnique : action.friendlyName, Style::fontSizeBody * scale,
      colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  titleRow->addChild(ui::spacer());
  if (isOverridden) {
    titleRow->addChild(makeOverrideBadge(scale));
  }
  column->addChild(std::move(titleRow));

  buildChordsColumn(*column, component.uniqueName, action.actionUnique, chords, scale, callbacks);
  parent.addChild(std::move(column));
}

bool KeybindsPage::isCustomComponent(std::string_view component) {
  return component == CustomKeybinds::kAppsComponent || component == CustomKeybinds::kCommandsComponent;
}

void KeybindsPage::removeCustomAction(
    const std::string& component, const std::string& action, const Callbacks& callbacks
) {
  // Best effort: unregister from the daemon and drop the definition; failures
  // are logged inside the service. The list is always re-read from the daemon
  // so the UI cannot keep showing an action that is already gone.
  m_customKeybinds.remove(component, action);
  m_pending.erase(keyPath(component, action));
  m_loaded = false;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::unique_ptr<Node> KeybindsPage::buildAddMenu(float scale, const Callbacks& callbacks) {
  auto menu = ui::column({.align = FlexAlign::End, .gap = Style::spaceXs * scale, .fillWidth = true});
  auto card = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceXs * scale,
      .padding = Style::spaceXs * scale,
      .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.5F),
      .radius = Style::scaledRadiusMd(scale),
      .width = 220.0F * scale,
  });
  const auto menuItem = [scale, callbacks](std::string_view text, auto onPick) {
    return ui::button({
        .text = std::string(text),
        .fontSize = Style::fontSizeCaption * scale,
        .contentAlign = ButtonContentAlign::Start,
        .variant = ButtonVariant::Ghost,
        .minHeight = Style::controlHeight * scale,
        .paddingV = Style::spaceXs * scale,
        .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = std::move(onPick),
    });
  };
  card->addChild(menuItem(
      i18n::tr("settings.keybinds.add-application"),
      [this, callbacks]() {
        m_addMenuOpen = false;
        m_addMode = AddMode::Apps;
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      }
  ));
  card->addChild(menuItem(
      i18n::tr("settings.keybinds.add-custom-command"),
      [this, callbacks]() {
        m_addMenuOpen = false;
        m_addMode = AddMode::Command;
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      }
  ));
  menu->addChild(std::move(card));
  return menu;
}

std::unique_ptr<Node> KeybindsPage::buildAppPicker(float scale, const Callbacks& callbacks) {
  auto column = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});
  column->addChild(makeLabel(
      i18n::tr("settings.keybinds.app-picker-title"), Style::fontSizeBody * scale,
      colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));

  std::vector<SearchPickerOption> options;
  for (const auto& entry : desktopEntries()) {
    if (entry.exec.empty()) {
      continue;
    }
    options.push_back(SearchPickerOption{
        .value = entry.id,
        .label = entry.name,
        .icon = "app-window",
    });
  }
  column->addChild(ui::searchPicker({
      .placeholder = i18n::tr("settings.keybinds.app-picker-placeholder"),
      .emptyText = i18n::tr("settings.keybinds.app-picker-empty"),
      .options = std::move(options),
      .height = 320.0F * scale,
      .onActivated =
          [this, callbacks](const SearchPickerOption& option) {
            const auto& entries = desktopEntries();
            const auto entry = std::ranges::find_if(
                entries, [&option](const DesktopEntry& e) { return e.id == option.value; }
            );
            if (entry == entries.end()) {
              return;
            }
            m_addMode = AddMode::None;
            m_addMenuOpen = false;
            if (m_customKeybinds.addApp(entry->id, entry->name, entry->exec)) {
              m_loaded = false;
            }
            if (callbacks.requestRebuild) {
              callbacks.requestRebuild();
            }
          },
      .onCancel =
          [this, callbacks]() {
            m_addMode = AddMode::None;
            if (callbacks.requestRebuild) {
              callbacks.requestRebuild();
            }
          },
      .configure =
          [](SearchPicker& picker) {
            picker.clearFill();
            picker.clearBorder();
            picker.setRadius(0.0F);
            picker.setPadding(0.0F);
          },
  }));

  column->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceXs * scale},
      ui::button({
          .text = i18n::tr("settings.keybinds.cancel"),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Ghost,
          .minHeight = Style::controlHeight * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = Style::spaceMd * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick =
              [this, callbacks]() {
                m_addMode = AddMode::None;
                if (callbacks.requestRebuild) {
                  callbacks.requestRebuild();
                }
              },
      })
  ));
  return column;
}

std::unique_ptr<Node> KeybindsPage::buildCommandForm(float scale, const Callbacks& callbacks) {
  auto column = ui::column({.align = FlexAlign::Start, .gap = Style::spaceSm * scale, .fillWidth = true});
  column->addChild(makeLabel(
      i18n::tr("settings.keybinds.command-form-title"), Style::fontSizeBody * scale,
      colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));

  auto input = std::make_unique<Input>();
  input->setPlaceholder(i18n::tr("settings.keybinds.command-placeholder"));
  input->setMinLayoutWidth(480.0F * scale);
  Input* inputPtr = input.get();
  input->setOnSubmit(
      [this, inputPtr, callbacks](const std::string& command) {
        if (command.empty()) {
          return;
        }
        m_addMode = AddMode::None;
        m_addMenuOpen = false;
        if (m_customKeybinds.addCommand(command)) {
          m_loaded = false;
        }
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      }
  );
  column->addChild(std::move(input));

  column->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceXs * scale},
      ui::button({
          .text = i18n::tr("settings.keybinds.add"),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Primary,
          .minHeight = Style::controlHeight * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = Style::spaceMd * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick =
              [this, inputPtr, callbacks]() {
                const std::string command = inputPtr->value();
                if (command.empty()) {
                  return;
                }
                m_addMode = AddMode::None;
                m_addMenuOpen = false;
                if (m_customKeybinds.addCommand(command)) {
                  m_loaded = false;
                }
                if (callbacks.requestRebuild) {
                  callbacks.requestRebuild();
                }
              },
      }),
      ui::button({
          .text = i18n::tr("settings.keybinds.cancel"),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Ghost,
          .minHeight = Style::controlHeight * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = Style::spaceMd * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick =
              [this, callbacks]() {
                m_addMode = AddMode::None;
                if (callbacks.requestRebuild) {
                  callbacks.requestRebuild();
                }
              },
      })
  ));
  return column;
}

std::unique_ptr<Node> KeybindsPage::buildComponentList(float scale, const Callbacks& callbacks) {
  auto list = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceXs * scale,
      .width = kComponentListWidth * scale,
  });
  for (const auto& component : m_components) {
    const bool selected = component.uniqueName == m_selectedComponent;
    list->addChild(ui::button({
        .text = componentDisplayName(component),
        .fontSize = Style::fontSizeCaption * scale,
        .contentAlign = ButtonContentAlign::Start,
        .variant = selected ? ButtonVariant::TabActive : ButtonVariant::Tab,
        .minHeight = Style::controlHeight * scale,
        .paddingV = Style::spaceXs * scale,
        .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick =
            [this, uniqueName = component.uniqueName, callbacks]() {
              m_selectedComponent = uniqueName;
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

std::unique_ptr<Node>
KeybindsPage::build(float scale, const Filter& filter, const Callbacks& callbacks) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  // Section header: glyph + bold primary title, with the "+" add button on the
  // far right of the title line.
  auto header = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  header->addChild(ui::glyph({
      .glyph = "keyboard",
      .glyphSize = Style::fontSizeHeader * scale,
      .color = colorSpecFromRole(ColorRole::Primary),
  }));
  header->addChild(makeLabel("Keybinds", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold));
  header->addChild(ui::spacer());
  header->addChild(ui::button({
      .glyph = "plus",
      .fontSize = Style::fontSizeCaption * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .variant = ButtonVariant::Ghost,
      .minWidth = Style::controlHeight * scale,
      .minHeight = Style::controlHeight * scale,
      .padding = Style::spaceXs * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick =
          [this, callbacks]() {
            m_addMenuOpen = !m_addMenuOpen;
            if (m_addMenuOpen) {
              m_addMode = AddMode::None;
            }
            if (callbacks.requestRebuild) {
              callbacks.requestRebuild();
            }
          },
  }));
  root->addChild(std::move(header));

  if (m_addMenuOpen) {
    root->addChild(buildAddMenu(scale, callbacks));
  }

  if (m_components.empty() && m_addMode == AddMode::None) {
    root->addChild(makeLabel(
        "The compositor's shortcut service is unavailable.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
    return root;
  }

  if (m_addMode == AddMode::Apps) {
    root->addChild(buildAppPicker(scale, callbacks));
    return root;
  }
  if (m_addMode == AddMode::Command) {
    root->addChild(buildCommandForm(scale, callbacks));
    return root;
  }

  if (m_selectedComponent.empty()
      || std::ranges::none_of(m_components, [this](const KeybindDbus::Component& c) {
           return c.uniqueName == m_selectedComponent;
         })) {
    m_selectedComponent = m_components.front().uniqueName;
  }

  // Component list on the left, actions on the right (KDE Shortcuts KCM layout).
  auto body = ui::row({
      .align = FlexAlign::Start,
      .gap = Style::spaceMd * scale,
      .fillWidth = true,
  });
  body->addChild(buildComponentList(scale, callbacks));
  body->addChild(ui::separator({
      .thickness = 1.0F,
      .spacing = Style::spaceXs * scale,
      .orientation = SeparatorOrientation::VerticalRule,
  }));

  auto actionsColumn = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .flexGrow = 1.0F,
  });
  bool anyRows = false;
  const auto componentIt = std::ranges::find_if(
      m_components, [this](const KeybindDbus::Component& c) { return c.uniqueName == m_selectedComponent; }
  );
  if (componentIt != m_components.end()) {
    for (const auto& action : componentIt->actions) {
      if (action.actionUnique.starts_with(kSessionActionPrefix)) {
        continue;
      }
      if (!matchesFilter(*componentIt, action, filter)) {
        continue;
      }
      buildActionRow(*actionsColumn, *componentIt, action, scale, callbacks);
      anyRows = true;
    }
  }
  if (!anyRows) {
    actionsColumn->addChild(makeLabel(
        "No keybinds match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  body->addChild(std::move(actionsColumn));
  root->addChild(std::move(body));
  return root;
}