#include "title_font_sync.h"

#include "core/log.h"
#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <optional>
#include <string>

namespace {

  constexpr Logger kLog("kinetic-settings");

  constexpr std::string_view kGroup = "org.kde.kdecoration2";
  constexpr std::string_view kKey = "KineticTitleFontFamily";

} // namespace

void syncTitleFontFamily(std::string_view shellFontFamily, SessionBus& bus) {
  KineticweKwe config;
  // A missing file is fine: save() creates it with just this key.
  config.load();

  const std::optional<std::string> stored = config.readString(kGroup, kKey);
  if (shellFontFamily.empty()) {
    if (!stored.has_value() || !config.removeKey(kGroup, kKey)) {
      return;
    }
  } else if (stored.has_value() && *stored == shellFontFamily) {
    return;
  } else {
    config.setString(kGroup, kKey, shellFontFamily);
  }

  if (!config.save()) {
    kLog.warn("failed to write title font family to {}", config.path().string());
    return;
  }
  CompositorDbus(bus).requestReconfigure();
}
