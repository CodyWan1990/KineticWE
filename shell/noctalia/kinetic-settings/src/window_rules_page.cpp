#include "window_rules_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "ui/builders.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"

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

} // namespace

WindowRulesPage::WindowRulesPage(SessionBus& bus) : m_dbus(bus) {}

void WindowRulesPage::ensureLoaded() {
  if (m_loaded) {
    return;
  }
  // A missing/unreadable file is fine: readInt() falls back to the default.
  m_config.load();
  m_loaded = true;
}

bool WindowRulesPage::hideTitleBarsOnDisk() const {
  // DefaultDecorationPolicy: 0 = None (hidden), 1 = ClientPreference (shown).
  // Absent key reads as ClientPreference.
  return m_config.readInt(kGroup, kKey, 1) == 0;
}

bool WindowRulesPage::effectiveHideTitleBars() const {
  return m_pending.value_or(hideTitleBarsOnDisk());
}

bool WindowRulesPage::overridden() const {
  // The app default is "title bars shown" (ClientPreference), so any effective
  // "hidden" state is a deviation worth resetting.
  return effectiveHideTitleBars();
}

void WindowRulesPage::stageHideTitleBars(bool hide, const Callbacks& callbacks) {
  ensureLoaded();
  if (hide == hideTitleBarsOnDisk()) {
    // Editing back to what's on disk un-stages.
    m_pending.reset();
  } else {
    m_pending = hide;
  }
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> WindowRulesPage::resettableKeyPaths() {
  ensureLoaded();
  if (overridden() || m_pending.has_value()) {
    return {std::string(kKeyPath)};
  }
  return {};
}

void WindowRulesPage::resetAll() {
  ensureLoaded();
  // The app default is "title bars shown". Only stage when it differs so the
  // Apply button isn't spuriously armed when nothing would change.
  if (hideTitleBarsOnDisk()) {
    m_pending = false;
  } else {
    m_pending.reset();
  }
}

bool WindowRulesPage::applyAll() {
  if (!m_pending.has_value()) {
    m_errorNotice.clear();
    return true;
  }
  // Re-read the file so we don't clobber a value another writer changed while
  // the window was open, then write only our staged key.
  m_config.load();
  m_config.setInt(kGroup, kKey, *m_pending ? 0 : 1);
  if (!m_config.save()) {
    kLog.warn("window defaults: could not write kineticwe.kwe");
    m_errorNotice =
        "Could not write kineticwe.kwe. Your change was kept; try Apply again.";
    return false;
  }
  // The compositor does not live-apply window defaults on its own file watch;
  // nudge it to re-read kineticwe.kwe and re-seed every window's decoration.
  m_dbus.requestReconfigure();
  m_pending.reset();
  m_loaded = false; // next build() re-reads the just-written value.
  m_errorNotice.clear();
  return true;
}

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
      "Hide title bars", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
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
  root->addChild(std::move(card));

  // Per-window rules (coming next) --------------------------------------------
  auto rulesHeader = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
  });
  rulesHeader->addChild(makeLabel(
      "Window rules", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
  ));
  root->addChild(std::move(rulesHeader));
  root->addChild(ui::separator({
      .thickness = 1.0F,
      .spacing = Style::spaceXs * scale,
      .orientation = SeparatorOrientation::HorizontalRule,
  }));
  root->addChild(makeLabel(
      "Per-window rules (add, edit, match, and reorder) land here next. The "
      "default above is what applies when no rule matches a window.",
      Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));

  return root;
}
