#include "appearance_page.h"

#include "core/log.h"
#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Window decoration tiles (a wider tile than the Icons page so the
  // decoration preview reads like a window).
  constexpr float kDecorationTileWidth = 200.0F;
  constexpr float kDecorationTileHeight = 150.0F;
  constexpr float kDecorationPreviewWidth = 168.0F;
  constexpr float kDecorationPreviewHeight = 96.0F;

  // Values the titlebar-metric sliders show when the config key is absent. The
  // compositor derives the actual default from the title font/grid unit, so
  // these approximate it; dragging a slider writes an explicit value.
  constexpr int kDefaultTitleBarHeight = 30; // logical px
  constexpr int kDefaultTitleFontSize = 9;   // points
  constexpr int kDefaultButtonSize = 22;     // logical px

  // Default title-bar button-code runs (the KDE letters the compositor reads
  // from [org.kde.kdecoration2] ButtonsOnLeft/ButtonsOnRight). An explicit value
  // equal to these removes the .kwe key again.
  constexpr std::string_view kDefaultButtonsOnLeft = "MSE";
  constexpr std::string_view kDefaultButtonsOnRight = "HIAX";

  // One configurable title-bar button: the KDecoration letter code and its
  // user-facing label.
  struct ButtonOption {
    char letter;
    std::string_view label;
  };

  // The configurable title-bar buttons, matching the compositor's letter codes
  // (src/decorations/settings.cpp s_buttonNames) and the labels used by the
  // stock KWin decoration KCM.
  const std::vector<ButtonOption>& buttonOptions() {
    static const std::vector<ButtonOption> v{
        {'M', "Window menu"},
        {'N', "Application menu"},
        {'S', "On all desktops"},
        {'I', "Minimize"},
        {'A', "Maximize"},
        {'X', "Close"},
        {'H', "Context help"},
        {'B', "Keep below other windows"},
        {'F', "Keep above other windows"},
        {'E', "Hide from screenshots and screen recordings"},
    };
    return v;
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

} // namespace

AppearancePage::AppearancePage(SessionBus& bus) : m_dbus(bus) {}

void AppearancePage::ensureLoaded() {
  if (!m_loaded) {
    m_decorationsConfig.load();
    m_loaded = true;
    kLog.info("appearance ensureLoaded: path={}", m_decorationsConfig.path().string());
  }
}

// ---------------------------------------------------------------------------
// Persistence: staged edit + Apply.
// ---------------------------------------------------------------------------

bool AppearancePage::applyAll() {
  if (!m_pendingDecoration.has_value() && !m_pendingHideTitleBars.has_value()
      && !m_pendingTitleBarHeight.has_value() && !m_pendingTitleFontSize.has_value()
      && !m_pendingButtonSize.has_value() && !m_pendingButtonsOnLeft.has_value()
      && !m_pendingButtonsOnRight.has_value() && !m_pendingShowTitle.has_value()) {
    return true;
  }
  ensureLoaded();
  // applyTitleBars() writes kineticwe.kwe ([org.kde.kdecoration2] + [Windows])
  // and asks the compositor to reconfigure; it sets m_errorNotice on failure.
  if (!applyTitleBars()) {
    return false;
  }
  m_pendingDecoration.reset();
  m_pendingHideTitleBars.reset();
  m_pendingTitleBarHeight.reset();
  m_pendingTitleFontSize.reset();
  m_pendingButtonSize.reset();
  m_pendingButtonsOnLeft.reset();
  m_pendingButtonsOnRight.reset();
  m_pendingShowTitle.reset();
  m_errorNotice.clear();
  return true;
}

std::vector<std::string> AppearancePage::resettableKeyPaths() const {
  return {
      decorationKeyPath(),
      titleBarsKeyPath(),
      titleBarHeightKeyPath(),
      titleFontSizeKeyPath(),
      buttonSizeKeyPath(),
      buttonsOnLeftKeyPath(),
      buttonsOnRightKeyPath(),
      showTitleKeyPath(),
  };
}

void AppearancePage::resetKeyPath(std::string_view path) {
  if (path == decorationKeyPath()) {
    stageDecoration(defaultDecoration());
  } else if (path == titleBarsKeyPath()) {
    stageHideTitleBars(false);
  } else if (path == titleBarHeightKeyPath()) {
    stageTitleBarHeight(kDefaultTitleBarHeight);
  } else if (path == titleFontSizeKeyPath()) {
    stageTitleFontSize(kDefaultTitleFontSize);
  } else if (path == buttonSizeKeyPath()) {
    stageButtonSize(kDefaultButtonSize);
  } else if (path == buttonsOnLeftKeyPath()) {
    stageButtonsOnLeft(std::string(kDefaultButtonsOnLeft));
  } else if (path == buttonsOnRightKeyPath()) {
    stageButtonsOnRight(std::string(kDefaultButtonsOnRight));
  } else if (path == showTitleKeyPath()) {
    stageShowTitle(true);
  }
}

void AppearancePage::resetAll() {
  stageDecoration(defaultDecoration());
  stageHideTitleBars(false);
  stageTitleBarHeight(kDefaultTitleBarHeight);
  stageTitleFontSize(kDefaultTitleFontSize);
  stageButtonSize(kDefaultButtonSize);
  stageButtonsOnLeft(std::string(kDefaultButtonsOnLeft));
  stageButtonsOnRight(std::string(kDefaultButtonsOnRight));
  stageShowTitle(true);
}

// ---------------------------------------------------------------------------
// Title bars: the window decoration selection (inline section on this page).
// Stored in the compositor's kineticwe.kwe [org.kde.kdecoration2]; the
// first-party Kinetic decoration is the compiled-in default.
// ---------------------------------------------------------------------------

AppearancePage::DecorationSelection AppearancePage::defaultDecoration() {
  return DecorationSelection{.library = "kineticwe", .theme = "dark"};
}

AppearancePage::DecorationSelection AppearancePage::fileBackedDecoration() const {
  return DecorationSelection{
      .library = m_decorationsConfig.readString("org.kde.kdecoration2", "library").value_or("kineticwe"),
      .theme = m_decorationsConfig.readString("org.kde.kdecoration2", "theme").value_or("dark"),
  };
}

AppearancePage::DecorationSelection AppearancePage::effectiveDecoration() const {
  return m_pendingDecoration.value_or(fileBackedDecoration());
}

bool AppearancePage::overriddenDecoration() const {
  return !(effectiveDecoration() == defaultDecoration());
}

void AppearancePage::stageDecoration(const DecorationSelection& selection) {
  ensureLoaded();
  if (selection == fileBackedDecoration()) {
    m_pendingDecoration.reset();
  } else {
    m_pendingDecoration = selection;
  }
}

void AppearancePage::stageDecorationAndRebuild(const DecorationSelection& selection, const Callbacks& callbacks) {
  stageDecoration(selection);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

bool AppearancePage::titleBarsHiddenOnDisk() const {
  // DefaultDecorationPolicy: 0 = None (hidden), 1 = ClientPreference (shown).
  // An absent key reads as shown, which is KWin's default.
  return m_decorationsConfig.readInt("Windows", "DefaultDecorationPolicy", 1) == 0;
}

bool AppearancePage::effectiveHideTitleBars() const {
  return m_pendingHideTitleBars.value_or(titleBarsHiddenOnDisk());
}

void AppearancePage::stageHideTitleBars(bool hide) {
  ensureLoaded();
  if (hide == titleBarsHiddenOnDisk()) {
    m_pendingHideTitleBars.reset();
  } else {
    m_pendingHideTitleBars = hide;
  }
}

int AppearancePage::titleBarHeightOnDisk() const {
  return m_decorationsConfig.readInt("org.kde.kdecoration2", "KineticTitleBarHeight", kDefaultTitleBarHeight);
}

int AppearancePage::titleFontSizeOnDisk() const {
  return m_decorationsConfig.readInt("org.kde.kdecoration2", "KineticTitleFontSize", kDefaultTitleFontSize);
}

int AppearancePage::buttonSizeOnDisk() const {
  return m_decorationsConfig.readInt("org.kde.kdecoration2", "KineticButtonSize", kDefaultButtonSize);
}

int AppearancePage::effectiveTitleBarHeight() const {
  return m_pendingTitleBarHeight.value_or(titleBarHeightOnDisk());
}

int AppearancePage::effectiveTitleFontSize() const {
  return m_pendingTitleFontSize.value_or(titleFontSizeOnDisk());
}

int AppearancePage::effectiveButtonSize() const {
  return m_pendingButtonSize.value_or(buttonSizeOnDisk());
}

bool AppearancePage::overriddenTitleBarHeight() const {
  return effectiveTitleBarHeight() != kDefaultTitleBarHeight;
}

bool AppearancePage::overriddenTitleFontSize() const {
  return effectiveTitleFontSize() != kDefaultTitleFontSize;
}

bool AppearancePage::overriddenButtonSize() const {
  return effectiveButtonSize() != kDefaultButtonSize;
}

void AppearancePage::stageTitleBarHeight(int value) {
  ensureLoaded();
  if (value == titleBarHeightOnDisk()) {
    m_pendingTitleBarHeight.reset();
  } else {
    m_pendingTitleBarHeight = value;
  }
}

void AppearancePage::stageTitleFontSize(int value) {
  ensureLoaded();
  if (value == titleFontSizeOnDisk()) {
    m_pendingTitleFontSize.reset();
  } else {
    m_pendingTitleFontSize = value;
  }
}

void AppearancePage::stageButtonSize(int value) {
  ensureLoaded();
  if (value == buttonSizeOnDisk()) {
    m_pendingButtonSize.reset();
  } else {
    m_pendingButtonSize = value;
  }
}

std::string AppearancePage::buttonsOnLeftOnDisk() const {
  return m_decorationsConfig.readString("org.kde.kdecoration2", "ButtonsOnLeft")
      .value_or(std::string(kDefaultButtonsOnLeft));
}

std::string AppearancePage::buttonsOnRightOnDisk() const {
  return m_decorationsConfig.readString("org.kde.kdecoration2", "ButtonsOnRight")
      .value_or(std::string(kDefaultButtonsOnRight));
}

std::string AppearancePage::effectiveButtonsOnLeft() const {
  return m_pendingButtonsOnLeft.value_or(buttonsOnLeftOnDisk());
}

std::string AppearancePage::effectiveButtonsOnRight() const {
  return m_pendingButtonsOnRight.value_or(buttonsOnRightOnDisk());
}

bool AppearancePage::overriddenButtonsOnLeft() const {
  return std::string_view(effectiveButtonsOnLeft()) != kDefaultButtonsOnLeft;
}

bool AppearancePage::overriddenButtonsOnRight() const {
  return std::string_view(effectiveButtonsOnRight()) != kDefaultButtonsOnRight;
}

std::string AppearancePage::toggleButton(std::string_view current, char letter, bool on) {
  std::string out;
  bool present = false;
  for (const char c : current) {
    if (c == letter) {
      present = true;
      continue;
    }
    out.push_back(c);
  }
  if (on && !present) {
    out.push_back(letter);
  }
  return out;
}

void AppearancePage::stageButtonsOnLeft(std::string value) {
  ensureLoaded();
  if (value == buttonsOnLeftOnDisk()) {
    m_pendingButtonsOnLeft.reset();
  } else {
    m_pendingButtonsOnLeft = std::move(value);
  }
}

void AppearancePage::stageButtonsOnRight(std::string value) {
  ensureLoaded();
  if (value == buttonsOnRightOnDisk()) {
    m_pendingButtonsOnRight.reset();
  } else {
    m_pendingButtonsOnRight = std::move(value);
  }
}

bool AppearancePage::showTitleOnDisk() const {
  return m_decorationsConfig.readBool("org.kde.kdecoration2", "KineticShowTitle", true);
}

bool AppearancePage::effectiveShowTitle() const {
  return m_pendingShowTitle.value_or(showTitleOnDisk());
}

bool AppearancePage::overriddenShowTitle() const {
  return !effectiveShowTitle();
}

void AppearancePage::stageShowTitle(bool show) {
  ensureLoaded();
  if (show == showTitleOnDisk()) {
    m_pendingShowTitle.reset();
  } else {
    m_pendingShowTitle = show;
  }
}

bool AppearancePage::applyTitleBars() {
  // Reload first so edits applied from other pages in the meantime are kept.
  m_decorationsConfig.load();
  const DecorationSelection selection = effectiveDecoration();
  if (selection == defaultDecoration()) {
    m_decorationsConfig.removeKey("org.kde.kdecoration2", "library");
    m_decorationsConfig.removeKey("org.kde.kdecoration2", "theme");
  } else {
    m_decorationsConfig.setString("org.kde.kdecoration2", "library", selection.library);
    m_decorationsConfig.setString("org.kde.kdecoration2", "theme", selection.theme);
  }
  if (m_pendingHideTitleBars.has_value()) {
    // None (0) hides every title bar/frame; ClientPreference (1) shows the
    // client's requested decoration.
    m_decorationsConfig.setInt("Windows", "DefaultDecorationPolicy", *m_pendingHideTitleBars ? 0 : 1);
  }
  // Titlebar metrics: an explicit default removes the key so the compositor
  // falls back to its font/grid-unit-derived value again.
  if (m_pendingTitleBarHeight.has_value()) {
    if (*m_pendingTitleBarHeight == kDefaultTitleBarHeight) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "KineticTitleBarHeight");
    } else {
      m_decorationsConfig.setInt("org.kde.kdecoration2", "KineticTitleBarHeight", *m_pendingTitleBarHeight);
    }
  }
  if (m_pendingTitleFontSize.has_value()) {
    if (*m_pendingTitleFontSize == kDefaultTitleFontSize) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "KineticTitleFontSize");
    } else {
      m_decorationsConfig.setInt("org.kde.kdecoration2", "KineticTitleFontSize", *m_pendingTitleFontSize);
    }
  }
  if (m_pendingButtonSize.has_value()) {
    if (*m_pendingButtonSize == kDefaultButtonSize) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "KineticButtonSize");
    } else {
      m_decorationsConfig.setInt("org.kde.kdecoration2", "KineticButtonSize", *m_pendingButtonSize);
    }
  }
  // Title bar buttons: an explicit default removes the key so the compositor
  // falls back to its own default code run again.
  if (m_pendingButtonsOnLeft.has_value()) {
    if (std::string_view(*m_pendingButtonsOnLeft) == kDefaultButtonsOnLeft) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "ButtonsOnLeft");
    } else {
      m_decorationsConfig.setString("org.kde.kdecoration2", "ButtonsOnLeft", *m_pendingButtonsOnLeft);
    }
  }
  if (m_pendingButtonsOnRight.has_value()) {
    if (std::string_view(*m_pendingButtonsOnRight) == kDefaultButtonsOnRight) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "ButtonsOnRight");
    } else {
      m_decorationsConfig.setString("org.kde.kdecoration2", "ButtonsOnRight", *m_pendingButtonsOnRight);
    }
  }
  // Window caption text: shown by default, so an explicit default removes the key.
  if (m_pendingShowTitle.has_value()) {
    if (*m_pendingShowTitle) {
      m_decorationsConfig.removeKey("org.kde.kdecoration2", "KineticShowTitle");
    } else {
      m_decorationsConfig.setBool("org.kde.kdecoration2", "KineticShowTitle", false);
    }
  }
  if (!m_decorationsConfig.save()) {
    m_errorNotice = "Failed to write kineticwe.kwe. Your changes were kept; try Apply again.";
    kLog.warn("kinetic-settings: failed to write {}", m_decorationsConfig.path().string());
    return false;
  }
  m_dbus.requestReconfigure();
  return true;
}

// ---------------------------------------------------------------------------
// Controls + scene.
// ---------------------------------------------------------------------------

namespace {

  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const AppearancePage::Callbacks& callbacks
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

  // How the drawn window mock renders its titlebar: which side(s) carry
  // buttons, whether the title is centered, the titlebar height and whether
  // the buttons are dots or flat glyphs.
  struct TitlebarStyle {
    std::string leftButtons;  // KDE button letters, e.g. "XIA"
    std::string rightButtons; // ... e.g. "IAX"
    bool centeredTitle = false;
    float titlebarHeight = 24.0F;
    bool dots = false;
  };

  // One titlebar button. The dots themes draw a neutral palette-tinted circle,
  // the standard themes the matching flat glyph. "_" is a spacer and draws
  // nothing.
  void addTitlebarButton(Flex& row, char letter, float scale, bool dots) {
    constexpr float kPlaceholderSize = 9.0F;
    constexpr float kGlyphSize = 8.0F;
    const float size = kPlaceholderSize * scale;
    if (letter == '_') {
      row.addChild(ui::row({.width = size, .height = size}));
      return;
    }
    const bool close = letter == 'X';
    if (dots) {
      row.addChild(ui::row({
          .fill = colorSpecFromRole(close ? ColorRole::Error : ColorRole::OnSurfaceVariant, close ? 0.85F : 0.55F),
          .radius = size / 2.0F,
          .width = size,
          .height = size,
      }));
      return;
    }
    std::string glyphName = "minus";
    if (close) {
      glyphName = "close";
    } else if (letter == 'A') {
      glyphName = "square";
    }
    row.addChild(ui::glyph({
        .glyph = std::move(glyphName),
        .glyphSize = kGlyphSize * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .width = size,
        .height = size,
    }));
  }

  [[nodiscard]] std::unique_ptr<Flex> titlebarButtonRow(
      std::string_view letters, float scale, FlexJustify justify, float grow, bool dots
  ) {
    auto row =
        ui::row({.align = FlexAlign::Center, .justify = justify, .gap = Style::spaceXs * scale, .flexGrow = grow});
    for (const char letter : letters) {
      addTitlebarButton(*row, letter, scale, dots);
    }
    return row;
  }

  // A drawn window mock keeps the tile reading as a window thumbnail instead of
  // a generic glyph. Palette roles make it track the active scheme, the way KDE
  // renders its live decoration previews with the current colors.
  [[nodiscard]] std::unique_ptr<Flex>
  buildDecorationMockPreview(float scale, const TitlebarStyle& style) {
    constexpr float kContentBarHeight = 6.0F;
    constexpr float kTitlebarMinHeight = 18.0F;
    constexpr float kTitlebarMaxHeight = 34.0F;
    const float titlebarHeight = std::clamp(style.titlebarHeight, kTitlebarMinHeight, kTitlebarMaxHeight) * scale;

    auto titlebar = ui::row({
        .align = FlexAlign::Center,
        .justify = FlexJustify::SpaceBetween,
        .paddingH = Style::spaceSm * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceVariant),
        // Matches the outer radius so the titlebar corners align with the
        // window frame (Flex clipping is rectangular, not rounded).
        .radius = Style::scaledRadiusMd(scale),
        .width = kDecorationPreviewWidth * scale,
        .height = titlebarHeight,
    });

    auto title = ui::row({
        .fill = colorSpecFromRole(ColorRole::OnSurfaceVariant, 0.5F),
        .radius = (5.0F * scale) / 2.0F,
        .width = 46.0F * scale,
        .height = 5.0F * scale,
    });

    if (style.centeredTitle) {
      // Equal-growing side columns keep the title centered whatever the button
      // counts are.
      titlebar->addChild(titlebarButtonRow(style.leftButtons, scale, FlexJustify::Start, 1.0F, style.dots));
      titlebar->addChild(std::move(title));
      titlebar->addChild(titlebarButtonRow(style.rightButtons, scale, FlexJustify::End, 1.0F, style.dots));
    } else {
      if (!style.leftButtons.empty()) {
        titlebar->addChild(titlebarButtonRow(style.leftButtons, scale, FlexJustify::Start, 0.0F, style.dots));
      }
      titlebar->addChild(std::move(title));
      titlebar->addChild(ui::row({.flexGrow = 1.0F}));
      titlebar->addChild(titlebarButtonRow(style.rightButtons, scale, FlexJustify::End, 0.0F, style.dots));
    }

    auto content = ui::column(
        {
            .align = FlexAlign::Start,
            .gap = Style::spaceXs * scale,
            .padding = Style::spaceSm * scale,
            .fill = clearColorSpec(),
            .fillWidth = true,
            .flexGrow = 1.0F,
        },
        ui::row({
            .fill = colorSpecFromRole(ColorRole::OnSurfaceVariant, 0.28F),
            .radius = (kContentBarHeight * scale) / 2.0F,
            .width = 88.0F * scale,
            .height = kContentBarHeight * scale,
        }),
        ui::row({
            .fill = colorSpecFromRole(ColorRole::OnSurfaceVariant, 0.18F),
            .radius = (kContentBarHeight * scale) / 2.0F,
            .width = 62.0F * scale,
            .height = kContentBarHeight * scale,
        })
    );

    return ui::column(
        {
            .align = FlexAlign::Stretch,
            .justify = FlexJustify::Start,
            .gap = 0.0F,
            .fill = colorSpecFromRole(ColorRole::Surface),
            .radius = Style::scaledRadiusMd(scale),
            .border = colorSpecFromRole(ColorRole::Outline),
            .borderWidth = Style::borderWidth,
            .clipChildren = true,
            .width = kDecorationPreviewWidth * scale,
            .height = kDecorationPreviewHeight * scale,
        },
        std::move(titlebar),
        std::move(content)
    );
  }

  // One window-decoration tile: a drawn window mock + a name label, with the
  // shared selected/hover styling of the Icons page.
  [[nodiscard]] std::unique_ptr<Node> buildDecorationTile(
      float scale, const std::string& displayName, const TitlebarStyle& titlebarStyle, bool selected,
      std::function<void()> onClick
  ) {
    const float tileW = kDecorationTileWidth * scale;
    const float tileH = kDecorationTileHeight * scale;

    Flex* cardBody = nullptr;
    auto card = ui::column({
        .out = &cardBody,
        .align = FlexAlign::Center,
        .justify = FlexJustify::Center,
        .gap = Style::spaceXs * scale,
        .padding = Style::spaceXs * scale,
        .fill = selected ? colorSpecFromRole(ColorRole::Primary, 0.15F) : colorSpecFromRole(ColorRole::SurfaceVariant),
        .radius = Style::scaledRadiusMd(scale),
        .border = selected ? colorSpecFromRole(ColorRole::Primary) : clearColorSpec(),
        .borderWidth = selected ? Style::emphasizedBorderWidth : 0.0F,
        .width = tileW,
        .height = tileH,
    });

    card->addChild(buildDecorationMockPreview(scale, titlebarStyle));

    card->addChild(ui::label({
        .text = displayName,
        .fontSize = Style::fontSizeCaption * scale,
        .fontWeight = selected ? FontWeight::Bold : FontWeight::Normal,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .maxWidth = (kDecorationTileWidth - Style::spaceSm * 2.0F) * scale,
        .maxLines = 1,
        .ellipsize = TextEllipsize::End,
    }));

    auto tile = ui::inputArea({
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
        .width = tileW,
        .height = tileH,
        .onEnter = [cardBody](const InputArea::PointerData&) {
          if (cardBody != nullptr) {
            cardBody->setBorder(colorSpecFromRole(ColorRole::Hover), Style::borderWidth);
          }
        },
        .onLeave = [cardBody]() {
          if (cardBody != nullptr) {
            cardBody->clearBorder();
          }
        },
        .onClick = [onClick = std::move(onClick)](const InputArea::PointerData&) { onClick(); },
    });
    tile->addChild(std::move(card));
    return tile;
  }

  // One numeric titlebar-metric row: bold title + caption on the left,
  // [Override][Reset][slider][value] on the right, matching the other pages.
  struct MetricRow {
    std::string title;
    std::string subtitle;
    int minValue = 0;
    int maxValue = 100;
    int value = 0;
    std::string suffix;
    std::string keyPath;
    bool overridden = false;
    bool armedReset = false;
    std::function<void(int)> onCommit;
  };

  [[nodiscard]] std::unique_ptr<Node> buildMetricRow(
      const MetricRow& field, float scale, const AppearancePage::Callbacks& callbacks
  ) {
    Slider* sliderPtr = nullptr;
    Label* valuePtr = nullptr;
    auto slider = ui::slider({
        .out = &sliderPtr,
        .minValue = static_cast<double>(field.minValue),
        .maxValue = static_cast<double>(field.maxValue),
        .step = 1.0,
        .value = static_cast<double>(field.value),
        .trackHeight = Style::sliderTrackHeight * scale,
        .thumbSize = Style::sliderThumbSize * scale,
        .controlHeight = Style::controlHeight * scale,
        .width = Style::sliderDefaultWidth * scale,
    });
    auto valueLabel = ui::label({
        .out = &valuePtr,
        .text = std::to_string(field.value) + field.suffix,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .minWidth = 56.0F * scale,
        .textAlign = TextAlign::End,
    });
    slider->setOnValueChanged([valuePtr, suffix = field.suffix](double next) {
      if (valuePtr != nullptr) {
        valuePtr->setText(std::to_string(static_cast<int>(next)) + suffix);
      }
    });
    slider->setOnDragEnd([sliderPtr, commit = field.onCommit]() {
      if (sliderPtr != nullptr && commit) {
        commit(static_cast<int>(sliderPtr->value()));
      }
    });

    auto titleRow = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
    titleRow->addChild(makeLabel(
        field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
    ));
    titleRow->addChild(ui::spacer());

    auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
    copy->addChild(std::move(titleRow));
    copy->addChild(ui::label({
        .text = field.subtitle,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 4,
    }));

    auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
    if (field.overridden) {
      actions->addChild(makeOverrideBadge(scale));
      actions->addChild(makeResetButton(field.keyPath, field.armedReset, scale, callbacks));
    }
    actions->addChild(std::move(slider));
    actions->addChild(std::move(valueLabel));

    return ui::row(
        {.align = FlexAlign::Center,
         .justify = FlexJustify::SpaceBetween,
         .gap = Style::spaceXs * scale,
         .paddingV = 2.0F * scale,
         .fillWidth = true},
        std::move(copy), std::move(actions)
    );
  }

} // namespace

std::unique_ptr<Node> AppearancePage::buildDecorationsSection(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  struct Tile {
    std::string displayName;
    DecorationSelection selection;
    TitlebarStyle titlebarStyle;
  };

  // The selectable variants; Standard Dark is the compiled-in default.
  std::vector<Tile> tiles;
  // The Kinetic decoration is drawn by the compositor itself, so its tiles use
  // a mock layout: title on the left, minimize/maximize/close on the right.
  // Standard draws flat glyphs, Dots the solid circles.
  tiles.push_back(Tile{
      .displayName = "Kinetic Standard Dark",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "dark"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX"},
  });
  tiles.push_back(Tile{
      .displayName = "Kinetic Standard Light",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "light"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX"},
  });
  tiles.push_back(Tile{
      .displayName = "Kinetic Dots Dark",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "dots-dark"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX", .dots = true},
  });
  tiles.push_back(Tile{
      .displayName = "Kinetic Dots Light",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "dots-light"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX", .dots = true},
  });

  const bool searchActive = !filter.searchQuery.empty();
  const bool sectionTitleMatches =
      searchActive && StringUtils::containsInsensitive("Title bars", filter.searchQuery);
  const bool showToggleRow = !searchActive || sectionTitleMatches
      || StringUtils::containsInsensitive("Show title bars", filter.searchQuery)
      || StringUtils::containsInsensitive("Hide title bars", filter.searchQuery)
      || StringUtils::containsInsensitive("Remove the title bar and frame on every window", filter.searchQuery);

  // The size sliders: each matches independently so a search / the Overridden
  // filter can narrow down to a single row.
  const auto matchesAny = [&](std::initializer_list<std::string_view> words) {
    if (!searchActive) {
      return true;
    }
    for (const std::string_view word : words) {
      if (StringUtils::containsInsensitive(word, filter.searchQuery)) {
        return true;
      }
    }
    return false;
  };
  const bool heightOverridden = overriddenTitleBarHeight();
  const bool fontSizeOverridden = overriddenTitleFontSize();
  const bool buttonSizeOverridden = overriddenButtonSize();
  const bool heightVisible = (!filter.showOverriddenOnly || heightOverridden)
      && (sectionTitleMatches || matchesAny({"Title bar height", "Title bar size"}));
  const bool fontSizeVisible = (!filter.showOverriddenOnly || fontSizeOverridden)
      && (sectionTitleMatches || matchesAny({"Title font size", "Title bar font", "Title bar size"}));
  const bool buttonSizeVisible = (!filter.showOverriddenOnly || buttonSizeOverridden)
      && (sectionTitleMatches || matchesAny({"Button size", "Title bar size"}));
  const bool showSizeSection = heightVisible || fontSizeVisible || buttonSizeVisible;

  // The Title bar buttons section: two side-by-side columns, each a per-button
  // toggle row, plus the window-title toggle. It matches the section title, an
  // individual button/label, or the Overridden filter when any part differs from
  // the default.
  const bool buttonsOverridden =
      overriddenButtonsOnLeft() || overriddenButtonsOnRight() || overriddenShowTitle();
  const auto matchesButtonOption = [&]() {
    if (!searchActive) {
      return true;
    }
    if (StringUtils::containsInsensitive("Title bar buttons", filter.searchQuery)
        || StringUtils::containsInsensitive("Title bar button", filter.searchQuery)
        || StringUtils::containsInsensitive("Window title", filter.searchQuery)) {
      return true;
    }
    for (const auto& option : buttonOptions()) {
      if (StringUtils::containsInsensitive(option.label, filter.searchQuery)) {
        return true;
      }
    }
    return false;
  };
  const bool buttonsVisible =
      (!filter.showOverriddenOnly || buttonsOverridden) && (sectionTitleMatches || matchesButtonOption());

  const DecorationSelection current = effectiveDecoration();

  std::vector<const Tile*> visible;
  for (const auto& tile : tiles) {
    const bool selected = tile.selection == current;
    if (filter.showOverriddenOnly && !selected) {
      continue;
    }
    if (searchActive && !sectionTitleMatches
        && !StringUtils::containsInsensitive(tile.displayName, filter.searchQuery)) {
      continue;
    }
    visible.push_back(&tile);
  }
  if (visible.empty() && !showToggleRow && !showSizeSection && !buttonsVisible) {
    return nullptr;
  }

  auto section = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});

  // Global default: show every title bar and frame unless a per-window rule
  // overrides it. The stored [Windows] DefaultDecorationPolicy is unchanged
  // (0 = no decoration / hidden, 1 = client preference / shown); only the
  // toggle's sense flips so off means no title bar and on means title bars.
  if (showToggleRow) {
    const bool showTitleBars = !effectiveHideTitleBars();
    auto card = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceMd * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.5F),
        .radius = Style::scaledRadiusMd(scale),
        .fillWidth = true,
    });
    auto row = ui::row({
        .align = FlexAlign::Center,
        .justify = FlexJustify::SpaceBetween,
        .gap = Style::spaceSm * scale,
        .paddingV = 2.0F * scale,
        .fillWidth = true,
    });
    auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
    copy->addChild(makeLabel(
        "Show title bars", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
    ));
    copy->addChild(ui::label({
        .text =
            "Show the title bar and frame on every window, unless a per-window "
            "rule overrides it for a specific application.",
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 4,
    }));
    row->addChild(std::move(copy));
    row->addChild(ui::toggle({
        .checked = showTitleBars,
        .scale = scale,
        .onChange =
            [this, callbacks](bool show) {
              stageHideTitleBars(!show);
              if (callbacks.requestRebuild) {
                callbacks.requestRebuild();
              }
            },
    }));
    card->addChild(std::move(row));
    section->addChild(std::move(card));
  }

  if (!visible.empty()) {
    // Action row: the override/reset controls (right-aligned).
    auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
    header->addChild(ui::spacer());
    if (overriddenDecoration()) {
      header->addChild(makeOverrideBadge(scale));
      header->addChild(makeResetButton(decorationKeyPath(), armedResetKey == decorationKeyPath(), scale, callbacks));
    }
    section->addChild(std::move(header));

    auto grid = ui::row({
        .align = FlexAlign::Start,
        .wrap = true,
        .gap = Style::spaceMd * scale,
        .fillWidth = true,
    });
    for (const Tile* tile : visible) {
      const bool selected = tile->selection == current;
      grid->addChild(buildDecorationTile(
          scale, tile->displayName, tile->titlebarStyle, selected,
          [this, selection = tile->selection, callbacks]() { stageDecorationAndRebuild(selection, callbacks); }
      ));
    }
    section->addChild(std::move(grid));
  }

  // Kinetic titlebar metrics, written to kineticwe.kwe on Apply. Left as an
  // explicit value only when the user moves a slider; otherwise the compositor
  // keeps deriving the size from the title font and grid unit.
  if (showSizeSection) {
    auto card = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceMd * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.5F),
        .radius = Style::scaledRadiusMd(scale),
        .fillWidth = true,
    });
    card->addChild(makeLabel(
        "Title bar size", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
    ));

    if (heightVisible) {
      card->addChild(buildMetricRow(
          MetricRow{
              .title = "Title bar height",
              .subtitle = "Height of the title bar in logical pixels. It never shrinks below what the "
                          "buttons need.",
              .minValue = 18,
              .maxValue = 72,
              .value = effectiveTitleBarHeight(),
              .suffix = " px",
              .keyPath = titleBarHeightKeyPath(),
              .overridden = heightOverridden,
              .armedReset = armedResetKey == titleBarHeightKeyPath(),
              .onCommit =
                  [this, callbacks](int value) {
                    stageTitleBarHeight(value);
                    if (callbacks.requestRebuild) {
                      callbacks.requestRebuild();
                    }
                  },
          },
          scale, callbacks
      ));
    }

    if (fontSizeVisible) {
      card->addChild(buildMetricRow(
          MetricRow{
              .title = "Title font size",
              .subtitle = "Point size of the window title.",
              .minValue = 6,
              .maxValue = 20,
              .value = effectiveTitleFontSize(),
              .suffix = " pt",
              .keyPath = titleFontSizeKeyPath(),
              .overridden = fontSizeOverridden,
              .armedReset = armedResetKey == titleFontSizeKeyPath(),
              .onCommit =
                  [this, callbacks](int value) {
                    stageTitleFontSize(value);
                    if (callbacks.requestRebuild) {
                      callbacks.requestRebuild();
                    }
                  },
          },
          scale, callbacks
      ));
    }

    if (buttonSizeVisible) {
      card->addChild(buildMetricRow(
          MetricRow{
              .title = "Button size",
              .subtitle = "Side length of the minimize, maximize and close buttons in logical pixels.",
              .minValue = 12,
              .maxValue = 48,
              .value = effectiveButtonSize(),
              .suffix = " px",
              .keyPath = buttonSizeKeyPath(),
              .overridden = buttonSizeOverridden,
              .armedReset = armedResetKey == buttonSizeKeyPath(),
              .onCommit =
                  [this, callbacks](int value) {
                    stageButtonSize(value);
                    if (callbacks.requestRebuild) {
                      callbacks.requestRebuild();
                    }
                  },
          },
          scale, callbacks
      ));
    }

    section->addChild(std::move(card));
  }

  if (buttonsVisible) {
    auto card = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceMd * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.5F),
        .radius = Style::scaledRadiusMd(scale),
        .fillWidth = true,
    });
    card->addChild(makeLabel(
        "Title bar buttons", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
    ));

    // Whether the decoration paints the caption text (KineticShowTitle).
    auto titleRow = ui::row({
        .align = FlexAlign::Center,
        .justify = FlexJustify::SpaceBetween,
        .gap = Style::spaceXs * scale,
        .paddingV = 2.0F * scale,
        .fillWidth = true,
    });
    auto titleLabel = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
    titleLabel->addChild(ui::label({
        .text = "Window title",
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurface),
    }));
    if (overriddenShowTitle()) {
      titleLabel->addChild(makeOverrideBadge(scale));
      titleLabel->addChild(makeResetButton(
          showTitleKeyPath(), armedResetKey == showTitleKeyPath(), scale, callbacks
      ));
    }
    titleRow->addChild(std::move(titleLabel));
    titleRow->addChild(ui::toggle({
        .checked = effectiveShowTitle(),
        .scale = scale,
        .onChange = [this, callbacks](bool show) {
          stageShowTitle(show);
          if (callbacks.requestRebuild) {
            callbacks.requestRebuild();
          }
        },
    }));
    card->addChild(std::move(titleRow));

    const auto buildButtonSide = [&](bool left, std::string_view sideLabel) -> std::unique_ptr<Node> {
      const std::string path = left ? buttonsOnLeftKeyPath() : buttonsOnRightKeyPath();
      const std::string current = left ? effectiveButtonsOnLeft() : effectiveButtonsOnRight();
      const bool sideOverridden = left ? overriddenButtonsOnLeft() : overriddenButtonsOnRight();

      auto column = ui::column({
          .align = FlexAlign::Stretch,
          .gap = Style::spaceXs * scale,
          .flexGrow = 1.0F,
      });

      auto header = ui::row({
          .align = FlexAlign::Center,
          .gap = Style::spaceSm * scale,
          .fillWidth = true,
      });
      header->addChild(makeLabel(
          sideLabel, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
      ));
      if (sideOverridden) {
        header->addChild(makeOverrideBadge(scale));
        header->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
      }
      header->addChild(ui::spacer());
      column->addChild(std::move(header));

      for (const auto& option : buttonOptions()) {
        const bool checked = current.find(option.letter) != std::string::npos;

        auto row = ui::row({
            .align = FlexAlign::Center,
            .justify = FlexJustify::SpaceBetween,
            .gap = Style::spaceXs * scale,
            .paddingV = 2.0F * scale,
            .fillWidth = true,
        });
        row->addChild(ui::label({
            .text = std::string(option.label),
            .fontSize = Style::fontSizeCaption * scale,
            .color = colorSpecFromRole(ColorRole::OnSurface),
        }));
        row->addChild(ui::toggle({
            .checked = checked,
            .scale = scale,
            .onChange = [this, left, letter = option.letter, callbacks](bool on) {
              const std::string currentSide = left ? effectiveButtonsOnLeft() : effectiveButtonsOnRight();
              const std::string next = toggleButton(currentSide, letter, on);
              if (left) {
                stageButtonsOnLeft(next);
              } else {
                stageButtonsOnRight(next);
              }
              if (callbacks.requestRebuild) {
                callbacks.requestRebuild();
              }
            },
        }));
        column->addChild(std::move(row));
      }
      return column;
    };

    card->addChild(ui::row(
        {.align = FlexAlign::Stretch, .gap = Style::spaceLg * scale, .fillWidth = true},
        buildButtonSide(true, "Left side"),
        buildButtonSide(false, "Right side")
    ));
    section->addChild(std::move(card));
  }
  return section;
}

std::unique_ptr<Node> AppearancePage::build(
    float scale, Renderer&, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

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

  bool anyRows = false;

  // The titlebar picker is this page's only section, using the same tile grid
  // as the Icons page.
  if (auto decorations = buildDecorationsSection(scale, filter, armedResetKey, callbacks);
      decorations != nullptr) {
    root->addChild(
        makeLabel("Title bars", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold)
    );
    root->addChild(std::move(decorations));
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
