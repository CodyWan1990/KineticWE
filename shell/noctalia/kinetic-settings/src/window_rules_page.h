#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;
class WaylandConnection;

// The Window Rules settings page: compositor-wide "Window defaults" plus the
// per-window rule book.
//
// * Window defaults: a "Toggle title bars" toggle that drives
//   [Windows] DefaultDecorationPolicy in kineticwe.kwe (None vs
//   ClientPreference). The compositor re-seeds every window's decoration base
//   on reconfigure (rules still win via checkDecorationPolicy).
//
// * Window rules: a list (+ Add / Duplicate / Delete / Move / Edit) of rules
//   stored in ~/.config/kineticwe/rules.kwe (one [<uuid>] group per rule,
//   keys matching rulesettings.kcfg so the file stays KWin-compatible). Each
//   rule is edited in a single form: window matching (window class / role /
//   title, each with a match type) and properties (above/below, skip taskbar/
//   pager/switcher, fullscreen, minimize, maximize, ignore geometry,
//   decoration policy, active/inactive opacity), each with a value control and
//   a policy dropdown (Do not affect / Force / Apply initially / ... and the
//   Unimportant/Exact/Substring/RegExp match types).
//
// Like the other pages, edits are STAGED: nothing touches disk until the
// window's Apply button calls applyAll(), which writes kineticwe.kwe and
// rules.kwe and then nudges the compositor to reload + re-apply.
class WindowRulesPage {
public:
  // Same shape as the other pages' filters (unused by the defaults section yet).
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  // Glue back to the owning window, same contract as the Keybinds page.
  struct Callbacks {
    std::function<void()> requestRebuild;
  };

  explicit WindowRulesPage(class SessionBus& bus, class WaylandConnection& wayland);

  std::unique_ptr<Node> build(float scale, const Filter& filter, const Callbacks& callbacks);

  // "Reset Page" scope key path (the single window-defaults toggle).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths();
  // Resets the defaults section back to the app default (title bars shown).
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept {
    return m_pending.has_value() || m_pendingActiveOpacity.has_value() || m_pendingInactiveOpacity.has_value() || m_rulesDirty;
  }

  // Writes the staged window defaults to kineticwe.kwe atomically. Returns
  // false on failure (staged edits are kept for retry).
  bool applyAll();

  // Drops staged edits and forces the next build() to re-read the file.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.reset();
    m_pendingActiveOpacity.reset();
    m_pendingInactiveOpacity.reset();
    m_errorNotice.clear();
    m_rulesLoaded = false;
    m_ruleList.clear();
    m_editingRule.reset();
    m_rulesDirty = false;
    m_newRuleDrafts.clear();
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }

private:
  // The toggle drives [Windows] DefaultDecorationPolicy: hidden -> None (0),
  // shown -> ClientPreference (1, the KWin default).
  static constexpr std::string_view kGroup = "Windows";
  static constexpr std::string_view kKey = "DefaultDecorationPolicy";
  static constexpr std::string_view kActiveOpacityKey = "DefaultActiveOpacity";
  static constexpr std::string_view kInactiveOpacityKey = "DefaultInactiveOpacity";
  static constexpr std::string_view kKeyPath = "Windows/DefaultDecorationPolicy";
  static constexpr std::string_view kActiveOpacityPath = "Windows/DefaultActiveOpacity";
  static constexpr std::string_view kInactiveOpacityPath = "Windows/DefaultInactiveOpacity";

  void ensureLoaded();
  // Whether the file currently hides title bars (value == None/0). Absent key
  // reads as ClientPreference (1) -> not hidden.
  [[nodiscard]] bool hideTitleBarsOnDisk() const;
  // What the UI shows: the staged value when present, else the file's.
  [[nodiscard]] bool effectiveHideTitleBars() const;
  // True when the effective value differs from the app default (not hidden).
  [[nodiscard]] bool overridden() const;
  // Stage a new toggle state; un-stages when it matches what's on disk.
  void stageHideTitleBars(bool hide, const Callbacks& callbacks);

  // ---- Global active/inactive opacity defaults (kineticwe.kwe [Windows]) ---
  [[nodiscard]] int activeOpacityOnDisk() const;
  [[nodiscard]] int inactiveOpacityOnDisk() const;
  [[nodiscard]] int effectiveActiveOpacity() const;
  [[nodiscard]] int effectiveInactiveOpacity() const;
  void stageActiveOpacity(int value, const Callbacks& callbacks);
  void stageInactiveOpacity(int value, const Callbacks& callbacks);
  // A 0-100% slider with a live value label; commits the int on drag end.
  [[nodiscard]] std::unique_ptr<Node> makePercentSlider(double value, std::function<void(int)> onCommit, float scale);
  // A "Title + subtitle on the left, control on the right" defaults row.
  [[nodiscard]] std::unique_ptr<Node> buildDefaultsRow(std::string_view title, std::string_view subtitle, std::unique_ptr<Node> control, float scale);

  // ---- Window rules (rules.kwe) --------------------------------------------
  // A rule is one [<uuid>] group in rules.kwe; `values` holds every config key
  // (Description, Enabled, wmclass, aboverule, ...) as raw strings.
  struct Rule {
    std::string id;
    std::map<std::string, std::string> values;
  };

  enum class RuleValueKind { Text, Bool, Option, Percentage, Point, Size, Desktop, Screen };
  enum class RulePolicyKind { None, StringMatch, SetRule, ForceRule };
  struct RuleField {
    std::string_view key;
    std::string_view policyKey; // empty string => no policy dropdown
    RuleValueKind valueKind;
    RulePolicyKind policyKind;
    std::string_view section;
    std::string_view title;
    bool advanced = false;
  };

  static const std::vector<RuleField>& ruleFields();
  static const std::vector<std::pair<int, std::string_view>>&
  policyOptions(RulePolicyKind kind);
  static const std::vector<std::pair<int, std::string_view>>&
  valueOptions(std::string_view key);
  // Whether a field participates (i.e. should be written) given its raw value
  // and policy. Matching fields are active only when they have a value AND a
  // match type (so an empty class/role/title doesn't silently match nothing);
  // property fields are active when their policy != "not set" (0).
  static bool fieldActive(const RuleField& field, const std::string& value, int policy);

  void ensureRulesLoaded();
  void reloadRules();
  [[nodiscard]] std::string ruleDescription(const Rule& rule) const;
  void writeRuleGroup(const Rule& rule);
  bool writeRulesBook();
  [[nodiscard]] bool rulesHavePending() const { return m_rulesDirty; }

  [[nodiscard]] std::unique_ptr<Node> buildRulesSection(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildRuleList(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildRuleEditor(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildRuleFieldRow(std::size_t ruleIndex, const RuleField& field, float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node> buildMatchingHeader(float scale, std::string_view text);

  void addRule(const Callbacks& callbacks);
  void duplicateRule(std::size_t index, const Callbacks& callbacks);
  void removeRule(std::size_t index, const Callbacks& callbacks);
  void moveRule(std::size_t from, std::size_t to, const Callbacks& callbacks);
  void editRule(std::size_t index, const Callbacks& callbacks);
  void cancelEdit(const Callbacks& callbacks);
  void setRuleValue(std::size_t index, const std::string& key, std::string value, const Callbacks& callbacks);
  void setRulePolicy(std::size_t index, const std::string& policyKey, int policy, const Callbacks& callbacks);
  // Runs the compositor's interactive window pick and fills the rule's matching
  // + geometry from the selected window.
  void detectWindow(std::size_t index, const Callbacks& callbacks);

  CompositorDbus m_dbus;
  WaylandConnection& m_wayland; // live monitor (output) list for the Monitor rule
  KineticweKwe m_config;      // kineticwe.kwe (window defaults)
  KineticweKwe m_rulesConfig; // rules.kwe (window rules)
  bool m_loaded = false;
  // Staged "hide title bars" value; nullopt = nothing staged.
  std::optional<bool> m_pending;
  // Staged global opacity values (0-100); nullopt = nothing staged.
  std::optional<int> m_pendingActiveOpacity;
  std::optional<int> m_pendingInactiveOpacity;
  // The rule book, loaded from rules.kwe.
  std::vector<Rule> m_ruleList;
  // Index into m_ruleList being edited, if any.
  std::optional<std::size_t> m_editingRule;
  // ids of rules created via "Add" that haven't been modified yet. Pressing
  // Done discards them (so an unedited "Add" doesn't leave an empty rule); any
  // real edit promotes them to a real rule.
  std::set<std::string> m_newRuleDrafts;
  bool m_rulesLoaded = false;
  bool m_rulesDirty = false;
  // Message shown when the last Apply failed (cleared on next Apply/reload).
  std::string m_errorNotice;
};
