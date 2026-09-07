#include "desktop_effects_page.h"

#include "core/log.h"
#include "dbus/session_bus.h"
#include "i18n/i18n.h"
#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/flex.h"
#include "ui/controls/glyph.h"
#include "ui/controls/label.h"
#include "ui/controls/separator.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

  constexpr Logger kLog("kinetic-settings");

  // Matches upstream's settings description wrapping budget.
  constexpr int kSettingDescriptionMaxLines = 5;

  // Effects hidden from the page: internal infrastructure (flagged in the
  // effect's own metadata) plus the handful the upstream desktop-effects KCM
  // also excludes (they are handled by other KCMs or are not user-facing).
  const std::set<std::string>& excludedEffects() {
    static const std::set<std::string> kExcluded{
        "login", "logout", "fadingpopups", "slidingpopups", "slidingnotifications"};
    return kExcluded;
  }

  // Reads a JSON boolean defensively. KDE effect metadata occasionally encodes
  // booleans as strings (e.g. sessionquit's "X-KWin-Internal": "true"), which
  // nlohmann would otherwise reject with a type error that crashes the page.
  // Non-object inputs and anything that is neither boolean nor a recognizable
  // "true"/"false" string fall back to `fallback`.
  [[nodiscard]] bool readBool(const nlohmann::json& obj, std::string_view key, bool fallback = false) {
    if (!obj.is_object()) {
      return fallback;
    }
    const auto it = obj.find(std::string(key));
    if (it == obj.end()) {
      return fallback;
    }
    if (it->is_boolean()) {
      return it->get<bool>();
    }
    if (it->is_string()) {
      const std::string value = it->get<std::string>();
      if (value == "true" || value == "1") {
        return true;
      }
      if (value == "false" || value == "0") {
        return false;
      }
    }
    return fallback;
  }

  // XDG data directories in priority order (user dirs first, /usr/share last),
  // mirroring theme_discovery's xdgDataDirs().
  [[nodiscard]] std::vector<std::string> dataDirs() {
    std::vector<std::string> dirs;
    const auto pushUnique = [&dirs](std::string dir) {
      if (dir.empty()) {
        return;
      }
      if (std::find(dirs.begin(), dirs.end(), dir) == dirs.end()) {
        dirs.push_back(std::move(dir));
      }
    };

    if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome != nullptr && dataHome[0] != '\0') {
      pushUnique(dataHome);
    } else if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      pushUnique(std::string(home) + "/.local/share");
    }

    if (const char* dataDirsEnv = std::getenv("XDG_DATA_DIRS"); dataDirsEnv != nullptr && dataDirsEnv[0] != '\0') {
      const std::size_t total = std::strlen(dataDirsEnv);
      std::size_t start = 0;
      while (start <= total) {
        const std::size_t end = std::string_view(dataDirsEnv).find(':', start);
        const std::size_t len = end == std::string_view::npos ? total - start : end - start;
        pushUnique(StringUtils::trim(std::string_view(dataDirsEnv + start, len)));
        if (end == std::string_view::npos) {
          break;
        }
        start = end + 1;
      }
    }

    pushUnique("/usr/share");
    return dirs;
  }

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

  // Status pill ("Override" / "Advanced"); same as the Workspaces/Window
  // Behavior pages.
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

  // The two-stage Reset action shown next to overridden effects.
  [[nodiscard]] std::unique_ptr<Button> makeResetButton(
      std::string path, bool pendingConfirmation, float scale, const DesktopEffectsPage::Callbacks& callbacks
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

DesktopEffectsPage::DesktopEffectsPage(SessionBus& bus) : m_dbus(bus) {}

// ---------------------------------------------------------------------------
// Discovery: parse the installed builtin- and scripted-effect metadata once
// and cache it.
// ---------------------------------------------------------------------------

std::vector<DesktopEffectsPage::Effect> DesktopEffectsPage::discoverEffects() {
  std::vector<Effect> effects;
  std::set<std::string> seen;

  // Parses one effect metadata.json and appends it to `effects`. Does nothing
  // when the file is not usable effect metadata or the effect was already
  // discovered (dedup by plugin id, first wins in XDG priority order).
  //
  // `requireEffectPackage` selects the scripted-effect branch: those packages
  // are KWin/Effect KPackages (KPackageStructure + X-Plasma-API), whereas
  // builtin effects are plain KPlugin JSON files under builtin-effects/. Both
  // share the KPlugin block, so parsing is shared.
  const auto consumeJson = [&effects, &seen](
      const fs::path& file, std::string fallbackName, bool requireEffectPackage) {
    std::ifstream in(file);
    if (!in.is_open()) {
      return;
    }

    nlohmann::json meta;
    try {
      in >> meta;
    } catch (const nlohmann::json::exception&) {
      return;
    }

    const auto& plugin = meta.value("KPlugin", nlohmann::json::object());
    if (plugin.empty()) {
      return;
    }

    // Only scripted desktop effects (KWin/Effect packages, JS or QML/declarative)
    // are listed here; plain KWin/Script packages are scripting infra and hidden.
    if (requireEffectPackage) {
      if (meta.value("KPackageStructure", std::string()) != "KWin/Effect") {
        return;
      }
      const std::string api = meta.value("X-Plasma-API", std::string());
      if (api != "javascript" && api != "declarativescript") {
        return;
      }
    }

    std::string name = plugin.value("Id", std::string());
    if (name.empty()) {
      name = std::move(fallbackName);
    }
    if (name.empty() || !seen.insert(name).second) {
      return;
    }

    // internal is flagged either via the builtin-effect block or the scripted
    // package's X-KWin-Internal key. Read defensively so a string-encoded
    // boolean (e.g. sessionquit's "X-KWin-Internal": "true") cannot throw.
    const auto& effectInfo = meta.value("org.kde.kwin.effect", nlohmann::json::object());
    const bool internal = readBool(effectInfo, "internal", false)
                       || readBool(meta, "X-KWin-Internal", false);

    effects.push_back(Effect{
        .name = name,
        .title = plugin.value("Name", name),
        .description = plugin.value("Description", std::string()),
        .category = plugin.value("Category", std::string("Miscellaneous")),
        .enabledByDefault = readBool(plugin, "EnabledByDefault", false),
        .internal = internal,
    });
  };

  for (const auto& dir : dataDirs()) {
    // Built-in (compiled) effects: kwin-wayland/builtin-effects/<name>.json
    const fs::path builtinDir = fs::path(dir) / "kwin-wayland" / "builtin-effects";
    std::error_code ec;
    fs::directory_iterator it(builtinDir, ec);
    const fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
      if (!it->is_regular_file() || it->path().extension() != ".json") {
        continue;
      }
      consumeJson(it->path(), it->path().stem().string(), /*requireEffectPackage=*/false);
    }

    // Scripted effects bundled with KineticWE: kwin-wayland/effects/<name>/metadata.json
    // (this includes Geometry Change). The sibling kwin/effects root is
    // deliberately NOT scanned: that is the KWin/Effect KPackage install path
    // used by third-party "Get New Effects" (KNewStuff) downloads, which are
    // out of scope for this page.
    const fs::path pkgRoot = fs::path(dir) / "kwin-wayland/effects";
    std::error_code ec2;
    fs::directory_iterator pkgIt(pkgRoot, ec2);
    const fs::directory_iterator pkgEnd;
    for (; !ec2 && pkgIt != pkgEnd; pkgIt.increment(ec2)) {
      if (!pkgIt->is_directory()) {
        continue;
      }
      const fs::path metaFile = pkgIt->path() / "metadata.json";
      if (!fs::is_regular_file(metaFile)) {
        continue;
      }
      consumeJson(metaFile, pkgIt->path().filename().string(), /*requireEffectPackage=*/true);
    }
  }

  std::sort(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) {
    if (a.category != b.category) {
      return a.category < b.category;
    }
    return a.title < b.title;
  });

  return effects;
}

const std::vector<DesktopEffectsPage::Effect>& DesktopEffectsPage::effects() {
  static const std::vector<Effect> list = discoverEffects();
  return list;
}

bool DesktopEffectsPage::isVisible(const Effect& effect) {
  return !effect.internal && !excludedEffects().contains(effect.name);
}

std::vector<const DesktopEffectsPage::Effect*> DesktopEffectsPage::visibleEffects() {
  std::vector<const Effect*> visible;
  for (const auto& effect : effects()) {
    if (isVisible(effect)) {
      visible.push_back(&effect);
    }
  }
  return visible;
}

std::string DesktopEffectsPage::keyPath(const Effect& effect) {
  return effect.name;
}

const DesktopEffectsPage::Effect* DesktopEffectsPage::findEffect(std::string_view keyPath) {
  for (const auto& effect : effects()) {
    if (keyPath == effect.name) {
      return &effect;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// State: file-backed (config) vs effective (staged) enabled state.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::ensureLoaded() {
  if (!m_loaded) {
    m_config.load();
    m_loaded = true;
  }
}

bool DesktopEffectsPage::fileBackedEnabled(const Effect& effect) const {
  return m_config.readBool("Plugins", effect.name + "Enabled", effect.enabledByDefault);
}

bool DesktopEffectsPage::effectiveEnabled(const Effect& effect) const {
  const auto staged = m_pending.find(effect.name);
  if (staged != m_pending.end()) {
    return staged->second;
  }
  return fileBackedEnabled(effect);
}

bool DesktopEffectsPage::overridden(const Effect& effect) const {
  return effectiveEnabled(effect) != effect.enabledByDefault;
}

bool DesktopEffectsPage::matchesFilter(const Effect& effect, const Filter& filter) const {
  if (filter.showOverriddenOnly && !overridden(effect)) {
    return false;
  }
  if (!filter.searchQuery.empty()) {
    const auto matches = [&filter](std::string_view text) {
      return StringUtils::containsInsensitive(text, filter.searchQuery);
    };
    if (!matches(effect.title) && !matches(effect.description) && !matches(effect.category)) {
      return false;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Persistence: staged edits + Apply. Values set back to the default remove the
// key once applied, mirroring the other pages' override semantics.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::stage(const Effect& effect, bool value) {
  ensureLoaded();
  // Editing back to the on-disk value un-stages (the row stops being dirty).
  if (value == fileBackedEnabled(effect)) {
    m_pending.erase(effect.name);
  } else {
    m_pending[effect.name] = value;
  }
}

void DesktopEffectsPage::stageAndRebuild(const Effect& effect, bool value, const Callbacks& callbacks) {
  stage(effect, value);
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> DesktopEffectsPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(effects().size());
  for (const Effect* effect : visibleEffects()) {
    paths.push_back(keyPath(*effect));
  }
  return paths;
}

void DesktopEffectsPage::resetKeyPath(std::string_view keyPath) {
  if (const Effect* effect = findEffect(keyPath)) {
    stage(*effect, effect->enabledByDefault);
  }
}

void DesktopEffectsPage::resetAll() {
  for (const Effect* effect : visibleEffects()) {
    stage(*effect, effect->enabledByDefault);
  }
}

bool DesktopEffectsPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  for (const auto& [name, enabled] : m_pending) {
    const Effect* effect = findEffect(name);
    if (effect == nullptr) {
      continue;
    }
    if (enabled == effect->enabledByDefault) {
      // Back to the compiled-in default: drop the key so the file stays tidy.
      m_config.removeKey("Plugins", name + "Enabled");
    } else {
      m_config.setBool("Plugins", name + "Enabled", enabled);
    }
  }

  if (!m_config.save()) {
    kLog.warn("kinetic-settings: failed to write {}", KineticweKwe::defaultPath().string());
    return false;
  }

  // Apply each changed effect live via org.kde.kwin.Effects so the user does
  // not have to restart the session (or wait on the config watcher).
  for (const auto& [name, enabled] : m_pending) {
    if (enabled) {
      m_dbus.loadEffect(name);
    } else {
      m_dbus.unloadEffect(name);
    }
  }

  m_pending.clear();
  return true;
}

// ---------------------------------------------------------------------------
// Controls + layout.
// ---------------------------------------------------------------------------

void DesktopEffectsPage::addEffectRow(
    Flex& parent, const Effect& effect, float scale, std::string_view armedResetKey, const Callbacks& callbacks
) {
  const std::string path = keyPath(effect);
  const bool isOverridden = overridden(effect);

  auto titleRow = ui::row({
      .align = FlexAlign::Center,
      .gap = Style::spaceSm * scale,
      .fillWidth = true,
  });
  titleRow->addChild(
      makeLabel(effect.title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold)
  );
  titleRow->addChild(ui::spacer());

  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(std::move(titleRow));
  if (!effect.description.empty()) {
    copy->addChild(ui::label({
        .text = effect.description,
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
  actions->addChild(ui::toggle({
      .checked = effectiveEnabled(effect),
      .scale = scale,
      .onChange = [this, effect, callbacks](bool value) {
        stageAndRebuild(effect, value, callbacks);
      },
  }));

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

std::unique_ptr<Node> DesktopEffectsPage::build(
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
          .glyph = "sparkles",
          .glyphSize = Style::fontSizeHeader * scale,
          .color = colorSpecFromRole(ColorRole::Primary),
      }),
      makeLabel("Desktop Effects", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold)
  ));

  bool anyRows = false;
  bool firstGroup = true;
  std::string_view lastCategory;
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

  for (const Effect* effect : visibleEffects()) {
    if (!matchesFilter(*effect, filter)) {
      continue;
    }
    if (effect->category != lastCategory) {
      lastCategory = effect->category;
      addGroupHeader(lastCategory);
    }
    addEffectRow(*root, *effect, scale, armedResetKey, callbacks);
    anyRows = true;
  }

  if (!anyRows) {
    root->addChild(makeLabel(
        "No effects match the current filters.", Style::fontSizeBody * scale,
        colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
  }

  return root;
}
