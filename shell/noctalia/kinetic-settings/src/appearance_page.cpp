#include "appearance_page.h"

#include "core/log.h"
#include "i18n/i18n.h"
#include "theme/color.h"
#include "theme/fixed_palette.h"
#include "theme/kde_color_scheme.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/separator.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

  constexpr Logger kLog("kinetic-settings");

  constexpr int kSettingDescriptionMaxLines = 5;
  constexpr float kDefaultControlWidth = 190.0F;

  // The Appearance page persists to its own portable appearance.kwe (not the
  // compositor's kineticwe.kwe), so a theme choice travels with the rest of
  // ~/.config/kineticwe when copied to a new machine.
  std::filesystem::path appearanceKwePath() {
    const auto dir = KineticweKwe::defaultPath().parent_path();
    return dir / "appearance.kwe";
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

  [[nodiscard]] std::string joinKeyPath(std::string_view group, std::string_view key) {
    return std::string(group) + '.' + std::string(key);
  }

  // The user's real config home ($XDG_CONFIG_HOME, else ~/.config). This is
  // where qt5ct/qt6ct (and kdeglobals) live — NOT the compositor-redirected
  // KWE_CONFIG_HOME. The shell process (which embeds kinetic-settings) keeps the
  // real XDG_CONFIG_HOME, so this resolves to the shared config location.
  [[nodiscard]] std::filesystem::path realConfigHome() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
      return std::filesystem::path(xdg);
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
      return std::filesystem::path(home) / ".config";
    }
    return std::filesystem::path(".");
  }

  // Builds a qt5ct/qt6ct [ColorScheme] file (the native QPalette format the
  // tools read from their colors/ directory), generated from a Noctalia
  // TokenMap. Mirrors assets/templates/qt/qtct.conf.
  [[nodiscard]] std::string buildQtctColorScheme(const noctalia::theme::TokenMap& tokens) {
    const auto hexOf = [&tokens](std::string_view key, std::string_view fallback) {
      auto it = tokens.find(std::string(key));
      return it != tokens.end() ? noctalia::theme::Color::fromArgb(it->second).toHex()
                                : noctalia::theme::Color::fromHex(fallback).toHex();
    };
    const auto line = [&](std::string_view onBg, std::string_view onSurface) {
      return hexOf("on_background", onBg) + ", " + hexOf("surface", "#000000") + ", #ffffff, #cacaca, #9f9f9f, #b8b8b8, "
             + hexOf("on_background", onBg) + ", #ffffff, " + hexOf("on_surface", onSurface) + ", "
             + hexOf("background", onBg) + ", " + hexOf("background", onBg) + ", " + hexOf("shadow", "#000000") + ", "
             + hexOf("primary_container", "#000000") + ", " + hexOf("on_primary_container", "#ffffff") + ", "
             + hexOf("secondary", "#ffffff") + ", " + hexOf("primary", "#ffffff") + ", "
             + hexOf("surface_variant", "#000000") + ", " + hexOf("surface", "#000000") + ", "
             + hexOf("surface_variant", "#000000") + ", " + hexOf("on_surface", onSurface) + ", "
             + hexOf("on_surface", onSurface) + ", " + hexOf("primary", "#ffffff");
    };
    const std::string active = line("#ffffff", "#ffffff");
    std::string out;
    out.reserve(1200);
    out += "[ColorScheme]\n";
    out += "# windowText,button,light,midlight,dark,mid,text,brightText,buttonText,base,window,shadow,"
           "highlight,highlightedText,link,linkVisited,alternateBase,reserved,toolTipBase,toolTipText,"
           "placeholderText,accent\n";
    out += "active_colors=" + active + "\n";
    out += "disabled_colors=" + active + "\n";
    out += "inactive_colors=" + active + "\n";
    return out;
  }

  // Minimal qt5ct/qt6ct config pointing at the Noctalia color scheme. Only the
  // [Appearance] section is emitted; qt5ct/qt6ct fill the rest with defaults on
  // first run and preserve any user edits to the other sections (this function
  // is only called when the config file does not already exist).
  [[nodiscard]] std::string buildQtctConfig(const std::filesystem::path& colorSchemePath) {
    std::string out;
    out.reserve(256);
    out += "[Appearance]\n";
    out += "color_scheme_path=" + colorSchemePath.string() + "\n";
    out += "custom_palette=true\n";
    out += "icon_theme=Papirus\n";
    out += "standard_dialogs=gtk3\n";
    out += "style=Breeze\n";
    return out;
  }

  // Writes `content` to `path`, creating parent directories. Returns false on
  // failure (sets *error when provided).
  [[nodiscard]] bool writeFileIfAbsent(
      const std::filesystem::path& path, const std::string& content, std::string* error
  ) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
      return true;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to create directory for " + path.string();
      }
      return false;
    }
    std::ofstream out(path);
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write " + path.string();
      }
      return false;
    }
    out << content;
    return static_cast<bool>(out);
  }

  // Breeze-inspired seed palettes. The exact derived colors are produced by
  // Noctalia's palette generator (expandFixedPaletteMode), which guarantees the
  // full token set with WCAG contrast enforcement.
  [[nodiscard]] ::Palette breezeDarkSeeds() {
    return ::Palette{
        .primary = hex("#3daee9"),
        .onPrimary = hex("#232629"),
        .secondary = hex("#27ae60"),
        .onSecondary = hex("#232629"),
        .tertiary = hex("#2980b9"),
        .onTertiary = hex("#232629"),
        .error = hex("#da4453"),
        .onError = hex("#ffffff"),
        .surface = hex("#232629"),
        .onSurface = hex("#fcfcfc"),
        .surfaceVariant = hex("#31363b"),
        .onSurfaceVariant = hex("#bdc3c7"),
        .outline = hex("#4d4d4d"),
        .shadow = hex("#000000"),
        .hover = hex("#3daee9"),
        .onHover = hex("#232629"),
    };
  }

  [[nodiscard]] ::Palette breezeLightSeeds() {
    return ::Palette{
        .primary = hex("#3daee9"),
        .onPrimary = hex("#ffffff"),
        .secondary = hex("#27ae60"),
        .onSecondary = hex("#ffffff"),
        .tertiary = hex("#2980b9"),
        .onTertiary = hex("#ffffff"),
        .error = hex("#da4453"),
        .onError = hex("#ffffff"),
        .surface = hex("#fcfcfc"),
        .onSurface = hex("#232629"),
        .surfaceVariant = hex("#eff0f1"),
        .onSurfaceVariant = hex("#4d4d4d"),
        .outline = hex("#b6b6b6"),
        .shadow = hex("#000000"),
        .hover = hex("#3daee9"),
        .onHover = hex("#ffffff"),
    };
  }

} // namespace

// ---------------------------------------------------------------------------
// Schema: field descriptor table + option catalogs.
// ---------------------------------------------------------------------------

const std::vector<std::string>& AppearancePage::themeValues() {
  static const std::vector<std::string> values{"BreezeDark", "BreezeLight"};
  return values;
}

const std::vector<std::string>& AppearancePage::themeLabels() {
  static const std::vector<std::string> labels{"Breeze Dark", "Breeze Light"};
  return labels;
}

const std::vector<AppearancePage::Field>& AppearancePage::fields() {
  static const std::vector<Field> table{
      Field{
          .group = "Appearance",
          .key = "GlobalTheme",
          .control = ControlKind::Select,
          .title = "Global theme",
          .subtitle = "Color scheme applied to Qt and KDE applications. The "
                      "scheme is generated from a Breeze seed palette and merged "
                      "into kdeglobals; apps pick it up on their next launch.",
          .pageGroup = "Global Theme",
          .optionValues = &themeValues(),
          .optionLabels = &themeLabels(),
          .fallback = Value{std::string("BreezeDark")},
      },
  };
  return table;
}

AppearancePage::Value AppearancePage::defaultValue(const Field& field) {
  return field.fallback;
}

std::string AppearancePage::keyPath(const Field& field) {
  return joinKeyPath(field.group, field.key);
}

const AppearancePage::Field* AppearancePage::findField(std::string_view path) {
  for (const auto& field : fields()) {
    if (path == keyPath(field)) {
      return &field;
    }
  }
  return nullptr;
}

bool AppearancePage::equalsDefault(const Field& field, const Value& value) {
  return valuesEqual(field, value, defaultValue(field));
}

bool AppearancePage::valuesEqual(const Field& field, const Value& a, const Value& b) {
  (void)field;
  return a.index() == b.index() && a == b;
}

void AppearancePage::ensureLoaded() {
  if (!m_loaded) {
    m_config.setPath(appearanceKwePath());
    m_config.load();
    m_loaded = true;
    kLog.info("appearance ensureLoaded: path={}", m_config.path().string());
  }
}

AppearancePage::Value AppearancePage::fileBackedValue(const Field& field) const {
  const Value fallback = defaultValue(field);
  return std::visit(
      [this, &field](const auto& fallbackValue) -> Value {
        using T = std::decay_t<decltype(fallbackValue)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return m_config.readString(field.group, field.key).value_or(fallbackValue);
        } else if constexpr (std::is_same_v<T, bool>) {
          return m_config.readBool(field.group, field.key, fallbackValue);
        } else if constexpr (std::is_same_v<T, int>) {
          return m_config.readInt(field.group, field.key, fallbackValue);
        } else if constexpr (std::is_same_v<T, double>) {
          return m_config.readDouble(field.group, field.key, fallbackValue);
        } else {
          return m_config.readStringList(field.group, field.key);
        }
      },
      fallback
  );
}

AppearancePage::Value AppearancePage::effectiveValue(const Field& field) const {
  const auto staged = m_pending.find(keyPath(field));
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedValue(field);
}

bool AppearancePage::overridden(const Field& field) const {
  return !equalsDefault(field, effectiveValue(field));
}

bool AppearancePage::matchesFilter(const Field& field, const Filter& filter) const {
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
// Persistence: staged edits + Apply.
// ---------------------------------------------------------------------------

void AppearancePage::writeValue(const Field& field, const Value& value) {
  // The Appearance choice is always written explicitly (even when it equals the
  // default). Unlike the Tiling/Workspaces override semantics, the theme is a
  // user-facing selection we want to persist in appearance.kwe so it is
  // portable and never collapses to an empty file.
  std::visit(
      [this, &field](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
          m_config.setBool(field.group, field.key, v);
        } else if constexpr (std::is_same_v<T, int>) {
          m_config.setInt(field.group, field.key, v);
        } else if constexpr (std::is_same_v<T, double>) {
          m_config.setDouble(field.group, field.key, v);
        } else if constexpr (std::is_same_v<T, std::string>) {
          m_config.setString(field.group, field.key, v);
        } else {
          m_config.setStringList(field.group, field.key, v);
        }
      },
      value
  );
}

void AppearancePage::stage(const Field& field, const Value& value) {
  ensureLoaded();
  const std::string path = keyPath(field);
  const auto fileValue = fileBackedValue(field);
  kLog.info(
      "appearance stage: field={} value={} equalToFile={}",
      path,
      std::get<std::string>(value),
      valuesEqual(field, value, fileValue) ? "yes" : "no"
  );
  if (valuesEqual(field, value, fileValue)) {
    m_pending.erase(path);
  } else {
    m_pending[path] = value;
  }
}

void AppearancePage::stageAndRebuild(const Field& field, const Value& value, const Callbacks& callbacks) {
  stage(field, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

bool AppearancePage::applyAll() {
  kLog.info("appearance applyAll: pending={}", m_pending.size());
  if (m_pending.empty()) {
    return true;
  }
  ensureLoaded();
  kLog.info("appearance applyAll: config path={}", m_config.path().string());
  for (const auto& [path, value] : m_pending) {
    if (const Field* field = findField(path)) {
      writeValue(*field, value);
      kLog.info(
          "appearance applyAll: wrote field group={} key={}", field->group, field->key
      );
    }
  }
  if (const auto v = m_config.readString("Appearance", "GlobalTheme"); v.has_value()) {
    kLog.info("appearance applyAll: in-memory GlobalTheme after write={}", *v);
  } else {
    kLog.info("appearance applyAll: in-memory GlobalTheme after write=ABSENT");
  }
  if (!m_config.save()) {
    m_errorNotice = "Failed to write appearance.kwe. Your changes were kept; try Apply again.";
    kLog.warn("kinetic-settings: failed to write {}", m_config.path().string());
    return false;
  }
  {
    std::error_code ec;
    const auto size = std::filesystem::file_size(m_config.path(), ec);
    kLog.info(
        "appearance applyAll: saved, file_size={} (error={})", ec ? -1 : static_cast<long long>(size),
        ec ? ec.message() : std::string("none")
    );
  }

  // Regenerate kdeglobals from the effective Global Theme selection every time
  // Apply runs. Using the effective value (not just the persisted key) means
  // the noctalia-generated scheme is applied even when the theme equals the
  // default (Breeze Dark), e.g. on a fresh install.
  if (const Field* themeField = findField("Appearance.GlobalTheme"); themeField != nullptr) {
    const auto themeValue = effectiveValue(*themeField);
    std::string error;
    if (!applyTheme(std::get<std::string>(themeValue), &error)) {
      m_errorNotice = "Failed to apply the theme: " + error;
      kLog.warn("kinetic-settings: failed to apply theme: {}", error);
      return false;
    }
  }

  m_pending.clear();
  m_errorNotice.clear();
  return true;
}

bool AppearancePage::applyTheme(std::string_view theme, std::string* error) {
  using namespace noctalia::theme;
  const bool dark = theme == "BreezeDark";
  const ::Palette seeds = dark ? breezeDarkSeeds() : breezeLightSeeds();
  const TokenMap tokens = expandFixedPaletteMode(seeds, dark);
  const std::filesystem::path configHome = realConfigHome();
  const std::string schemeName = dark ? "BreezeDark" : "BreezeLight";

  // 1. Regenerate the Noctalia color scheme for qt5ct and qt6ct. These files
  // are always rewritten (pure outputs of the selected theme), so qt5ct/qt6ct
  // pick up the new colors on the next app launch.
  const std::string colorScheme = buildQtctColorScheme(tokens);
  for (const std::string_view tool : {"qt5ct", "qt6ct"}) {
    const std::filesystem::path colorsPath = configHome / std::string(tool) / "colors" / "noctalia.conf";
    std::error_code ec;
    std::filesystem::create_directories(colorsPath.parent_path(), ec);
    if (ec) {
      if (error != nullptr) {
        *error = "failed to create directory for " + colorsPath.string();
      }
      return false;
    }
    std::ofstream out(colorsPath);
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write " + colorsPath.string();
      }
      return false;
    }
    out << colorScheme;
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write " + colorsPath.string();
      }
      return false;
    }
  }

  // 2. Ensure qt5ct/qt6ct configs point at the Noctalia color scheme. Only
  // written when absent so a user's existing config (fonts, interface tweaks)
  // is never clobbered.
  for (const std::string_view tool : {"qt5ct", "qt6ct"}) {
    const std::filesystem::path cfgPath = configHome / std::string(tool) / (std::string(tool) + ".conf");
    const std::filesystem::path colorsPath = configHome / std::string(tool) / "colors" / "noctalia.conf";
    if (!writeFileIfAbsent(cfgPath, buildQtctConfig(colorsPath), error)) {
      return false;
    }
  }

  // 3. Also merge into kdeglobals so KDE Framework apps (Dolphin, Kate,
  // Konsole, ...) pick up the theme. Kept for widest compatibility; easy to
  // drop later if it causes issues.
  const auto csv = [&tokens](std::string_view key, std::string_view fallback) {
    auto it = tokens.find(std::string(key));
    const noctalia::theme::Color color =
        it != tokens.end() ? noctalia::theme::Color::fromArgb(it->second)
                           : noctalia::theme::Color::fromHex(fallback);
    return std::to_string(color.r) + ',' + std::to_string(color.g) + ',' + std::to_string(color.b);
  };

  std::string colors;
  colors.reserve(4096);
  colors += "[KDE]\ncontrast=4\n\n";
  colors += "[General]\nColorScheme=" + schemeName + "\nName=" + schemeName + "\n\n";
  colors += "[ColorEffects:Disabled]\n";
  colors += "Color=" + csv("surface_container_lowest", "#000000") + "\n";
  colors += "ColorAmount=0\nColorEffect=0\nContrastAmount=0.65\nContrastEffect=1\n"
            "IntensityAmount=0.1\nIntensityEffect=2\n\n";
  colors += "[ColorEffects:Inactive]\n";
  colors += "ChangeSelectionColor=true\nColor=" + csv("surface_variant", "#000000") + "\n";
  colors += "ColorAmount=0.025\nColorEffect=2\nContrastAmount=0.1\nContrastEffect=2\n"
            "Enable=false\nIntensityAmount=0\nIntensityEffect=0\n\n";

  const auto section = [&](std::string_view name, std::string_view bg, std::string_view bgAlt,
                           std::string_view focus, std::string_view hover, std::string_view fgActive,
                           std::string_view fgInactive, std::string_view fgLink, std::string_view fgNegative,
                           std::string_view fgNeutral, std::string_view fgNormal, std::string_view fgPositive,
                           std::string_view fgVisited) {
    std::string out = std::string("[Colors:") + std::string(name) + "]\n";
    out += "BackgroundAlternate=" + csv(bgAlt, "#000000") + "\n";
    out += "BackgroundNormal=" + csv(bg, "#000000") + "\n";
    out += "DecorationFocus=" + csv(focus, "#ffffff") + "\n";
    out += "DecorationHover=" + csv(hover, "#ffffff") + "\n";
    out += "ForegroundActive=" + csv(fgActive, "#ffffff") + "\n";
    out += "ForegroundInactive=" + csv(fgInactive, "#ffffff") + "\n";
    out += "ForegroundLink=" + csv(fgLink, "#ffffff") + "\n";
    out += "ForegroundNegative=" + csv(fgNegative, "#ffffff") + "\n";
    out += "ForegroundNeutral=" + csv(fgNeutral, "#ffffff") + "\n";
    out += "ForegroundNormal=" + csv(fgNormal, "#ffffff") + "\n";
    out += "ForegroundPositive=" + csv(fgPositive, "#ffffff") + "\n";
    out += "ForegroundVisited=" + csv(fgVisited, "#ffffff") + "\n\n";
    return out;
  };

  const std::string onSurface = "on_surface";
  const std::string onSurfaceVariant = "on_surface_variant";
  const std::string primary = "primary";
  const std::string secondary = "secondary";
  const std::string tertiary = "tertiary";
  const std::string errorToken = "error";
  const std::string onBackground = "on_background";
  const std::string onPrimaryContainer = "on_primary_container";
  const std::string onSecondaryContainer = "on_secondary_container";
  const std::string primaryContainer = "primary_container";
  const std::string surface = "surface";
  const std::string surfaceVariant = "surface_variant";
  const std::string surfaceContainer = "surface_container";
  const std::string surfaceContainerLow = "surface_container_low";
  const std::string surfaceContainerHigh = "surface_container_high";
  const std::string errorContainer = "error_container";
  const std::string tertiaryContainer = "tertiary_container";

  colors += section(
      "Button", surfaceContainerHigh, surfaceContainerLow, primary, primary, primary, onSurfaceVariant, secondary,
      errorToken, tertiary, onSurface, tertiary, onSecondaryContainer
  );
  colors += section(
      "Complementary", surface, surfaceContainerLow, primary, primary, primary, onSurfaceVariant, secondary, errorToken,
      tertiary, onPrimaryContainer, tertiary, onSecondaryContainer
  );
  colors += section(
      "Header", surfaceContainer, surface, primary, primary, primary, onSurfaceVariant, secondary, errorToken, tertiary,
      onSurface, tertiary, onSecondaryContainer
  );
  colors += section(
      "Selection", primary, surfaceContainerLow, primary, primary, onSurface, onSurfaceVariant, secondary,
      errorContainer, tertiaryContainer, onSurface, tertiaryContainer, onSecondaryContainer
  );
  colors += section(
      "Tooltip", surfaceContainer, surface, primary, primary, primary, onSurfaceVariant, secondary, errorToken, tertiary,
      onBackground, tertiary, onSecondaryContainer
  );
  colors += section(
      "View", surface, surfaceContainer, onPrimaryContainer, onSurface, primary, onSurfaceVariant, secondary, errorToken,
      tertiary, onBackground, tertiary, onSecondaryContainer
  );
  colors += section(
      "Window", surfaceContainer, primaryContainer, primary, primary, primary, onSurfaceVariant, secondary, errorToken,
      tertiary, onBackground, tertiary, onSecondaryContainer
  );

  colors += "[WM]\n";
  colors += "activeBackground=" + csv(primaryContainer, "#000000") + "\n";
  colors += "activeBlend=" + csv(onPrimaryContainer, "#ffffff") + "\n";
  colors += "activeForeground=" + csv(onPrimaryContainer, "#ffffff") + "\n";
  colors += "inactiveBackground=" + csv(surface, "#000000") + "\n";
  colors += "inactiveBlend=" + csv(onSurfaceVariant, "#ffffff") + "\n";
  colors += "inactiveForeground=" + csv(onSurfaceVariant, "#ffffff") + "\n";

  const std::filesystem::path schemePath = std::filesystem::temp_directory_path() / "kineticwe-breeze-scheme.colors";
  {
    std::ofstream out(schemePath);
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write temporary color scheme";
      }
      return false;
    }
    out << colors;
    if (!out) {
      if (error != nullptr) {
        *error = "failed to write temporary color scheme";
      }
      return false;
    }
  }

  const std::filesystem::path kdeGlobals = configHome / "kdeglobals";
  std::string mergeError;
  if (!mergeKdeColorScheme(schemePath, kdeGlobals, &mergeError)) {
    if (error != nullptr) {
      *error = std::move(mergeError);
    }
    return false;
  }
  return true;
}

std::vector<std::string> AppearancePage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(fields().size());
  for (const auto& field : fields()) {
    paths.push_back(keyPath(field));
  }
  return paths;
}

void AppearancePage::resetKeyPath(std::string_view path) {
  if (const Field* field = findField(path)) {
    stage(*field, defaultValue(*field));
  }
}

void AppearancePage::resetAll() {
  for (const auto& field : fields()) {
    stage(field, defaultValue(field));
  }
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

} // namespace

std::unique_ptr<Node>
AppearancePage::buildControl(const Field& field, float scale, const Callbacks& callbacks) {
  const auto& values = *field.optionValues;
  const auto& labels = *field.optionLabels;
  const auto selected = std::ranges::find(values, std::get<std::string>(effectiveValue(field)));
  const bool known = selected != values.end();
  const std::optional<std::size_t> selectedIndex =
      known ? std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())} : std::nullopt;
  return ui::select({
      .options = labels,
      .selectedIndex = selectedIndex,
      .clearSelection = !known,
      .placeholder = known ? std::optional<std::string>{std::nullopt}
                           : std::optional<std::string>{"Unknown value: "
                                                        + std::get<std::string>(effectiveValue(field))},
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .width = kDefaultControlWidth * scale,
      .height = Style::controlHeight * scale,
      .onSelectionChanged = [this, field, callbacks, values](std::size_t index, std::string_view /*label*/) {
        if (index < values.size()) {
          kLog.info("appearance dropdown: selected index={} value={}", index, values[index]);
          stageAndRebuild(field, Value{values[index]}, callbacks);
        } else {
          kLog.warn("appearance dropdown: out-of-range index={}", index);
        }
      },
  });
}

void AppearancePage::addSettingRow(
    Flex& parent, const Field& field, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(field);
  const bool isOverridden = overridden(field);

  auto titleRow = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  titleRow->addChild(makeLabel(
      field.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
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

std::unique_ptr<Node> AppearancePage::build(
    float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks
) {
  ensureLoaded();

  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  root->addChild(ui::row(
      {.align = FlexAlign::Center, .gap = Style::spaceSm * scale},
      ui::glyph({
          .glyph = "palette",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Global Theme", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

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

  if (!anyRows) {
    root->addChild(makeLabel(
        "No settings match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
