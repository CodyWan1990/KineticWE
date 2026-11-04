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
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Window decoration tiles (a wider tile than the Icons page so the
  // decoration preview reads like a window).
  constexpr float kDecorationTileWidth = 200.0F;
  constexpr float kDecorationTileHeight = 150.0F;
  constexpr float kDecorationPreviewWidth = 168.0F;
  constexpr float kDecorationPreviewHeight = 96.0F;

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
  if (!hasPending()) {
    return true;
  }
  ensureLoaded();
  // applyTitleBars() writes kineticwe.kwe ([org.kde.kdecoration2] + [Windows])
  // and asks the compositor to reconfigure; it sets m_errorNotice on failure.
  if (!applyTitleBars()) {
    return false;
  }
  m_pending.clear();
  m_pendingDecoration.reset();
  m_pendingButtonsOnLeft.reset();
  m_pendingButtonsOnRight.reset();
  m_errorNotice.clear();
  return true;
}

std::vector<std::string> AppearancePage::resettableKeyPaths() const {
  std::vector<std::string> paths{decorationKeyPath(), buttonsOnLeftKeyPath(), buttonsOnRightKeyPath()};
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void AppearancePage::resetKeyPath(std::string_view path) {
  if (path == decorationKeyPath()) {
    stageDecoration(defaultDecoration());
  } else if (path == buttonsOnLeftKeyPath()) {
    stageButtonsOnLeft(std::string(kDefaultButtonsOnLeft));
  } else if (path == buttonsOnRightKeyPath()) {
    stageButtonsOnRight(std::string(kDefaultButtonsOnRight));
  } else if (const Field* field = findField(path); field != nullptr) {
    stage(*field, field->fallback);
  }
}

void AppearancePage::resetAll() {
  stageDecoration(defaultDecoration());
  stageButtonsOnLeft(std::string(kDefaultButtonsOnLeft));
  stageButtonsOnRight(std::string(kDefaultButtonsOnRight));
  for (const auto& field : fields()) {
    stage(field, field.fallback);
  }
}

// ---------------------------------------------------------------------------
// Settings table: the plain toggles and sliders. Each entry's fallback is the
// compiled-in default (its type is the value type); a value equal to it
// removes the key on Apply.
// ---------------------------------------------------------------------------

const std::vector<AppearancePage::Field>& AppearancePage::fields() {
  static const std::vector<Field> table{
      // Global default: [Windows] DefaultDecorationPolicy, 0 = None (no title
      // bar/frame), 1 = ClientPreference (shown). A per-window rule can still
      // override it. Any non-zero value reads as shown.
      Field{
          .group = "Windows",
          .key = "DefaultDecorationPolicy",
          .title = "Show title bars",
          .subtitle = "Show the title bar and frame on every window, unless a per-window "
                      "rule overrides it for a specific application.",
          .card = Card::TitleBars,
          .boolAsInt = true,
          .fallback = Value{true},
      },
      // Kinetic titlebar metrics. The compositor derives the real default from
      // the title font/grid unit when the key is absent; these fallbacks
      // approximate it.
      Field{
          .group = "org.kde.kdecoration2",
          .key = "KineticTitleBarHeight",
          .control = ControlKind::Slider,
          .title = "Title bar height",
          .subtitle = "Height of the title bar in logical pixels. It never shrinks below what the "
                      "buttons need.",
          .card = Card::Size,
          .minValue = 18,
          .maxValue = 72,
          .valueSuffix = " px",
          .fallback = Value{30},
      },
      Field{
          .group = "org.kde.kdecoration2",
          .key = "KineticTitleFontSize",
          .control = ControlKind::Slider,
          .title = "Title font size",
          .subtitle = "Point size of the window title.",
          .card = Card::Size,
          .minValue = 6,
          .maxValue = 20,
          .valueSuffix = " pt",
          .fallback = Value{9},
      },
      Field{
          .group = "org.kde.kdecoration2",
          .key = "KineticButtonSize",
          .control = ControlKind::Slider,
          .title = "Button size",
          .subtitle = "Side length of the minimize, maximize and close buttons in logical pixels.",
          .card = Card::Size,
          .minValue = 12,
          .maxValue = 48,
          .valueSuffix = " px",
          .fallback = Value{22},
      },
      // Whether the decoration paints the window caption text.
      Field{
          .group = "org.kde.kdecoration2",
          .key = "KineticShowTitle",
          .title = "Window title",
          .card = Card::Buttons,
          .fallback = Value{true},
      },
      // Whether captions use the shell font; the family itself is mirrored by
      // syncTitleFontFamily().
      Field{
          .group = "org.kde.kdecoration2",
          .key = "KineticTitleFollowShellFont",
          .title = "Use shell font for title bars",
          .card = Card::Buttons,
          .fallback = Value{true},
      },
  };
  return table;
}

std::string AppearancePage::keyPath(const Field& field) {
  return std::string(field.group) + '.' + std::string(field.key);
}

const AppearancePage::Field* AppearancePage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

std::string_view AppearancePage::cardTitle(Card card) {
  switch (card) {
  case Card::TitleBars:
    return "Title bars";
  case Card::Size:
    return "Title bar size";
  case Card::Buttons:
    return "Title bar buttons";
  }
  return {};
}

AppearancePage::Value AppearancePage::fileBackedValue(const Field& field) const {
  if (const bool* fallback = std::get_if<bool>(&field.fallback)) {
    if (field.boolAsInt) {
      return Value{m_decorationsConfig.readInt(field.group, field.key, *fallback ? 1 : 0) != 0};
    }
    return Value{m_decorationsConfig.readBool(field.group, field.key, *fallback)};
  }
  return Value{m_decorationsConfig.readInt(field.group, field.key, std::get<int>(field.fallback))};
}

AppearancePage::Value AppearancePage::effectiveValue(const Field& field) const {
  if (const auto it = m_pending.find(keyPath(field)); it != m_pending.end()) {
    return it->second;
  }
  return fileBackedValue(field);
}

bool AppearancePage::overridden(const Field& field) const {
  return effectiveValue(field) != field.fallback;
}

bool AppearancePage::matchesFilter(const Field& field, const Filter& filter) const {
  if (filter.showOverriddenOnly && !overridden(field)) {
    return false;
  }
  const std::string& query = filter.searchQuery;
  if (query.empty()) {
    return true;
  }
  return StringUtils::containsInsensitive("Title bars", query) || StringUtils::containsInsensitive(field.title, query)
      || StringUtils::containsInsensitive(field.subtitle, query)
      || StringUtils::containsInsensitive(cardTitle(field.card), query);
}

void AppearancePage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  if (value == fileBackedValue(field)) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void AppearancePage::writeValue(const Field& field, const Value& value) {
  if (value == field.fallback) {
    m_decorationsConfig.removeKey(field.group, field.key);
    return;
  }
  if (const bool* b = std::get_if<bool>(&value)) {
    if (field.boolAsInt) {
      m_decorationsConfig.setInt(field.group, field.key, *b ? 1 : 0);
    } else {
      m_decorationsConfig.setBool(field.group, field.key, *b);
    }
    return;
  }
  m_decorationsConfig.setInt(field.group, field.key, std::get<int>(value));
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
  for (const auto& [path, value] : m_pending) {
    if (const Field* field = findField(path); field != nullptr) {
      writeValue(*field, value);
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
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer),
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
            .border = colorSpecFromRole(ColorRole::OutlineVariant),
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
        .fill = selected ? colorSpecFromRole(ColorRole::Primary, 0.15F) : colorSpecFromRole(ColorRole::SurfaceContainer),
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

// One table row. Full rows (with a subtitle): bold title + caption on the left,
// [Override][Reset][control] on the right. Compact rows (no subtitle): a
// caption-size label with the badge/reset beside it, control on the right.
std::unique_ptr<Node> AppearancePage::buildFieldRow(
    const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const Value value = effectiveValue(field);
  const bool isOverridden = overridden(field);

  std::unique_ptr<Node> control;
  if (field.control == ControlKind::Slider) {
    const int current = std::get<int>(value);
    Slider* sliderPtr = nullptr;
    Label* valuePtr = nullptr;
    auto slider = ui::slider({
        .out = &sliderPtr,
        .minValue = static_cast<double>(field.minValue),
        .maxValue = static_cast<double>(field.maxValue),
        .step = 1.0,
        .value = static_cast<double>(current),
        .trackHeight = Style::sliderTrackHeight * scale,
        .thumbSize = Style::sliderThumbSize * scale,
        .controlHeight = Style::controlHeight * scale,
        .width = Style::sliderDefaultWidth * scale,
    });
    const std::string suffix(field.valueSuffix);
    auto valueLabel = ui::label({
        .out = &valuePtr,
        .text = std::to_string(current) + suffix,
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .minWidth = 56.0F * scale,
        .textAlign = TextAlign::End,
    });
    slider->setOnValueChanged([valuePtr, suffix](double next) {
      if (valuePtr != nullptr) {
        valuePtr->setText(std::to_string(static_cast<int>(next)) + suffix);
      }
    });
    slider->setOnDragEnd([this, &field, sliderPtr, callbacks]() {
      if (sliderPtr == nullptr) {
        return;
      }
      stage(field, Value{static_cast<int>(sliderPtr->value())});
      if (callbacks.requestRebuild) {
        callbacks.requestRebuild();
      }
    });
    control = ui::row(
        {.align = FlexAlign::Center, .gap = Style::spaceSm * scale}, std::move(slider), std::move(valueLabel)
    );
  } else {
    control = ui::toggle({
        .checked = std::get<bool>(value),
        .scale = scale,
        .onChange =
            [this, &field, callbacks](bool checked) {
              stage(field, Value{checked});
              if (callbacks.requestRebuild) {
                callbacks.requestRebuild();
              }
            },
    });
  }

  if (field.subtitle.empty()) {
    auto label = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
    label->addChild(ui::label({
        .text = std::string(field.title),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurface),
    }));
    if (isOverridden) {
      label->addChild(makeOverrideBadge(scale));
      label->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
    }
    return ui::row(
        {.align = FlexAlign::Center,
         .justify = FlexJustify::SpaceBetween,
         .gap = Style::spaceXs * scale,
         .paddingV = 2.0F * scale,
         .fillWidth = true},
        std::move(label), std::move(control)
    );
  }

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  copy->addChild(ui::label({
      .text = std::string(field.subtitle),
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 4,
  }));

  auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  if (isOverridden) {
    actions->addChild(makeOverrideBadge(scale));
    actions->addChild(makeResetButton(path, armedResetKey == path, scale, callbacks));
  }
  actions->addChild(std::move(control));

  return ui::row(
      {.align = FlexAlign::Center,
       .justify = FlexJustify::SpaceBetween,
       .gap = Style::spaceSm * scale,
       .paddingV = 2.0F * scale,
       .fillWidth = true},
      std::move(copy), std::move(actions)
  );
}

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

  // Table rows visible under the current filter, per card.
  std::map<Card, std::vector<const Field*>> visibleRows;
  for (const auto& field : fields()) {
    if (matchesFilter(field, filter)) {
      visibleRows[field.card].push_back(&field);
    }
  }
  const auto rowsIn = [&](Card card) -> const std::vector<const Field*>& {
    static const std::vector<const Field*> kNone;
    const auto it = visibleRows.find(card);
    return it != visibleRows.end() ? it->second : kNone;
  };

  // The Title bar buttons card shows as a whole: its table rows plus the two
  // letter columns. It matches the section title, a button label, one of its
  // table rows, or the Overridden filter when any part differs from the default.
  const auto matchesButtonOption = [&]() {
    if (!searchActive || StringUtils::containsInsensitive(cardTitle(Card::Buttons), filter.searchQuery)) {
      return true;
    }
    for (const auto& option : buttonOptions()) {
      if (StringUtils::containsInsensitive(option.label, filter.searchQuery)) {
        return true;
      }
    }
    return false;
  };
  const bool lettersVisible = (!filter.showOverriddenOnly || overriddenButtonsOnLeft() || overriddenButtonsOnRight())
      && (sectionTitleMatches || matchesButtonOption());
  const bool buttonsVisible = lettersVisible || !rowsIn(Card::Buttons).empty();

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
  if (visible.empty() && rowsIn(Card::TitleBars).empty() && rowsIn(Card::Size).empty() && !buttonsVisible) {
    return nullptr;
  }

  auto section = ui::column({.align = FlexAlign::Stretch, .gap = Style::spaceSm * scale, .fillWidth = true});

  const auto makeCard = [scale]() {
    return ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .padding = Style::spaceMd * scale,
        .fill = colorSpecFromRole(ColorRole::SurfaceContainer, 0.5F),
        .radius = Style::scaledRadiusMd(scale),
        .fillWidth = true,
    });
  };
  const auto makeCardTitle = [scale](Card card) {
    return makeLabel(
        cardTitle(card), Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold
    );
  };

  if (const auto& rows = rowsIn(Card::TitleBars); !rows.empty()) {
    auto card = makeCard();
    for (const Field* field : rows) {
      card->addChild(buildFieldRow(*field, scale, armedResetKey, callbacks));
    }
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

  // Kinetic titlebar metrics. Written as an explicit value only when the user
  // moves a slider; otherwise the compositor keeps deriving the size from the
  // title font and grid unit.
  if (const auto& rows = rowsIn(Card::Size); !rows.empty()) {
    auto card = makeCard();
    card->addChild(makeCardTitle(Card::Size));
    for (const Field* field : rows) {
      card->addChild(buildFieldRow(*field, scale, armedResetKey, callbacks));
    }
    section->addChild(std::move(card));
  }

  if (buttonsVisible) {
    auto card = makeCard();
    card->addChild(makeCardTitle(Card::Buttons));
    // The card shows as a whole, so every table row in it renders.
    for (const auto& field : fields()) {
      if (field.card == Card::Buttons) {
        card->addChild(buildFieldRow(field, scale, armedResetKey, callbacks));
      }
    }

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
