#include "window_rules_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "ui/builders.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

  constexpr Logger kLog("kinetic-settings");

  [[nodiscard]] std::unique_ptr<Label>
  makeLabel(std::string_view text, float fontSize, const ColorSpec& color, FontWeight fontWeight = FontWeight::Normal) {
    return ui::label({
        .text = std::string(text),
        .fontSize = fontSize,
        .fontWeight = fontWeight,
        .color = color,
    });
  }

  [[nodiscard]] std::vector<std::string>
  optionLabels(const std::vector<std::pair<int, std::string_view>>& options) {
    std::vector<std::string> out;
    out.reserve(options.size());
    for (const auto& [value, label] : options) {
      out.emplace_back(label);
    }
    return out;
  }

  [[nodiscard]] std::optional<std::size_t>
  optionIndex(const std::vector<std::pair<int, std::string_view>>& options, int value) {
    for (std::size_t i = 0; i < options.size(); ++i) {
      if (options[i].first == value) {
        return i;
      }
    }
    return std::nullopt;
  }

  // A unique-ish group id for newly created rules (rules.kwe group names just
  // need to be distinct; the compositor itself uses UUIDs).
  [[nodiscard]] std::string nextRuleId() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return "rule-" + std::to_string(now);
  }

} // namespace

WindowRulesPage::WindowRulesPage(SessionBus& bus) : m_dbus(bus) {
  m_rulesConfig.setPath(KineticweKwe::rulesDefaultPath());
}

void WindowRulesPage::ensureLoaded() {
  if (m_loaded) {
    return;
  }
  // A missing/unreadable file is fine: readInt() falls back to the default.
  m_config.load();
  ensureRulesLoaded();
  m_loaded = true;
}

// ---- Window defaults -------------------------------------------------------

bool WindowRulesPage::hideTitleBarsOnDisk() const {
  // DefaultDecorationPolicy: 0 = None (hidden), 1 = ClientPreference (shown).
  // Absent key reads as ClientPreference.
  return m_config.readInt(kGroup, kKey, 1) == 0;
}

bool WindowRulesPage::effectiveHideTitleBars() const {
  return m_pending.value_or(hideTitleBarsOnDisk());
}

bool WindowRulesPage::overridden() const {
  return effectiveHideTitleBars();
}

void WindowRulesPage::stageHideTitleBars(bool hide, const Callbacks& callbacks) {
  ensureLoaded();
  if (hide == hideTitleBarsOnDisk()) {
    m_pending.reset();
  } else {
    m_pending = hide;
  }
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

// ---- Global opacity defaults -----------------------------------------------

int WindowRulesPage::activeOpacityOnDisk() const {
  return m_config.readInt(kGroup, kActiveOpacityKey, 100);
}

int WindowRulesPage::inactiveOpacityOnDisk() const {
  return m_config.readInt(kGroup, kInactiveOpacityKey, 100);
}

int WindowRulesPage::effectiveActiveOpacity() const {
  return m_pendingActiveOpacity.value_or(activeOpacityOnDisk());
}

int WindowRulesPage::effectiveInactiveOpacity() const {
  return m_pendingInactiveOpacity.value_or(inactiveOpacityOnDisk());
}

void WindowRulesPage::stageActiveOpacity(int value, const Callbacks& callbacks) {
  ensureLoaded();
  if (value == activeOpacityOnDisk()) {
    m_pendingActiveOpacity.reset();
  } else {
    m_pendingActiveOpacity = std::clamp(value, 0, 100);
  }
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::stageInactiveOpacity(int value, const Callbacks& callbacks) {
  ensureLoaded();
  if (value == inactiveOpacityOnDisk()) {
    m_pendingInactiveOpacity.reset();
  } else {
    m_pendingInactiveOpacity = std::clamp(value, 0, 100);
  }
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::unique_ptr<Node>
WindowRulesPage::makePercentSlider(double value, std::function<void(int)> onCommit, float scale) {
  const double initial = std::clamp(value, 0.0, 100.0);
  Label* valuePtr = nullptr;
  auto valueLabel = ui::label({
      .text = std::to_string(static_cast<int>(initial)) + "%",
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .minWidth = 48.0F * scale,
  });
  valuePtr = valueLabel.get();
  auto last = std::make_shared<double>(initial);
  auto slider = ui::slider({
      .minValue = 0.0,
      .maxValue = 100.0,
      .step = 1.0,
      .value = initial,
      .trackHeight = Style::sliderTrackHeight * scale,
      .thumbSize = Style::sliderThumbSize * scale,
      .controlHeight = Style::controlHeight * scale,
      .width = 140.0F * scale,
      .onValueChanged =
          [valuePtr, last](double v) {
            *last = v;
            if (valuePtr != nullptr) {
              valuePtr->setText(std::to_string(static_cast<int>(std::lround(v))) + "%");
            }
          },
      .onDragEnd =
          [onCommit, last]() {
            if (onCommit) {
              onCommit(static_cast<int>(std::lround(*last)));
            }
          },
  });
  auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs * scale});
  row->addChild(std::move(slider));
  row->addChild(std::move(valueLabel));
  return row;
}

std::unique_ptr<Node> WindowRulesPage::buildDefaultsRow(
    std::string_view title, std::string_view subtitle, std::unique_ptr<Node> control, float scale
) {
  auto row = ui::row({
      .align = FlexAlign::Center,
      .justify = FlexJustify::SpaceBetween,
      .gap = Style::spaceSm * scale,
      .paddingV = 2.0F * scale,
      .fillWidth = true,
  });
  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  copy->addChild(ui::label({
      .text = std::string(subtitle),
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 4,
  }));
  row->addChild(std::move(copy));
  row->addChild(std::move(control));
  return row;
}

// ---- Window rules ----------------------------------------------------------

void WindowRulesPage::ensureRulesLoaded() {
  if (m_rulesLoaded) {
    return;
  }
  reloadRules();
}

void WindowRulesPage::reloadRules() {
  m_rulesConfig.setPath(KineticweKwe::rulesDefaultPath());
  m_rulesConfig.load();
  m_ruleList.clear();
  // Which groups are rules? Prefer the compositor's own [General] rules=list;
  // if absent (a hand-written file), treat every group except the [General]
  // header as a rule.
  std::vector<std::string> ruleIds = m_rulesConfig.readStringList("General", "rules");
  const bool missingGeneralList = ruleIds.empty();
  if (ruleIds.empty()) {
    for (const auto& group : m_rulesConfig.groupNames()) {
      if (group != "General") {
        ruleIds.push_back(group);
      }
    }
  }
  for (const auto& group : ruleIds) {
    Rule rule;
    rule.id = group;
    rule.values["Description"] = m_rulesConfig.readString(group, "Description").value_or("");
    rule.values["Enabled"] = m_rulesConfig.readBool(group, "Enabled", true) ? "true" : "false";
    for (const auto& field : ruleFields()) {
      if (const auto value = m_rulesConfig.readString(group, field.key); value) {
        rule.values[std::string(field.key)] = *value;
      }
      if (!field.policyKey.empty()) {
        if (const auto policy = m_rulesConfig.readString(group, field.policyKey); policy) {
          rule.values[std::string(field.policyKey)] = *policy;
        }
      }
    }
    m_ruleList.push_back(std::move(rule));
  }
  m_rulesLoaded = true;
  m_newRuleDrafts.clear();
  // If the file has rule groups but no [General] rules= list (an older file the
  // compositor can't read), flag dirty so Apply rewrites the proper book.
  m_rulesDirty = missingGeneralList && !m_ruleList.empty();
}

std::string WindowRulesPage::ruleDescription(const Rule& rule) const {
  const auto it = rule.values.find("Description");
  return it != rule.values.end() && !it->second.empty() ? it->second : "New window settings";
}

void WindowRulesPage::writeRuleGroup(const Rule& rule) {
  const auto& v = rule.values;
  m_rulesConfig.setString(rule.id, "Description", ruleDescription(rule));
  m_rulesConfig.setBool(rule.id, "Enabled", (v.count("Enabled") ? v.at("Enabled") == "true" : true));

  for (const auto& field : ruleFields()) {
    const std::string valueKey(field.key);
    const std::string policyKey(field.policyKey);
    const std::string value = v.count(valueKey) ? v.at(valueKey) : "";
    int policy = 0;
    if (!policyKey.empty()) {
      policy = v.count(policyKey) ? std::atoi(v.at(policyKey).c_str()) : 0;
    }
    if (fieldActive(field, value, policy)) {
      m_rulesConfig.setString(rule.id, valueKey, value);
      if (!policyKey.empty()) {
        m_rulesConfig.setInt(rule.id, policyKey, policy);
      }
    } else {
      m_rulesConfig.removeKey(rule.id, valueKey);
      if (!policyKey.empty()) {
        m_rulesConfig.removeKey(rule.id, policyKey);
      }
    }
  }
}

bool WindowRulesPage::writeRulesBook() {
  // The whole file is this page's rule book, so rebuild it from scratch (a
  // clear() drops groups belonging to deleted rules).
  m_rulesConfig.setPath(KineticweKwe::rulesDefaultPath());
  m_rulesConfig.clear();
  std::vector<std::string> ruleIds;
  ruleIds.reserve(m_ruleList.size());
  for (const auto& rule : m_ruleList) {
    writeRuleGroup(rule);
    ruleIds.push_back(rule.id);
  }
  // RuleBookSettings discovers the rule groups via [General] rules=<list>; the
  // compositor can't find the rules without it.
  m_rulesConfig.setInt("General", "count", static_cast<int>(m_ruleList.size()));
  m_rulesConfig.setStringList("General", "rules", ruleIds);
  if (!m_rulesConfig.save()) {
    kLog.warn("window rules: could not write rules.kwe");
    m_errorNotice = "Could not write rules.kwe. Your changes were kept; try Apply again.";
    return false;
  }
  m_rulesLoaded = false; // force a re-read of the just-written book
  return true;
}

const std::vector<WindowRulesPage::RuleField>& WindowRulesPage::ruleFields() {
  static const std::vector<RuleField> kFields{
      // Matching
      {"wmclass", "wmclassmatch", RuleValueKind::Text, RulePolicyKind::StringMatch, "Window matching", "Window class (application)"},
      {"wmclasscomplete", "", RuleValueKind::Bool, RulePolicyKind::None, "Window matching", "Match whole window class"},
      {"windowrole", "windowrolematch", RuleValueKind::Text, RulePolicyKind::StringMatch, "Window matching", "Window role"},
      {"title", "titlematch", RuleValueKind::Text, RulePolicyKind::StringMatch, "Window matching", "Window title"},
      // Size & Position
      {"position", "positionrule", RuleValueKind::Point, RulePolicyKind::SetRule, "Size & Position", "Position"},
      {"size", "sizerule", RuleValueKind::Size, RulePolicyKind::SetRule, "Size & Position", "Size"},
      {"ignoregeometry", "ignoregeometryrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Size & Position", "Ignore requested geometry"},
      {"maximizehoriz", "maximizehorizrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Size & Position", "Maximized horizontally"},
      {"maximizevert", "maximizevertrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Size & Position", "Maximized vertically"},
      {"minimize", "minimizerule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Size & Position", "Minimized"},
      {"fullscreen", "fullscreenrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Size & Position", "Fullscreen"},
      // Arrangement & Access (stacking). Skip taskbar/pager/switcher are
      // intentionally omitted: KineticWE doesn't run a Plasma taskbar, pager or
      // switcher, so those window rules have no effect here.
      {"above", "aboverule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Arrangement & Access", "Keep above other windows"},
      {"below", "belowrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Arrangement & Access", "Keep below other windows"},
      {"floating", "floatingrule", RuleValueKind::Bool, RulePolicyKind::SetRule, "Arrangement & Access", "Float window"},
      // Appearance & Fixes
      {"decorationpolicy", "decorationpolicyrule", RuleValueKind::Option, RulePolicyKind::SetRule, "Appearance & Fixes", "Title bar & frame"},
      {"opacityactive", "opacityactiverule", RuleValueKind::Percentage, RulePolicyKind::ForceRule, "Appearance & Fixes", "Active opacity"},
      {"opacityinactive", "opacityinactiverule", RuleValueKind::Percentage, RulePolicyKind::ForceRule, "Appearance & Fixes", "Inactive opacity"},
  };
  return kFields;
}

const std::vector<std::pair<int, std::string_view>>& WindowRulesPage::policyOptions(RulePolicyKind kind) {
  static const std::vector<std::pair<int, std::string_view>> kStringMatch{
      {0, "Global"}, {1, "Exact match"}, {2, "Substring match"}, {3, "Regular expression"},
  };
  static const std::vector<std::pair<int, std::string_view>> kSetRule{
      {0, "Not set"}, {1, "Do not affect"}, {2, "Force"}, {3, "Apply initially"},
      {4, "Remember"}, {5, "Apply now"}, {6, "Force temporarily"},
  };
  static const std::vector<std::pair<int, std::string_view>> kForceRule{
      {0, "Not set"}, {1, "Do not affect"}, {2, "Force"}, {6, "Force temporarily"},
  };
  static const std::vector<std::pair<int, std::string_view>> kNone{};
  switch (kind) {
  case RulePolicyKind::StringMatch:
    return kStringMatch;
  case RulePolicyKind::SetRule:
    return kSetRule;
  case RulePolicyKind::ForceRule:
    return kForceRule;
  case RulePolicyKind::None:
  default:
    return kNone;
  }
}

const std::vector<std::pair<int, std::string_view>>& WindowRulesPage::valueOptions(std::string_view key) {
  // DecorationPolicy: None=0, ClientPreference=1, Server=2, Shadow=3.
  static const std::vector<std::pair<int, std::string_view>> kDecorationPolicy{
      {0, "Hidden (no title bar or frame)"},
      {1, "If the application asks"},
      {2, "Always"},
      {3, "Only shadow"},
  };
  static const std::vector<std::pair<int, std::string_view>> kEmpty{};
  (void)key;
  return kDecorationPolicy;
}

bool WindowRulesPage::fieldActive(const RuleField& field, const std::string& value, int policy) {
  if (field.policyKind == RulePolicyKind::StringMatch) {
    // A matching field takes part only when it has a value AND a match type;
    // otherwise an empty class/role/title would silently match nothing.
    return policy != 0 && !value.empty();
  }
  if (!field.policyKey.empty()) {
    // A property (SetRule/ForceRule) is active once a policy is chosen; for
    // geometry also require a value so an empty point/size isn't written.
    if (field.valueKind == RuleValueKind::Point || field.valueKind == RuleValueKind::Size) {
      return policy != 0 && !value.empty();
    }
    return policy != 0;
  }
  // No-policy fields: the value itself determines presence.
  if (field.valueKind == RuleValueKind::Bool) {
    return value == "true";
  }
  return !value.empty();
}

void WindowRulesPage::addRule(const Callbacks& callbacks) {
  ensureRulesLoaded();
  Rule rule;
  rule.id = nextRuleId();
  rule.values["Description"] = "New window settings";
  rule.values["Enabled"] = "true";
  // Default the match types to Exact so typing a class/role/title activates it.
  rule.values["wmclassmatch"] = "1";
  rule.values["windowrolematch"] = "1";
  rule.values["titlematch"] = "1";
  m_ruleList.push_back(std::move(rule));
  m_newRuleDrafts.insert(m_ruleList.back().id);
  m_editingRule = m_ruleList.size() - 1;
  // Don't mark dirty yet: an unmodified draft is discarded on Done, so it
  // shouldn't arm Apply until the user actually changes something.
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::duplicateRule(std::size_t index, const Callbacks& callbacks) {
  if (index >= m_ruleList.size()) {
    return;
  }
  Rule copy = m_ruleList[index];
  copy.id = nextRuleId();
  copy.values["Description"] = "Copy of " + ruleDescription(copy);
  m_ruleList.insert(m_ruleList.begin() + static_cast<std::ptrdiff_t>(index + 1), std::move(copy));
  m_rulesDirty = true;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::removeRule(std::size_t index, const Callbacks& callbacks) {
  if (index >= m_ruleList.size()) {
    return;
  }
  m_newRuleDrafts.erase(m_ruleList[index].id);
  m_ruleList.erase(m_ruleList.begin() + static_cast<std::ptrdiff_t>(index));
  if (m_editingRule.has_value() && *m_editingRule >= m_ruleList.size()) {
    m_editingRule.reset();
  } else if (m_editingRule.has_value() && *m_editingRule > index) {
    m_editingRule = *m_editingRule - 1;
  }
  m_rulesDirty = true;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::moveRule(std::size_t from, std::size_t to, const Callbacks& callbacks) {
  if (from >= m_ruleList.size() || to >= m_ruleList.size() || from == to) {
    return;
  }
  Rule rule = std::move(m_ruleList[from]);
  m_ruleList.erase(m_ruleList.begin() + static_cast<std::ptrdiff_t>(from));
  m_ruleList.insert(m_ruleList.begin() + static_cast<std::ptrdiff_t>(to), std::move(rule));
  if (m_editingRule.has_value()) {
    if (*m_editingRule == from) {
      m_editingRule = to;
    }
  }
  m_rulesDirty = true;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::editRule(std::size_t index, const Callbacks& callbacks) {
  if (index >= m_ruleList.size()) {
    return;
  }
  m_editingRule = index;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::cancelEdit(const Callbacks& callbacks) {
  // If the user opened a freshly-added rule and changed nothing, drop it so
  // "Add" then "Done" doesn't leave an empty rule behind.
  if (m_editingRule.has_value() && *m_editingRule < m_ruleList.size()) {
    const std::string& id = m_ruleList[*m_editingRule].id;
    if (m_newRuleDrafts.contains(id)) {
      m_newRuleDrafts.erase(id);
      m_ruleList.erase(m_ruleList.begin() + static_cast<std::ptrdiff_t>(*m_editingRule));
    }
  }
  m_editingRule.reset();
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::setRuleValue(std::size_t index, const std::string& key, std::string value, const Callbacks& callbacks) {
  if (index >= m_ruleList.size()) {
    return;
  }
  auto& rule = m_ruleList[index];
  const auto it = rule.values.find(key);
  if (value.empty()) {
    // Unsetting a field drops the key (e.g. clearing a position/size).
    if (it != rule.values.end()) {
      m_newRuleDrafts.erase(rule.id);
      rule.values.erase(it);
      m_rulesDirty = true;
      if (callbacks.requestRebuild) {
        callbacks.requestRebuild();
      }
    }
    return;
  }
  const bool changed = (it == rule.values.end()) || it->second != value;
  if (!changed) {
    return; // no real change: don't promote a draft or arm Apply
  }
  m_newRuleDrafts.erase(rule.id);
  rule.values[key] = std::move(value);
  m_rulesDirty = true;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

void WindowRulesPage::setRulePolicy(std::size_t index, const std::string& policyKey, int policy, const Callbacks& callbacks) {
  if (index >= m_ruleList.size()) {
    return;
  }
  auto& rule = m_ruleList[index];
  const std::string newValue = std::to_string(policy);
  const auto it = rule.values.find(policyKey);
  const bool changed = (it == rule.values.end()) || it->second != newValue;
  if (!changed) {
    return;
  }
  m_newRuleDrafts.erase(rule.id);
  rule.values[policyKey] = newValue;
  m_rulesDirty = true;
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

// ---- Build: field row ------------------------------------------------------

std::unique_ptr<Node>
WindowRulesPage::buildRuleFieldRow(std::size_t ruleIndex, const RuleField& field, float scale, const Callbacks& callbacks) {
  if (ruleIndex >= m_ruleList.size()) {
    return ui::spacer();
  }
  Rule& rule = m_ruleList[ruleIndex];
  const std::string valueKey(field.key);
  const std::string policyKey(field.policyKey);
  const std::string currentValue = rule.values.count(valueKey) ? rule.values.at(valueKey) : "";
  const int currentPolicy = rule.values.count(policyKey) ? std::atoi(rule.values.at(policyKey).c_str()) : 0;

  auto row = ui::row({
      .align = FlexAlign::Center,
      .justify = FlexJustify::SpaceBetween,
      .gap = Style::spaceSm * scale,
      .paddingV = 2.0F * scale,
      .fillWidth = true,
  });
  auto copy = ui::column({
      .align = FlexAlign::Start,
      .gap = Style::spaceXs * scale,
      .flexGrow = 1.0F,
  });
  copy->addChild(makeLabel(field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  row->addChild(std::move(copy));

  auto controls = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs * scale});
  switch (field.valueKind) {
    case RuleValueKind::Text: {
      auto input = std::make_unique<Input>();
      input->setValue(currentValue);
      input->setMinLayoutWidth(220.0F * scale);
      input->setEnabled(true);
      // Commit on Enter/focus loss so we don't rebuild mid-keystroke, which
      // would otherwise drop the user's cursor/focus while typing.
      input->setSubmitOnFocusLoss(true);
      input->setOnSubmit([this, ruleIndex, key = std::string(field.key), callbacks](const std::string& value) {
        setRuleValue(ruleIndex, key, value, callbacks);
      });
      controls->addChild(std::move(input));
      break;
    }
    case RuleValueKind::Bool: {
      controls->addChild(ui::toggle({
          .checked = currentValue == "true",
          .scale = scale,
          .onChange =
              [this, ruleIndex, key = std::string(field.key), polKey = std::string(field.policyKey), callbacks](bool value) {
                setRuleValue(ruleIndex, key, value ? "true" : "false", callbacks);
                // Toggling a property on forces it (so a simple toggle actually
                // takes effect, e.g. "Float window"); toggling off clears the
                // policy so the property falls back to the default handling.
                if (!polKey.empty()) {
                  setRulePolicy(ruleIndex, polKey, value ? 2 : 0, callbacks);
                }
              },
      }));
      break;
    }
    case RuleValueKind::Option: {
      const auto& opts = valueOptions(field.key);
      controls->addChild(ui::select({
          .options = optionLabels(opts),
          .selectedIndex = optionIndex(opts, std::atoi(currentValue.empty() ? "0" : currentValue.c_str())),
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeight * scale,
          .glyphSize = Style::fontSizeBody * scale,
          .width = 200.0F * scale,
          .height = Style::controlHeight * scale,
          .onSelectionChanged =
              [this, ruleIndex, key = std::string(field.key), opts, callbacks](std::size_t index, std::string_view) {
                if (index < opts.size()) {
                  setRuleValue(ruleIndex, key, std::to_string(opts[index].first), callbacks);
                }
              },
      }));
      break;
    }
    case RuleValueKind::Percentage: {
      const double initial = std::clamp(
          static_cast<double>(std::atoi(currentValue.empty() ? "100" : currentValue.c_str())), 0.0, 100.0);
      controls->addChild(makePercentSlider(initial, [this, ruleIndex, key = std::string(field.key), callbacks](int value) {
        setRuleValue(ruleIndex, key, std::to_string(value), callbacks);
      }, scale));
      break;
    }
    case RuleValueKind::Point:
    case RuleValueKind::Size: {
      // Value is stored as "a,b" (x,y for Point; width,height for Size) and is
      // written only when both halves are present.
      const bool isSize = field.valueKind == RuleValueKind::Size;
      int a = 0;
      int b = 0;
      if (!currentValue.empty()) {
        const auto comma = currentValue.find(',');
        if (comma != std::string::npos) {
          a = std::atoi(currentValue.substr(0, comma).c_str());
          b = std::atoi(currentValue.substr(comma + 1).c_str());
        }
      }
      const std::string keyStr(field.key);
      auto fieldA = std::make_unique<Input>();
      fieldA->setValue(currentValue.empty() ? "" : std::to_string(a));
      fieldA->setPlaceholder("0");
      fieldA->setMinLayoutWidth(90.0F * scale);
      fieldA->setSubmitOnFocusLoss(true);
      auto fieldB = std::make_unique<Input>();
      fieldB->setValue(currentValue.empty() ? "" : std::to_string(b));
      fieldB->setPlaceholder("0");
      fieldB->setMinLayoutWidth(90.0F * scale);
      fieldB->setSubmitOnFocusLoss(true);
      Input* aPtr = fieldA.get();
      Input* bPtr = fieldB.get();
      fieldA->setOnSubmit([this, ruleIndex, key = keyStr, bPtr, callbacks](const std::string& va) {
        const std::string vb = bPtr->value();
        setRuleValue(ruleIndex, key, (!va.empty() && !vb.empty()) ? va + "," + vb : "", callbacks);
      });
      fieldB->setOnSubmit([this, ruleIndex, key = keyStr, aPtr, callbacks](const std::string& vb) {
        const std::string va = aPtr->value();
        setRuleValue(ruleIndex, key, (!va.empty() && !vb.empty()) ? va + "," + vb : "", callbacks);
      });
      auto pairRow = ui::row({.align = FlexAlign::Center, .gap = Style::spaceXs * scale});
      pairRow->addChild(makeLabel(isSize ? "W" : "X", Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant), FontWeight::Bold));
      pairRow->addChild(std::move(fieldA));
      pairRow->addChild(makeLabel(isSize ? "H" : "Y", Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant), FontWeight::Bold));
      pairRow->addChild(std::move(fieldB));
      controls->addChild(std::move(pairRow));
      break;
    }
    }

  if (!policyKey.empty()) {
    const auto& opts = policyOptions(field.policyKind);
    bool policyEnabled = field.policyKind != RulePolicyKind::None;
    if (policyEnabled) {
      controls->addChild(ui::select({
          .options = optionLabels(opts),
          .selectedIndex = optionIndex(opts, currentPolicy),
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeight * scale,
          .glyphSize = Style::fontSizeBody * scale,
          .width = 150.0F * scale,
          .height = Style::controlHeight * scale,
          .onSelectionChanged =
              [this, ruleIndex, polKey = std::string(policyKey), opts, callbacks](std::size_t index, std::string_view) {
                if (index < opts.size()) {
                  setRulePolicy(ruleIndex, polKey, opts[index].first, callbacks);
                }
              },
      }));
    }
  }

  row->addChild(std::move(controls));
  return row;
}

std::unique_ptr<Node> WindowRulesPage::buildMatchingHeader(float scale, std::string_view text) {
  return makeLabel(text, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold);
}

// ---- Build: list -----------------------------------------------------------

std::unique_ptr<Node> WindowRulesPage::buildRuleList(float scale, const Callbacks& callbacks) {
  auto column = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});

  auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  header->addChild(makeLabel("Window rules", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold));
  header->addChild(ui::spacer());
  header->addChild(ui::button({
      .text = "Add",
      .glyph = "plus",
      .fontSize = Style::fontSizeCaption * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .variant = ButtonVariant::Primary,
      .minHeight = Style::controlHeight * scale,
      .paddingV = Style::spaceXs * scale,
      .paddingH = Style::spaceMd * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick = [this, callbacks]() { addRule(callbacks); },
  }));
  column->addChild(std::move(header));

  if (m_ruleList.empty()) {
    column->addChild(makeLabel(
        "No window rules yet. Add a rule to match a window and set its behavior; the defaults above apply when no rule matches.",
        Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
    return column;
  }

  for (std::size_t i = 0; i < m_ruleList.size(); ++i) {
    const Rule& rule = m_ruleList[i];
    const bool enabled = rule.values.count("Enabled") ? rule.values.at("Enabled") == "true" : true;

    auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .paddingV = 2.0F * scale, .fillWidth = true});
    row->addChild(ui::toggle({
        .checked = enabled,
        .scale = scale,
        .onChange = [this, i, callbacks](bool value) { setRuleValue(i, "Enabled", value ? "true" : "false", callbacks); },
    }));
    auto desc = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
    desc->addChild(makeLabel(ruleDescription(rule), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
    row->addChild(std::move(desc));

    row->addChild(ui::button({
        .text = "Edit", .fontSize = Style::fontSizeCaption * scale, .variant = ButtonVariant::Ghost,
        .minHeight = Style::controlHeight * scale, .paddingV = Style::spaceXs * scale, .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = [this, i, callbacks]() { editRule(i, callbacks); },
    }));
    row->addChild(ui::button({
        .text = "Duplicate", .fontSize = Style::fontSizeCaption * scale, .variant = ButtonVariant::Ghost,
        .minHeight = Style::controlHeight * scale, .paddingV = Style::spaceXs * scale, .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = [this, i, callbacks]() { duplicateRule(i, callbacks); },
    }));
    row->addChild(ui::button({
        .text = "Delete", .fontSize = Style::fontSizeCaption * scale, .variant = ButtonVariant::Destructive,
        .minHeight = Style::controlHeight * scale, .paddingV = Style::spaceXs * scale, .paddingH = Style::spaceMd * scale,
        .radius = Style::scaledRadiusMd(scale),
        .onClick = [this, i, callbacks]() { removeRule(i, callbacks); },
    }));
    row->addChild(ui::button({
        .glyph = "arrow-up", .fontSize = Style::fontSizeCaption * scale, .glyphSize = Style::fontSizeBody * scale,
        .enabled = i > 0,
        .variant = ButtonVariant::Ghost, .minWidth = Style::controlHeight * scale, .minHeight = Style::controlHeight * scale,
        .padding = Style::spaceXs * scale, .radius = Style::scaledRadiusMd(scale),
        .onClick = [this, i, callbacks]() { moveRule(i, i - 1, callbacks); },
    }));
    row->addChild(ui::button({
        .glyph = "arrow-down", .fontSize = Style::fontSizeCaption * scale, .glyphSize = Style::fontSizeBody * scale,
        .enabled = i + 1 < m_ruleList.size(),
        .variant = ButtonVariant::Ghost, .minWidth = Style::controlHeight * scale, .minHeight = Style::controlHeight * scale,
        .padding = Style::spaceXs * scale, .radius = Style::scaledRadiusMd(scale),
        .onClick = [this, i, callbacks]() { moveRule(i, i + 1, callbacks); },
    }));

    column->addChild(std::move(row));
    column->addChild(ui::separator({.thickness = 1.0F, .spacing = Style::spaceXs * scale, .orientation = SeparatorOrientation::HorizontalRule}));
  }

  return column;
}

// ---- Build: editor ---------------------------------------------------------

std::unique_ptr<Node> WindowRulesPage::buildRuleEditor(float scale, const Callbacks& callbacks) {
  auto column = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});
  if (!m_editingRule.has_value() || *m_editingRule >= m_ruleList.size()) {
    return buildRuleList(scale, callbacks);
  }
  const std::size_t index = *m_editingRule;
  Rule& rule = m_ruleList[index];

  auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  header->addChild(makeLabel("Edit window rule", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold));
  header->addChild(ui::spacer());
  header->addChild(ui::button({
      .text = "Done", .fontSize = Style::fontSizeCaption * scale, .variant = ButtonVariant::Ghost,
      .minHeight = Style::controlHeight * scale, .paddingV = Style::spaceXs * scale, .paddingH = Style::spaceMd * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick = [this, callbacks]() { cancelEdit(callbacks); },
  }));
  column->addChild(std::move(header));

  // Description + enabled
  auto descInput = std::make_unique<Input>();
  descInput->setValue(ruleDescription(rule));
  descInput->setPlaceholder("Window settings for ...");
  descInput->setMinLayoutWidth(360.0F * scale);
  descInput->setSubmitOnFocusLoss(true);
  descInput->setOnSubmit([this, index, callbacks](const std::string& value) {
    setRuleValue(index, "Description", value, callbacks);
  });
  column->addChild(std::move(descInput));

  auto enabledRow = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  enabledRow->addChild(makeLabel("Enabled", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  enabledRow->addChild(ui::spacer());
  enabledRow->addChild(ui::toggle({
      .checked = rule.values.count("Enabled") ? rule.values.at("Enabled") == "true" : true,
      .scale = scale,
      .onChange = [this, index, callbacks](bool value) { setRuleValue(index, "Enabled", value ? "true" : "false", callbacks); },
  }));
  column->addChild(std::move(enabledRow));
  column->addChild(ui::separator({.thickness = 1.0F, .spacing = Style::spaceXs * scale, .orientation = SeparatorOrientation::HorizontalRule}));

  // Field groups by section.
  std::string_view currentSection;
  for (const auto& field : ruleFields()) {
    if (field.section != currentSection) {
      currentSection = field.section;
      column->addChild(buildMatchingHeader(scale, currentSection));
      column->addChild(ui::separator({.thickness = 1.0F, .spacing = Style::spaceXs * scale, .orientation = SeparatorOrientation::HorizontalRule}));
    }
    column->addChild(buildRuleFieldRow(index, field, scale, callbacks));
  }

  return column;
}

std::unique_ptr<Node> WindowRulesPage::buildRulesSection(float scale, const Callbacks& callbacks) {
  ensureRulesLoaded();
  auto column = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});
  column->addChild(ui::separator({.thickness = 1.0F, .spacing = Style::spaceXs * scale, .orientation = SeparatorOrientation::HorizontalRule}));
  column->addChild(m_editingRule.has_value() ? buildRuleEditor(scale, callbacks) : buildRuleList(scale, callbacks));
  return column;
}

// ---- Apply / reset ---------------------------------------------------------

std::vector<std::string> WindowRulesPage::resettableKeyPaths() {
  ensureLoaded();
  if (overridden() || m_pending.has_value() || m_pendingActiveOpacity.has_value() || m_pendingInactiveOpacity.has_value() || m_rulesDirty) {
    return {std::string(kKeyPath), std::string(kActiveOpacityPath), std::string(kInactiveOpacityPath)};
  }
  return {};
}

void WindowRulesPage::resetAll() {
  ensureLoaded();
  // Defaults back to "title bars shown" (only stage when it differs).
  if (hideTitleBarsOnDisk()) {
    m_pending = false;
  } else {
    m_pending.reset();
  }
  // Opacity defaults back to 100% (only stage when they differ).
  if (activeOpacityOnDisk() != 100) {
    m_pendingActiveOpacity = 100;
  } else {
    m_pendingActiveOpacity.reset();
  }
  if (inactiveOpacityOnDisk() != 100) {
    m_pendingInactiveOpacity = 100;
  } else {
    m_pendingInactiveOpacity.reset();
  }
  // Revert rule edits (and leave the editor): reload the saved book.
  m_editingRule.reset();
  m_rulesDirty = false;
  m_rulesLoaded = false;
  ensureRulesLoaded();
}

bool WindowRulesPage::applyAll() {
  // Window defaults (kineticwe.kwe): title bars + global opacity.
  const bool defaultsDirty = m_pending.has_value() || m_pendingActiveOpacity.has_value() || m_pendingInactiveOpacity.has_value();
  if (defaultsDirty) {
    m_config.load();
    if (m_pending.has_value()) {
      m_config.setInt(kGroup, kKey, *m_pending ? 0 : 1);
    }
    if (m_pendingActiveOpacity.has_value()) {
      m_config.setInt(kGroup, kActiveOpacityKey, *m_pendingActiveOpacity);
    }
    if (m_pendingInactiveOpacity.has_value()) {
      m_config.setInt(kGroup, kInactiveOpacityKey, *m_pendingInactiveOpacity);
    }
    if (!m_config.save()) {
      kLog.warn("window defaults: could not write kineticwe.kwe");
      m_errorNotice = "Could not write kineticwe.kwe. Your change was kept; try Apply again.";
      return false;
    }
    m_pending.reset();
    m_pendingActiveOpacity.reset();
    m_pendingInactiveOpacity.reset();
  }

  // Window rules (rules.kwe). Always write when there are rules (or a pending
  // deletion), so the [General] rules= list is kept correct even for files that
  // predate this fix.
  if (m_rulesDirty || !m_ruleList.empty()) {
    if (!writeRulesBook()) {
      return false; // writeRulesBook set the error notice
    }
    m_rulesDirty = false;
  }

  m_loaded = false; // next build() re-reads both files.
  m_errorNotice.clear();
  // The compositor re-reads kineticwe.kwe and rules.kwe and re-applies to
  // windows (covers the global defaults and the whole rule book).
  m_dbus.requestReconfigure();
  return true;
}

// ---- Build: page -----------------------------------------------------------

std::unique_ptr<Node>
WindowRulesPage::build(float scale, const Filter& /*filter*/, const Callbacks& callbacks) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  // Section header: glyph + bold primary title.
  auto header = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  header->addChild(ui::glyph({
      .glyph = "window",
      .glyphSize = Style::fontSizeHeader * scale,
      .color = colorSpecFromRole(ColorRole::Primary),
  }));
  header->addChild(makeLabel(
      "Window Rules", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
  ));
  header->addChild(ui::spacer());
  root->addChild(std::move(header));

  if (!m_errorNotice.empty()) {
    root->addChild(ui::row(
        {.align = FlexAlign::Center,
         .gap = Style::spaceSm * scale,
         .paddingV = Style::spaceXs * scale,
         .paddingH = Style::spaceMd * scale,
         .fill = colorSpecFromRole(ColorRole::Error, 0.15F),
         .radius = Style::scaledRadiusMd(scale)},
        makeLabel(m_errorNotice, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::Error))
    ));
  }

  // Window defaults card ------------------------------------------------------
  auto card = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceMd * scale,
      .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.5F),
      .radius = Style::scaledRadiusMd(scale),
      .fillWidth = true,
  });
  card->addChild(makeLabel(
      "Window defaults", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
  ));

  const bool hideTitleBars = effectiveHideTitleBars();
  auto row = ui::row({
      .align = FlexAlign::Center,
      .justify = FlexJustify::SpaceBetween,
      .gap = Style::spaceSm * scale,
      .paddingV = 2.0F * scale,
      .fillWidth = true,
  });
  auto copy = ui::column({
      .align = FlexAlign::Start,
      .gap = Style::spaceXs * scale,
      .flexGrow = 1.0F,
  });
  copy->addChild(makeLabel(
      "Toggle title bars", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  copy->addChild(ui::label({
      .text =
          "Remove the title bar and frame on every window, unless a per-window "
          "rule overrides it for a specific application.",
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 4,
  }));
  row->addChild(std::move(copy));
  row->addChild(ui::toggle({
      .checked = hideTitleBars,
      .scale = scale,
      .onChange =
          [this, callbacks](bool hide) {
            stageHideTitleBars(hide, callbacks);
          },
  }));
  card->addChild(std::move(row));

  card->addChild(buildDefaultsRow(
      "Active opacity", "Opacity of the currently focused window.",
      makePercentSlider(effectiveActiveOpacity(), [this, callbacks](int value) { stageActiveOpacity(value, callbacks); }, scale),
      scale
  ));
  card->addChild(buildDefaultsRow(
      "Inactive opacity", "Opacity of background windows.",
      makePercentSlider(effectiveInactiveOpacity(), [this, callbacks](int value) { stageInactiveOpacity(value, callbacks); }, scale),
      scale
  ));

  root->addChild(std::move(card));

  // Per-window rules ----------------------------------------------------------
  root->addChild(buildRulesSection(scale, callbacks));

  return root;
}
