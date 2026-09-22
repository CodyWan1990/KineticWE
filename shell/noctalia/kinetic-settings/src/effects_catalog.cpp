#include "effects_catalog.h"

#include "util/string_utils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace effects_catalog {

namespace {

  // Effects hidden from the pages: internal infrastructure (flagged in the
  // effect's own metadata) plus the handful the upstream desktop-effects KCM
  // also excludes (they are handled by other KCMs or are not user-facing).
  const std::set<std::string>& excludedEffects() {
    static const std::set<std::string> kExcluded{
        "login", "logout", "fadingpopups", "slidingpopups", "slidingnotifications",
        // KineticWE ships the full-screen ("Stretch") animation disabled and
        // non-configurable, so it is not exposed on any page.
        "fullscreen",
        // Geometry Change was reimplemented as a native builtin effect
        // ("geometrychange", Category=Animations, listed on the Animations
        // page). The legacy scripted implementation it replaced
        // ("kwin4_effect_geometry_change", Category=Appearance) can still be
        // discovered in kwin-wayland/effects on installs that carried it over;
        // hide it so the same feature is not presented twice with two
        // independent toggles (and so it is not animated twice).
        "kwin4_effect_geometry_change"};
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

  // The order in which effect categories are presented on the Desktop Effects
  // page. "Animations" is a KineticWE grouping for window move/resize
  // animations (e.g. Geometry Change) and is shown right after "Appearance".
  // Categories absent from this list sort last (preserving their alphabetical
  // ordering).
  const std::vector<std::string>& sectionOrder() {
    static const std::vector<std::string> kOrder{
        "Accessibility",
        "Appearance",
        "Animations",
        "Focus",
        "Show Desktop Animation",
        "Tools",
        "Virtual Desktop Switching Animation",
        "Window Management",
        "Window Open/Close Animation",
    };
    return kOrder;
  }

  [[nodiscard]] int sectionRank(const std::string& category) {
    const auto& order = sectionOrder();
    const auto it = std::find(order.begin(), order.end(), category);
    if (it == order.end()) {
      return static_cast<int>(order.size()); // unknown categories sort last
    }
    return static_cast<int>(std::distance(order.begin(), it));
  }

} // namespace

// ---------------------------------------------------------------------------
// Discovery: parse the installed builtin- and scripted-effect metadata once
// and cache it.
// ---------------------------------------------------------------------------

static std::vector<Effect> discoverEffects() {
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

    // Effects can belong to a mutually-exclusive group (only one may be enabled
    // at a time), e.g. the open/close or desktop-switching animations. KWin
    // marks these with X-KWin-Exclusive-Category (scripted) or the
    // org.kde.kwin.effect.exclusiveGroup key (builtin).
    std::string exclusiveGroup = meta.value("X-KWin-Exclusive-Category", std::string());
    if (exclusiveGroup.empty()) {
      exclusiveGroup = effectInfo.value("exclusiveGroup", std::string());
    }

    effects.push_back(Effect{
        .name = name,
        .title = plugin.value("Name", name),
        .description = plugin.value("Description", std::string()),
        .category = plugin.value("Category", std::string("Miscellaneous")),
        .enabledByDefault = readBool(plugin, "EnabledByDefault", false),
        .internal = internal,
        .exclusiveGroup = exclusiveGroup,
    });
  };

  // Prefer KineticWE's namespace, but keep compatibility with a Noctalia-KWE
  // process running against a standard KDE/KWin data directory.
  const std::vector<std::string_view> effectRoots{"kineticwe-wayland", "kwin-wayland"};
  for (const auto& dir : dataDirs()) {
    for (const std::string_view effectRoot : effectRoots) {
      // Built-in (compiled) effects: <root>/builtin-effects/<name>.json
      const fs::path builtinDir = fs::path(dir) / effectRoot / "builtin-effects";
      std::error_code ec;
      fs::directory_iterator it(builtinDir, ec);
      const fs::directory_iterator end;
      for (; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file() || it->path().extension() != ".json") {
          continue;
        }
        consumeJson(it->path(), it->path().stem().string(), /*requireEffectPackage=*/false);
      }

      // Scripted effects bundled with KineticWE: <root>/effects/<name>/metadata.json
      // (this includes Geometry Change). The sibling kwin/effects root is
      // deliberately NOT scanned: that is the KWin/Effect KPackage install path
      // used by third-party "Get New Effects" (KNewStuff) downloads, which are
      // out of scope for these pages.
      const fs::path pkgRoot = fs::path(dir) / effectRoot / "effects";
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
  }

  std::sort(effects.begin(), effects.end(), [](const Effect& a, const Effect& b) {
    const int rankA = sectionRank(a.category);
    const int rankB = sectionRank(b.category);
    if (rankA != rankB) {
      return rankA < rankB;
    }
    if (a.title != b.title) {
      return a.title < b.title;
    }
    return a.name < b.name;
  });

  return effects;
}

const std::vector<Effect>& effects() {
  static const std::vector<Effect> list = discoverEffects();
  return list;
}

bool isVisible(const Effect& effect) {
  return !effect.internal && !excludedEffects().contains(effect.name);
}

std::vector<const Effect*> visibleEffects() {
  std::vector<const Effect*> visible;
  for (const auto& effect : effects()) {
    if (isVisible(effect)) {
      visible.push_back(&effect);
    }
  }
  return visible;
}

std::string keyPath(const Effect& effect) {
  return effect.name;
}

const Effect* findEffect(std::string_view keyPath) {
  for (const auto& effect : effects()) {
    if (keyPath == effect.name) {
      return &effect;
    }
  }
  return nullptr;
}

const GroupInfo* exclusiveGroupInfo(std::string_view group) {
  static const std::map<std::string_view, GroupInfo> kGroups{
      {"toplevel-open-close-animation", {"Open/Close Animation", "Animation for windows being opened or closed"}},
      {"maximize", {"Maximize Animation", "Animation when windows are maximized or restored"}},
      {"minimize", {"Minimize Animation", "Animation when windows are minimized"}},
      {"magnifiers", {"Magnifier", "Screen magnifier zoom effect"}},
      {"show-desktop", {"Show Desktop Animation", "Animation for showing the desktop"}},
      {"desktop-animations", {"Workspace Switching Animation", "Animation when switching virtual desktops"}},
  };
  const auto it = kGroups.find(group);
  if (it != kGroups.end()) {
    return &it->second;
  }
  return nullptr;
}

const std::set<std::string_view>& animationGroups() {
  static const std::set<std::string_view> kGroups{
      "toplevel-open-close-animation",
      "maximize",
      "minimize",
      "show-desktop",
      "desktop-animations",
  };
  return kGroups;
}

bool isAnimationEffect(const Effect& effect) {
  if (!effect.exclusiveGroup.empty() && animationGroups().contains(effect.exclusiveGroup)) {
    return true;
  }
  return effect.category == "Animations";
}

} // namespace effects_catalog
