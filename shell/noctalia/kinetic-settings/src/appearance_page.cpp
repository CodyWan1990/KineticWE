#include "appearance_page.h"

#include "core/log.h"
#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/label.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
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
  if (!m_pendingDecoration.has_value() && !m_pendingHideTitleBars.has_value()) {
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
  m_errorNotice.clear();
  return true;
}

std::vector<std::string> AppearancePage::resettableKeyPaths() const {
  return {decorationKeyPath(), titleBarsKeyPath()};
}

void AppearancePage::resetKeyPath(std::string_view path) {
  if (path == decorationKeyPath()) {
    stageDecoration(defaultDecoration());
  } else if (path == titleBarsKeyPath()) {
    stageHideTitleBars(false);
  }
}

void AppearancePage::resetAll() {
  stageDecoration(defaultDecoration());
  stageHideTitleBars(false);
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
  // buttons, whether the title is centered, and the titlebar height.
  struct TitlebarStyle {
    std::string leftButtons;  // KDE button letters, e.g. "XIA"
    std::string rightButtons; // ... e.g. "IAX"
    bool centeredTitle = false;
    float titlebarHeight = 24.0F;
  };

  // One titlebar button: a neutral palette-tinted dot. "_" is a spacer and
  // draws nothing.
  void addTitlebarButton(Flex& row, char letter, float scale) {
    constexpr float kPlaceholderSize = 9.0F;
    if (letter == '_') {
      const float spacer = kPlaceholderSize * scale;
      row.addChild(ui::row({.width = spacer, .height = spacer}));
      return;
    }
    const bool close = letter == 'X';
    const float size = kPlaceholderSize * scale;
    row.addChild(ui::row({
        .fill = colorSpecFromRole(close ? ColorRole::Error : ColorRole::OnSurfaceVariant, close ? 0.85F : 0.55F),
        .radius = size / 2.0F,
        .width = size,
        .height = size,
    }));
  }

  [[nodiscard]] std::unique_ptr<Flex> titlebarButtonRow(
      std::string_view letters, float scale, FlexJustify justify, float grow
  ) {
    auto row =
        ui::row({.align = FlexAlign::Center, .justify = justify, .gap = Style::spaceXs * scale, .flexGrow = grow});
    for (const char letter : letters) {
      addTitlebarButton(*row, letter, scale);
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
      titlebar->addChild(titlebarButtonRow(style.leftButtons, scale, FlexJustify::Start, 1.0F));
      titlebar->addChild(std::move(title));
      titlebar->addChild(titlebarButtonRow(style.rightButtons, scale, FlexJustify::End, 1.0F));
    } else {
      if (!style.leftButtons.empty()) {
        titlebar->addChild(titlebarButtonRow(style.leftButtons, scale, FlexJustify::Start, 0.0F));
      }
      titlebar->addChild(std::move(title));
      titlebar->addChild(ui::row({.flexGrow = 1.0F}));
      titlebar->addChild(titlebarButtonRow(style.rightButtons, scale, FlexJustify::End, 0.0F));
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

} // namespace

std::unique_ptr<Node> AppearancePage::buildDecorationsSection(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  struct Tile {
    std::string displayName;
    DecorationSelection selection;
    TitlebarStyle titlebarStyle;
  };

  // The two selectable variants; Dark is the compiled-in default.
  std::vector<Tile> tiles;
  // The Kinetic decoration is drawn by the compositor itself, so its tiles use
  // a mock layout: title on the left, minimize/maximize/close dots on the right.
  tiles.push_back(Tile{
      .displayName = "Kinetic Dark",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "dark"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX"},
  });
  tiles.push_back(Tile{
      .displayName = "Kinetic Light",
      .selection = DecorationSelection{.library = "kineticwe", .theme = "light"},
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX"},
  });

  const bool searchActive = !filter.searchQuery.empty();
  const bool sectionTitleMatches =
      searchActive && StringUtils::containsInsensitive("Title bars", filter.searchQuery);
  const bool showToggleRow = !searchActive || sectionTitleMatches
      || StringUtils::containsInsensitive("Show title bars", filter.searchQuery)
      || StringUtils::containsInsensitive("Hide title bars", filter.searchQuery)
      || StringUtils::containsInsensitive("Remove the title bar and frame on every window", filter.searchQuery);
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
  if (visible.empty() && !showToggleRow) {
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
