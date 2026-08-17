#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

class Flex;
class Node;

// The Workspaces settings page: edits the virtual-desktop-related keys in
// ~/.config/kineticwe/kineticwe.kwe ([Windows], [Plugins],
// [Script-desktopchangeosd], [Desktops]) plus the live workspace list — same
// schema mirror as src/kcms/desktop/virtualdesktopssettings.kcfg — and applies
// them via the compositor's org.kde.KWin reconfigure().
//
// The page mirrors the Noctalia settings content layout exactly like the
// Tiling page (glyph + title header, group headers, rows with a bold title, a
// caption description and the control on the right; Override badges and
// two-stage Reset buttons for values that differ from the compiled-in
// defaults).
//
// Edits are STAGED, never applied live: controls update an in-memory pending
// map (and a staged copy of the workspace list) and nothing touches the
// compositor until the window's Apply button flushes everything — one file
// write, one D-Bus workspace sync and one reconfigure() per Apply. Closing the
// window discards staged edits.
class WorkspacesPage {
public:
  // Persisted values are typed so override detection compares semantic values,
  // not file text.
  using Value = std::variant<bool, int>;

  // Filters driven by the window chrome (search field, Advanced toggle,
  // Overridden toggle).
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as TilingPage::Callbacks.
  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit WorkspacesPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the "group.key" path whose Reset
  // button was clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // "group.key" paths of every resettable setting on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one setting back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  // Stages every persisted setting back at its default and resets the staged
  // workspace list to the KDE default (one workspace, two rows). The workspace
  // changes land only when the user hits Apply.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  // Covers both the config-backed fields and the staged workspace list.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty() || m_workspaceListDirty; }

  // Writes every staged edit to the .kwe file, syncs the staged workspace list
  // to the compositor over D-Bus (create/remove/rename/set rows) and reloads
  // the compositor ONCE. Requires a config write even when only the workspace
  // list changed, because the compositor's D-Bus setRows does not call save().
  // Returns false when the (first, gating) file write failed — the staged edits
  // are kept for retry and nothing reaches the compositor.
  bool applyAll();

  // Force the next build() to re-read the config file from disk and the
  // workspace list from the compositor, discarding any staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_liveLoaded = false;
    m_workspaceListDirty = false;
  }

private:
  enum class ControlKind {
    Toggle,
    Stepper,
  };

  struct Field {
    std::string_view group;      // .kwe group: "Windows" / "Plugins" / ...
    std::string_view key;        // .kwe key
    ControlKind control = ControlKind::Toggle;
    std::string_view title;
    std::string_view subtitle;
    std::string_view pageGroup;  // group header shown on the page
    bool advanced = false;
    int minValue = 0;            // steppers
    int maxValue = 100;
    int step = 1;
    std::string_view valueSuffix;
    bool inverted = false;       // the UI toggle is the negation of the stored bool
    Value fallback;              // compiled-in default (override detection)
  };

  // One workspace in the staged working copy. Newly added workspaces carry an
  // empty id until Apply (the compositor generates the real id).
  struct WorkspaceEntry {
    std::string id;
    std::string name;
    bool pendingRemoval = false;
  };

  static const std::vector<Field>& fields();
  [[nodiscard]] static std::string keyPath(const Field& field);
  [[nodiscard]] static const Field* findField(std::string_view keyPath);
  [[nodiscard]] static Value defaultValue(const Field& field);
  [[nodiscard]] static bool valuesEqual(const Field& field, const Value& a, const Value& b);
  [[nodiscard]] static bool equalsDefault(const Field& field, const Value& value);

  void ensureLoaded();
  // Value read from the config file (ignoring staged edits).
  [[nodiscard]] Value fileBackedValue(const Field& field) const;
  // What the UI shows: the staged value when present, otherwise the file value.
  [[nodiscard]] Value effectiveValue(const Field& field) const;
  [[nodiscard]] bool overridden(const Field& field) const;
  [[nodiscard]] bool matchesFilter(const Field& field, const Filter& filter) const;

  // Writes `value` into the config model (or removes the key when it equals the
  // default). No save / no compositor reload — applyAll() owns those.
  void writeValue(const Field& field, const Value& value);
  // Stage an edit; un-stages when it matches the value already in the file.
  void stage(const Field& field, const Value& value);
  void stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks);

  [[nodiscard]] std::unique_ptr<Node>
  buildControl(const Field& field, float scale, const Callbacks& callbacks);
  void addSettingRow(
      Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );

  // Staged workspace list (org.kde.KWin.VirtualDesktopManager over D-Bus).
  // loadLiveWorkspaces() refreshes the working copy AND the snapshot from the
  // compositor; every subsequent edit mutates the working copy only, marking
  // m_workspaceListDirty so Apply knows to sync. Nothing is sent live.
  void loadLiveWorkspaces();
  // Staged workspace-list edits. None of these reach the compositor until
  // applyAll() syncs the dirty list.
  void addWorkspace();
  void removeWorkspace(std::size_t index);
  void renameWorkspace(std::size_t index, std::string name);
  void setWorkspaceRows(int rows);
  // Sends the staged list to the compositor (remove / create / rename / rows).
  // Called only from applyAll() after the config file write succeeded.
  void syncWorkspacesToCompositor();
  // 1-based default name for a new workspace "Workspace N".
  [[nodiscard]] static std::string defaultNameFor(int position);
  [[nodiscard]] std::size_t visibleCount() const;
  [[nodiscard]] bool workspaceListClean() const;

  void buildWorkspacesSection(Flex& parent, float scale, const Callbacks& callbacks);
  void buildWorkspaceRow(Flex& parent, std::size_t index, float scale, const Callbacks& callbacks);

  SessionBus& m_bus;
  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by "group.key"; flushed by applyAll().
  std::map<std::string, Value> m_pending;

  // Live workspace state (snapshot read from the compositor) + staged copy.
  bool m_liveLoaded = false;
  std::vector<WorkspaceEntry> m_snapshot;
  std::vector<WorkspaceEntry> m_workspaces;
  int m_snapshotRows = 2;
  int m_workspaceRows = 2;
  bool m_workspaceListDirty = false;
};