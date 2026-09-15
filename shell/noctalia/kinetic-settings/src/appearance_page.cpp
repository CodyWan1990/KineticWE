#include "appearance_page.h"

#include "kinetic-settings/src/theme_discovery.h"

#include "core/log.h"
#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "render/core/renderer.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
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

  // A representative preview image for a decoration theme: a bundled preview
  // image when present, otherwise empty so the caller draws a window mock.
  // decoration.svg is deliberately NOT used — for Aurorae themes it is the
  // nine-slice artwork atlas, not a preview (rendered flat it reads as blobs).
  [[nodiscard]] std::string decorationPreviewFile(const std::string& themePath) {
    if (themePath.empty()) {
      return {};
    }
    std::error_code ec;
    const std::filesystem::path dir(themePath);
    for (const char* name : {"preview.png", "preview.jpg", "preview.jpeg", "preview.svg"}) {
      const std::filesystem::path candidate = dir / name;
      if (std::filesystem::is_regular_file(candidate, ec)) {
        return candidate.string();
      }
    }
    return {};
  }

  // KineticWE's display names for the Aurorae themes vendored under
  // data/aurorae-themes/ (see that dir's README): the transparent dark variant
  // reads as "Neutral", the flat one as "Solid". Every other discovered theme
  // keeps its own metadata Name=.
  [[nodiscard]] std::string titlebarDisplayName(std::string_view name, std::string_view fallback) {
    static constexpr std::pair<std::string_view, std::string_view> kRenamed[] = {
        {"Layan", "Neutral"},
        {"Layan-solid", "Solid"},
    };
    for (const auto& [themeName, label] : kRenamed) {
      if (name == themeName) {
        return std::string(label);
      }
    }
    return std::string(fallback);
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
  if (!m_pendingDecoration.has_value()) {
    return true;
  }
  ensureLoaded();
  // applyDecoration() writes kineticwe.kwe [org.kde.kdecoration2] and asks the
  // compositor to reconfigure; it sets m_errorNotice on failure.
  if (!applyDecoration(*m_pendingDecoration)) {
    return false;
  }
  m_pendingDecoration.reset();
  m_errorNotice.clear();
  return true;
}

std::vector<std::string> AppearancePage::resettableKeyPaths() const {
  return {decorationKeyPath()};
}

void AppearancePage::resetKeyPath(std::string_view path) {
  if (path == decorationKeyPath()) {
    stageDecoration(defaultDecoration());
  }
}

void AppearancePage::resetAll() {
  stageDecoration(defaultDecoration());
}

// ---------------------------------------------------------------------------
// Title bars: the window decoration selection (inline section on this page).
// Stored in the compositor's kineticwe.kwe [org.kde.kdecoration2]; Breeze is the
// compiled-in default.
// ---------------------------------------------------------------------------

AppearancePage::DecorationSelection AppearancePage::defaultDecoration() {
  return DecorationSelection{.library = "org.kde.breeze", .theme = "Breeze"};
}

AppearancePage::DecorationSelection AppearancePage::fileBackedDecoration() const {
  return DecorationSelection{
      .library = m_decorationsConfig.readString("org.kde.kdecoration2", "library").value_or("org.kde.breeze"),
      .theme = m_decorationsConfig.readString("org.kde.kdecoration2", "theme").value_or("Breeze"),
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

bool AppearancePage::applyDecoration(const DecorationSelection& selection) {
  // Reload first so edits applied from other pages in the meantime are kept.
  m_decorationsConfig.load();
  if (selection == defaultDecoration()) {
    m_decorationsConfig.removeKey("org.kde.kdecoration2", "library");
    m_decorationsConfig.removeKey("org.kde.kdecoration2", "theme");
  } else {
    m_decorationsConfig.setString("org.kde.kdecoration2", "library", selection.library);
    m_decorationsConfig.setString("org.kde.kdecoration2", "theme", selection.theme);
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

  // How a drawn window mock renders its titlebar. Mirrors the theme's own
  // Aurorae rc ([General] TitleAlignment / LeftButtons / RightButtons and
  // [Layout] TitleHeight + title edges). The compiled-in Breeze decoration has
  // no rc, so its tile keeps the historical mock layout (title on the left,
  // placeholder buttons on the right).
  struct TitlebarStyle {
    std::string leftButtons;  // KDE button letters, e.g. "XIA"
    std::string rightButtons; // ... e.g. "IAX"
    bool centeredTitle = false;
    float titlebarHeight = 24.0F;
    std::string buttonArtPath; // theme dir for real button SVGs; empty = placeholders
  };

  // One titlebar button: the theme's own artwork when it ships any (Layan ships
  // close/minimize/maximize SVGs), otherwise a neutral palette-tinted dot. "_"
  // is Aurorae's explicit spacer and draws nothing.
  void addTitlebarButton(Flex& row, Renderer& renderer, char letter, const TitlebarStyle& style, float scale) {
    // Placeholder dots keep the historical Breeze mock size; real theme artwork
    // (Layan's buttons are 22px canvases holding 16px glyphs) needs a bit more
    // room to stay legible.
    constexpr float kPlaceholderSize = 9.0F;
    constexpr float kArtworkSize = 12.0F;
    if (letter == '_') {
      const float spacer = kPlaceholderSize * scale;
      row.addChild(ui::row({.width = spacer, .height = spacer}));
      return;
    }
    if (const std::string art = theme_discovery::decorationButtonFile(style.buttonArtPath, letter); !art.empty()) {
      const float size = kArtworkSize * scale;
      auto img = ui::image({
          .fit = ImageFit::Contain,
          .width = size,
          .height = size,
      });
      img->setSourceFile(renderer, art, static_cast<int>(size * 2), true);
      row.addChild(std::move(img));
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
      Renderer& renderer, std::string_view letters, const TitlebarStyle& style, float scale, FlexJustify justify,
      float grow
  ) {
    auto row =
        ui::row({.align = FlexAlign::Center, .justify = justify, .gap = Style::spaceXs * scale, .flexGrow = grow});
    for (const char letter : letters) {
      addTitlebarButton(*row, renderer, letter, style, scale);
    }
    return row;
  }

  // A drawn window mock used when a decoration ships no preview image: the
  // compiled-in Breeze decoration has no artwork on disk, and an Aurorae theme's
  // decoration.svg is the nine-slice atlas (not a preview). Drawing a titlebar
  // and content area keeps the tile reading as a window thumbnail instead of a
  // generic glyph. Palette roles make it track the active scheme, the way KDE
  // renders its live decoration previews with the current colors.
  [[nodiscard]] std::unique_ptr<Flex>
  buildDecorationMockPreview(float scale, Renderer& renderer, const TitlebarStyle& style) {
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
      // counts are, like the real centered Aurorae titlebar.
      titlebar->addChild(titlebarButtonRow(renderer, style.leftButtons, style, scale, FlexJustify::Start, 1.0F));
      titlebar->addChild(std::move(title));
      titlebar->addChild(titlebarButtonRow(renderer, style.rightButtons, style, scale, FlexJustify::End, 1.0F));
    } else {
      if (!style.leftButtons.empty()) {
        titlebar->addChild(titlebarButtonRow(renderer, style.leftButtons, style, scale, FlexJustify::Start, 0.0F));
      }
      titlebar->addChild(std::move(title));
      titlebar->addChild(ui::row({.flexGrow = 1.0F}));
      titlebar->addChild(titlebarButtonRow(renderer, style.rightButtons, style, scale, FlexJustify::End, 0.0F));
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

  // One window-decoration tile: preview (bundled preview image or a drawn
  // window mock) + a name label, with the shared selected/hover styling of the
  // Icons page.
  [[nodiscard]] std::unique_ptr<Node> buildDecorationTile(
      float scale, Renderer& renderer, const std::string& displayName, const std::string& themePath,
      const TitlebarStyle& titlebarStyle, bool selected, std::function<void()> onClick
  ) {
    const float tileW = kDecorationTileWidth * scale;
    const float tileH = kDecorationTileHeight * scale;
    const float previewW = kDecorationPreviewWidth * scale;
    const float previewH = kDecorationPreviewHeight * scale;

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

    const std::string previewPath = decorationPreviewFile(themePath);
    if (!previewPath.empty()) {
      auto img = ui::image({
          .fit = ImageFit::Contain,
          .width = previewW,
          .height = previewH,
      });
      img->setSourceFile(renderer, previewPath, static_cast<int>(previewW * 2), true);
      card->addChild(std::move(img));
    } else {
      card->addChild(buildDecorationMockPreview(scale, renderer, titlebarStyle));
    }

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
    float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  struct Tile {
    std::string displayName;
    std::string path;
    DecorationSelection selection;
    TitlebarStyle titlebarStyle;
  };

  // Breeze is the compiled-in default; every discovered Aurorae theme is
  // applied through the org.kde.kwin.aurorae.v2 plugin with the
  // __aurorae__svg__<Name> theme id. Only the labels are KineticWE's names for
  // the vendored styles (Breeze -> "Dynamic", Layan -> "Neutral", Layan-solid
  // -> "Solid"); the stored (library, theme) pair, and every other discovered
  // theme's own Name=, are unchanged.
  std::vector<Tile> tiles;
  // The compiled-in Breeze decoration ships no artwork, so its tile keeps the
  // historical mock layout: title on the left, minimize/maximize/close
  // placeholder dots on the right.
  tiles.push_back(Tile{
      .displayName = "Dynamic",
      .path = {},
      .selection = defaultDecoration(),
      .titlebarStyle = TitlebarStyle{.rightButtons = "IAX"},
  });
  for (const auto& theme : theme_discovery::decorationThemes()) {
    const std::string displayName = titlebarDisplayName(theme.name, theme.displayName);
    // An Aurorae theme previews as a drawn window whose titlebar mirrors the
    // theme's own rc (title alignment, button sides, titlebar height) and, when
    // the theme ships button artwork, its real button SVGs.
    tiles.push_back(Tile{
        .displayName = displayName,
        .path = theme.path,
        .selection =
            DecorationSelection{.library = "org.kde.kwin.aurorae.v2", .theme = "__aurorae__svg__" + theme.name},
        .titlebarStyle =
            TitlebarStyle{
                .leftButtons = theme.leftButtons,
                .rightButtons = theme.rightButtons,
                .centeredTitle = theme.titleAlignment == "center",
                .titlebarHeight = static_cast<float>(theme.titlebarHeight),
                .buttonArtPath = theme.path,
            },
    });
  }

  const bool searchActive = !filter.searchQuery.empty();
  const bool sectionTitleMatches =
      searchActive && StringUtils::containsInsensitive("Title bars", filter.searchQuery);
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
  if (visible.empty()) {
    return nullptr;
  }

  auto section = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});

  // Action row: folder button, then the override/reset controls.
  auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  header->addChild(ui::button({
      .glyph = "folder-open",
      .glyphSize = Style::fontSizeBody * scale,
      .variant = ButtonVariant::Ghost,
      .tooltip = "Open the window decorations folder",
      .minHeight = Style::controlHeightSm * scale,
      .padding = Style::spaceXs * scale,
      .radius = Style::scaledRadiusMd(scale),
      .onClick = []() { (void)theme_discovery::openDecorationRoot(); },
  }));
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
        scale, renderer, tile->displayName, tile->path, tile->titlebarStyle, selected,
        [this, selection = tile->selection, callbacks]() { stageDecorationAndRebuild(selection, callbacks); }
    ));
  }
  section->addChild(std::move(grid));
  return section;
}

std::unique_ptr<Node> AppearancePage::build(
    float scale, Renderer& renderer, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
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
  if (auto decorations = buildDecorationsSection(scale, renderer, filter, armedResetKey, callbacks);
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
