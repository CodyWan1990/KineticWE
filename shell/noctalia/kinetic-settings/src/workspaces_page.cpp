#include "workspaces_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
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
#include <cstdint>
#include <format>
#include <string>
#include <type_traits>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;
  constexpr float kWorkspaceNameInputWidth = 220.0F;

  // Compositor-side cap (VirtualDesktopManager::maximum()).
  constexpr int kMaximumWorkspaces = 25;
  // KDE default layout: a single desktop over two rows.
  constexpr int kDefaultWorkspaceRows = 2;
  // OSD hide delay defaults, mirroring src/kcms/desktop/virtualdesktopssettings.kcfg.
  constexpr int kOsdHideDelayMin = 0;
  constexpr int kOsdHideDelayMax = 10000;
  constexpr int kOsdHideDelayStep = 100;

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
  // SettingsControlFactory::makeStatusBadge (same as the Tiling page).
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
      std::string path, bool pendingConfirmation, float scale, const WorkspacesPage::Callbacks& callbacks
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

WorkspacesPage::WorkspacesPage(SessionBus& bus) : m_bus(bus), m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Schema: option catalogs + the field descriptor table — the single source of
// truth for file keys, defaults, ranges and presentation. Defaults mirror
// src/kcms/desktop/virtualdesktopssettings.kcfg and the slide effect's
// EnabledByDefault; keys are written verbatim, which is what the compositor
// parses natively. `inverted` fields store the negation of what the UI shows
// (the OSD "show layout indicators" toggle maps to [Script-desktopchangeosd]
// TextOnly).
const std::vector<WorkspacesPage::Field>& WorkspacesPage::fields() {
  static const std::vector<Field> table = [] {
    std::vector<Field> f;
    // Options ----------------------------------------------------------------
    f.push_back(Field{
        .group = "Windows",
        .key = "RollOverDesktops",
        .title = "Navigation wraps around",
        .subtitle = "Moving past the first or last workspace wraps around to "
                    "the opposite end",
        .pageGroup = "Options",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "PerOutputVirtualDesktops",
        .title = "Switch workspaces independently for each screen",
        .subtitle = "Each screen keeps its own current workspace instead of "
                    "all screens sharing one",
        .pageGroup = "Options",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Windows",
        .key = "InvertScrollDesktopSwitch",
        .title = "Swap scroll modifiers for moving windows with workspace switch",
        .subtitle = "Inverts which scroll modifier moves the active window "
                    "instead of switching workspaces",
        .pageGroup = "Options",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Plugins",
        .key = "slideEnabled",
        .title = "Show animation when switching",
        .subtitle = "Slides between workspaces while switching (the Slide "
                    "effect)",
        .pageGroup = "Options",
        .fallback = Value{true},
    });
    f.push_back(Field{
        .group = "Effect-slide",
        .key = "SlideBackground",
        .title = "Slide desktop background",
        .subtitle = "Also slides the wallpaper along with the windows while "
                    "switching",
        .pageGroup = "Options",
        .fallback = Value{true},
    });
    // On-screen display ------------------------------------------------------
    f.push_back(Field{
        .group = "Plugins",
        .key = "desktopchangeosdEnabled",
        .title = "Show on-screen display when switching",
        .subtitle = "Displays the workspace name and layout in an overlay for "
                    "a short time whenever the workspace changes",
        .pageGroup = "On-screen display",
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Script-desktopchangeosd",
        .key = "PopupHideDelay",
        .control = ControlKind::Stepper,
        .title = "On-screen display duration",
        .subtitle = "How long the on-screen display stays visible after a "
                    "workspace switch",
        .pageGroup = "On-screen display",
        .advanced = true,
        .minValue = kOsdHideDelayMin,
        .maxValue = kOsdHideDelayMax,
        .step = kOsdHideDelayStep,
        .valueSuffix = "ms",
        .fallback = Value{1000},
    });
    f.push_back(Field{
        .group = "Script-desktopchangeosd",
        .key = "TextOnly",
        .title = "Show workspace layout indicators in the on-screen display",
        .subtitle = "Also draws the workspace layout grid instead of text only",
        .pageGroup = "On-screen display",
        .advanced = true,
        .inverted = true, // the file stores TextOnly; the UI checkbox is the negation
        .fallback = Value{false},
    });
    f.push_back(Field{
        .group = "Script-desktopchangeosd",
        .key = "ShowTilingLayout",
        .title = "Show current tiling layout in the on-screen display",
        .subtitle = "Also draws the active tiling layout while the overlay is up",
        .pageGroup = "On-screen display",
        .advanced = true,
        .fallback = Value{false},
    });
    return f;
  }();
  return table;
}
// ---------------------------------------------------------------------------
// Values: defaults, effective (config-backed) state, override detection.
// ---------------------------------------------------------------------------

std::string WorkspacesPage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const WorkspacesPage::Field* WorkspacesPage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

WorkspacesPage::Value WorkspacesPage::defaultValue(const Field& field) {
  return field.fallback;
}

bool WorkspacesPage::valuesEqual(const Field& /*field*/, const Value& a, const Value& b) {
  return a == b;
}

bool WorkspacesPage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(field, value, defaultValue(field));
}

void WorkspacesPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

WorkspacesPage::Value WorkspacesPage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else {
          const auto parsed = m_config.readInt(field.group, field.key, fallbackValue);
          return std::clamp(parsed, field.minValue, field.maxValue);
        }
      },
      fallback
  );
}

WorkspacesPage::Value WorkspacesPage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool WorkspacesPage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool WorkspacesPage::matchesFilter(const Field& field, const Filter& filter) const {
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
// key once applied, mirroring Noctalia's setOverride/clearOverride behavior.
// ---------------------------------------------------------------------------

void WorkspacesPage::writeValue(const Field& field, const Value& value) {
  if (equalsDefault(field, value)) {
    m_config.removeKey(field.group, field.key);
  } else {
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
}

void WorkspacesPage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (valuesEqual(field, value, fileBackedValue(field))) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void WorkspacesPage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> WorkspacesPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void WorkspacesPage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void WorkspacesPage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
  // Workspace list back to the KDE default: one workspace over two rows. The
  // first workspace keeps its current name (KDE preserves it too); removing it
  // would leave zero workspaces, which the compositor refuses.
  loadLiveWorkspaces();
  if (!m_workspaces.empty()) {
    for (std::size_t i = 1; i < m_workspaces.size(); ++i) {
      m_workspaces[i].pendingRemoval = true;
    }
  }
  m_workspaceRows = kDefaultWorkspaceRows;
  m_workspaceListDirty = true;
}

bool WorkspacesPage::applyAll() {
  ensureLoaded();
  const bool hasConfig = !m_pending.empty();
  const bool hasWorkspaces = m_workspaceListDirty;
  bool slideConfigChanged = false;

  // Compose the staged settings into the config model. The row layout is also
  // written here (see syncWorkspacesToCompositor for why).
  if (hasConfig) {
    for (const auto& [path, value] : m_pending) {
      if (const Field* field = findField(path)) {
        writeValue(*field, value);
        if (field->group == "Effect-slide") {
          slideConfigChanged = true;
        }
      }
    }
  }
  if (hasWorkspaces) {
    m_config.setInt("Desktops", "Rows", m_workspaceRows);
  }

  // The (first, gating) write: staged edits are kept for retry when it fails.
  if ((hasConfig || hasWorkspaces) && !m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  if (hasWorkspaces) {
    syncWorkspacesToCompositor();
    // The compositor's create/remove/rename calls each save() the [Desktops]
    // group, which would rewrite Rows before the D-Bus rows change (setRows
    // does not save). Refresh the file so the persisted layout is the one we
    // just set; a later compositor save writes the same live value, so the two
    // stay converged.
    m_config.load();
    m_config.setInt("Desktops", "Rows", m_workspaceRows);
    if (!m_config.save()) {
      kLog.warn("kinetic-settings: failed to refresh rows in {}", KineticweKwe::defaultPath().string());
    }
    m_workspaceListDirty = false;
    m_liveLoaded = false; // re-query the (now synced) list on the next build
  }

  m_pending.clear();

  // One compositor reload per Apply (batching every staged change, whether it
  // was a settings value or a workspace-list edit).
  if (hasConfig || hasWorkspaces) {
    m_dbus.requestReconfigure();
  }

  // The full reconfigure re-reads the config file but does not re-read effect
  // configs, so reload the Slide effect explicitly when its [Effect-slide]
  // group changed (mirrors the upstream Slide KCM calling reconfigureEffect).
  if (slideConfigChanged) {
    m_dbus.reconfigureEffect("slide");
  }
  return true;
}

void WorkspacesPage::syncWorkspacesToCompositor() {
  // The desired list is the staged working copy minus pending removals, in
  // order (existing workspaces keep their positions; new ones are appended).
  const auto kept = [](const WorkspaceEntry& e) { return !e.pendingRemoval; };
  const auto existingId = [](const WorkspaceEntry& e) {
    return !e.pendingRemoval && !e.id.empty();
  };

  // 1. Remove workspaces that are gone from the staged list.
  for (const auto& snapshot : m_snapshot) {
    const bool stillPresent = std::ranges::any_of(m_workspaces, [&snapshot](const WorkspaceEntry& e) {
      return !e.pendingRemoval && e.id == snapshot.id;
    });
    if (!stillPresent) {
      m_dbus.removeDesktop(snapshot.id);
    }
  }

  // 2. Create the appended workspaces at their desired index. Removals above
  // only remove entries absent from the desired list, so the relative order of
  // the survivors is unchanged and each new workspace's index equals the live
  // count at the time of its creation (the compositor appends and persists).
  for (std::size_t i = 0; i < m_workspaces.size(); ++i) {
    const WorkspaceEntry& entry = m_workspaces[i];
    if (kept(entry) && entry.id.empty()) {
      m_dbus.createDesktop(static_cast<std::uint32_t>(i), entry.name);
    }
  }

  // 3. Rename workspaces whose name changed.
  for (std::size_t i = 0; i < m_workspaces.size(); ++i) {
    const WorkspaceEntry& entry = m_workspaces[i];
    if (!existingId(entry) || i >= m_snapshot.size()) {
      continue;
    }
    const WorkspaceEntry& baseline = m_snapshot[i];
    if (baseline.id == entry.id && baseline.name != entry.name) {
      m_dbus.setDesktopName(entry.id, entry.name);
    }
  }

  // 4. Apply the row layout (the D-Bus setter updates it live but does not
  // persist — applyAll() writes [Desktops] Rows afterwards).
  const auto rows = static_cast<std::uint32_t>(
      std::clamp(m_workspaceRows, 1, std::max(1, static_cast<int>(visibleCount())))
  );
  if (rows != static_cast<std::uint32_t>(m_snapshotRows)) {
    m_dbus.setVirtualDesktopRows(rows);
  }
}
// ---------------------------------------------------------------------------
// Staged workspace list: snapshot + working copy. All edits are local; only
// applyAll() (via syncWorkspacesToCompositor) talks to the compositor.
// ---------------------------------------------------------------------------

std::string WorkspacesPage::defaultNameFor(int position) {
  return std::format("Workspace {}", position);
}

std::size_t WorkspacesPage::visibleCount() const {
  return std::ranges::count_if(m_workspaces, [](const WorkspaceEntry& e) { return !e.pendingRemoval; });
}

bool WorkspacesPage::workspaceListClean() const {
  if (m_workspaceRows != m_snapshotRows || m_workspaces.size() != m_snapshot.size()) {
    return false;
  }
  for (std::size_t i = 0; i < m_workspaces.size(); ++i) {
    if (m_workspaces[i].pendingRemoval || m_workspaces[i].id != m_snapshot[i].id
        || m_workspaces[i].name != m_snapshot[i].name) {
      return false;
    }
  }
  return true;
}

void WorkspacesPage::loadLiveWorkspaces() {
  ensureLoaded();
  m_snapshot.clear();
  m_workspaces.clear();
  for (const auto& info : m_dbus.virtualDesktops()) {
    m_snapshot.push_back(WorkspaceEntry{.id = info.id, .name = info.name, .pendingRemoval = false});
    m_workspaces.push_back(WorkspaceEntry{.id = info.id, .name = info.name, .pendingRemoval = false});
  }
  const auto rows = m_dbus.virtualDesktopRows();
  m_snapshotRows = rows > 0 ? static_cast<int>(rows) : kDefaultWorkspaceRows;
  m_workspaceRows = m_snapshotRows;
  m_workspaceListDirty = false;
  m_liveLoaded = true;
}

void WorkspacesPage::addWorkspace() {
  ensureLoaded();
  if (visibleCount() >= static_cast<std::size_t>(kMaximumWorkspaces)) {
    return;
  }
  // Appended at the end (KDE's DesktopsModel does the same); the number is
  // `count + 1`, matching KWin's default workspace naming.
  m_workspaces.push_back(WorkspaceEntry{
      .id = {},
      .name = defaultNameFor(static_cast<int>(m_workspaces.size()) + 1),
      .pendingRemoval = false,
  });
  m_workspaceListDirty = true;
}

void WorkspacesPage::removeWorkspace(std::size_t index) {
  if (index >= m_workspaces.size() || visibleCount() <= 1) {
    return; // never stage a removal that would leave zero workspaces
  }
  m_workspaces[index].pendingRemoval = true;
  m_workspaceListDirty = true;
  // Keep the staged row count valid for the post-removal count.
  m_workspaceRows = std::clamp(m_workspaceRows, 1, std::max(1, static_cast<int>(visibleCount())));
}

void WorkspacesPage::renameWorkspace(std::size_t index, std::string name) {
  if (index >= m_workspaces.size() || m_workspaces[index].pendingRemoval) {
    return;
  }
  const std::string trimmed = StringUtils::trim(name);
  if (trimmed.empty() || trimmed == m_workspaces[index].name) {
    return; // nothing staged; the rebuild restores the current value
  }
  m_workspaces[index].name = std::move(trimmed);
  m_workspaceListDirty = true;
}

void WorkspacesPage::setWorkspaceRows(int rows) {
  ensureLoaded();
  const int clamped = std::clamp(rows, 1, std::max(1, static_cast<int>(visibleCount())));
  m_workspaceRows = clamped;
  // Re-derive the dirty flag: a row change back to the snapshot value clears it
  // when nothing else on the page is staged.
  m_workspaceListDirty = !workspaceListClean();
}
// ---------------------------------------------------------------------------
// Controls (Noctalia settings visuals, re-implemented module-locally).
// ---------------------------------------------------------------------------

std::unique_ptr<Node>
WorkspacesPage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  switch (field.control) {
  case ControlKind::Toggle: {
    // `inverted` fields store the negation of what the checkbox shows (e.g. the
    // OSD "show layout indicators" toggle writes TextOnly).
    const bool checked = std::get<bool>(effectiveValue(field));
    return ui::toggle({
        .checked = field.inverted ? !checked : checked,
        .scale = scale,
        .onChange = [this, field, callbacks](bool value) {
          stageAndRebuild(field, Value{field.inverted ? !value : value}, callbacks);
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
  }
  return ui::node({});
}

// One settings row in the exact Noctalia layout: bold title (+ Advanced badge)
// and caption description on the left; [Override badge][Reset][control] on the
// right. Badges only appear while the value differs from the default.
void WorkspacesPage::addSettingRow(
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
// ---------------------------------------------------------------------------
// Workspace list section (staged; applied with the rest on Apply).
// ---------------------------------------------------------------------------

void WorkspacesPage::buildWorkspaceRow(
    Flex& parent, std::size_t index, float scale, const Callbacks& callbacks
) {
  const WorkspaceEntry& entry = m_workspaces[index];
  const bool onlyOne = visibleCount() <= 1;

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(makeLabel(
      std::format("Workspace {}", index + 1), Style::fontSizeCaption * scale,
      colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));
  copy->addChild(ui::input({
      .value = entry.name,
      .placeholder = "Workspace name",
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .horizontalPadding = Style::spaceSm * scale,
      .width = kWorkspaceNameInputWidth * scale,
      .height = Style::controlHeight * scale,
      .onSubmit = [this, index, callbacks](const std::string& value) {
        renameWorkspace(index, value);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      },
      .submitOnFocusLoss = true,
  }));

  auto row = ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceSm * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true}
  );
  row->addChild(std::move(copy));
  row->addChild(ui::button({
      .glyph = "trash",
      .fontSize = Style::fontSizeCaption * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .enabled = !onlyOne,
      .variant = ButtonVariant::Ghost,
      .minWidth = Style::controlHeight * scale,
      .minHeight = Style::controlHeight * scale,
      .padding = Style::spaceXs * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick = [this, index, callbacks]() {
        removeWorkspace(index);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      },
  }));
  parent.addChild(std::move(row));
}

void WorkspacesPage::buildWorkspacesSection(Flex& parent, float scale, const Callbacks& callbacks) {
  const std::size_t count = visibleCount();
  const int rows = std::clamp(m_workspaceRows, 1, std::max(1, static_cast<int>(count)));
  const std::size_t perRow = std::max<std::size_t>(
      1,
      static_cast<std::size_t>(std::ceil(static_cast<double>(count) / static_cast<double>(rows)))
  );

  // Number of rows (KDE's "Rows:" spinbox).
  auto rowsRow = ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceXs * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true}
  );
  auto rowsCopy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  rowsCopy->addChild(makeLabel(
      "Number of rows", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  rowsCopy->addChild(
      makeLabel(
          "How the workspaces are arranged in the switcher", Style::fontSizeCaption * scale,
          colorSpecFromRole(ColorRole::OnSurfaceVariant)
      )
  );
  rowsRow->addChild(std::move(rowsCopy));
  rowsRow->addChild(ui::stepper({
      .minValue = 1,
      .maxValue = std::max(1, static_cast<int>(count)),
      .step = 1,
      .value = rows,
      .scale = scale,
      .onValueCommitted = [this, callbacks](int committed) {
        setWorkspaceRows(committed);
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      },
  }));
  parent.addChild(std::move(rowsRow));

  // The workspace list, grouped by row when more than one row is used.
  std::size_t visibleIndex = 0;
  for (std::size_t i = 0; i < m_workspaces.size(); ++i) {
    if (m_workspaces[i].pendingRemoval) {
      continue;
    }
    if (rows > 1 && visibleIndex % perRow == 0) {
      parent.addChild(ui::row(
          {.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .paddingV = Style::spaceSm * scale},
          makeLabel(
              std::format("Row {}", (visibleIndex / perRow) + 1), Style::fontSizeCaption * scale,
              colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
          )
      ));
    }
    buildWorkspaceRow(parent, i, scale, callbacks);
    ++visibleIndex;
  }

  // Add Workspace.
  parent.addChild(ui::button({
      .text = "Add Workspace",
      .glyph = "plus",
      .fontSize = Style::fontSizeBody * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .enabled = count < static_cast<std::size_t>(kMaximumWorkspaces),
      .variant = ButtonVariant::Default,
      .minHeight = Style::controlHeight * scale,
      .paddingV = Style::spaceSm * scale,
      .paddingH = Style::spaceMd * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick = [this, callbacks]() {
        addWorkspace();
        if (callbacks.requestRebuild) {
          callbacks.requestRebuild();
        }
      },
  }));
}
std::unique_ptr<Node>
WorkspacesPage::build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks) {
  ensureLoaded();
  if (!m_liveLoaded) {
    loadLiveWorkspaces();
  }

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
          .glyph = "device-desktop",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Workspaces", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  bool anyRows = false;
  bool firstGroup = true;
  std::string_view lastGroup;
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

  for (const auto& field : fields()) {
    if (!matchesFilter(field, filter)) {
      continue;
    }
    if (field.pageGroup != lastGroup) {
      lastGroup = field.pageGroup;
      addGroupHeader(lastGroup);
    }
    addSettingRow(*root, field, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  // Workspace-list section: live state staged locally, applied on Apply. It is
  // not an "override" so the Overridden-only filter hides it, but it is
  // reachable through search.
  const bool showWorkspaceList = !filter.showOverriddenOnly && [&filter]() {
    if (filter.searchQuery.empty()) {
      return true;
    }
    return StringUtils::containsInsensitive("workspaces", filter.searchQuery)
           || StringUtils::containsInsensitive("rows", filter.searchQuery)
           || StringUtils::containsInsensitive("add workspace", filter.searchQuery);
  }();
  if (showWorkspaceList) {
    addGroupHeader("Workspaces");
    buildWorkspacesSection(*root, scale, callbacks);
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
// ---------------------------------------------------------------------------