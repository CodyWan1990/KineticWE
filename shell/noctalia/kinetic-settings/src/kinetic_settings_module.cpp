#include "kinetic_settings_module.h"

#include "kinetic-settings/src/autostart.h"
#include "kinetic-settings/src/custom_keybinds.h"
#include "kinetic-settings/src/keybinds_kwe.h"
#include "kinetic-settings/src/kinetic_settings_window.h"

#include "cli/schema_msg.h"
#include "core/input/key_chord.h"
#include "core/log.h"
#include "ipc/ipc_service.h"

#include <string>
#include <unordered_map>
#include <utility>

namespace {
  constexpr Logger kLog("kinetic-settings");
} // namespace

KineticSettingsModule::KineticSettingsModule() = default;

KineticSettingsModule::~KineticSettingsModule() = default;

void KineticSettingsModule::initialize(
    WaylandConnection& wayland, CompositorPlatform& platform, RenderContext& renderContext, SessionBus& bus,
    IpcService& ipcService, const ConfigService& config, float uiScale
) {
  m_bus = &bus;
  // Custom keybind actions must exist in the daemon before the mirror replay
  // (importKeybinds) tries to re-apply their chords.
  m_customKeybinds = std::make_unique<CustomKeybinds>(bus, config);
  m_customKeybinds->initialize();
  m_window = std::make_unique<KineticSettingsWindow>();
  m_window->initialize(wayland, platform, renderContext, bus, *m_customKeybinds, uiScale);
  bindIpc(ipcService);
  // The compositor is already up by the time the shell (and this module)
  // initializes, so the persisted keybinds mirror can be replayed now. A
  // failure is not fatal — the session keeps its live binds and the user can
  // re-run the import (`noctalia-kwe msg keybinds-import`).
  importKeybinds();
  // Start the user's XDG autostart entries (~/.config/autostart). Runs once
  // per session; a supervised shell restart is a no-op.
  autostart::launchAll();
}

void KineticSettingsModule::importKeybinds() {
  if (m_bus == nullptr) {
    return;
  }
  // User-added Applications / Custom Commands actions must replay their chords
  // with exactly the friendly names they were registered with (doRegister),
  // otherwise the daemon's findAction() cannot match the action id and the
  // replay is silently dropped. Build the override map from the persisted
  // definitions so the mirror's custom-component entries match at session
  // start.
  std::unordered_map<std::string, KeybindsKwe::FriendlyNameOverride> friendlyNameOverrides;
  if (m_customKeybinds != nullptr) {
    for (const auto& definition : m_customKeybinds->definitions()) {
      KeybindsKwe::FriendlyNameOverride override;
      override.componentFriendlyName =
          definition.component == CustomKeybinds::kAppsComponent
          ? std::string(CustomKeybinds::kAppsFriendly)
          : std::string(CustomKeybinds::kCommandsFriendly);
      override.actionFriendlyName = definition.friendlyName;
      friendlyNameOverrides.emplace(definition.component + "." + definition.action, std::move(override));
    }
  }
  const int applied = KeybindsKwe::applyToDaemon(*m_bus, friendlyNameOverrides);
  if (applied > 0) {
    kLog.info("keybinds import applied {} shortcut(s)", applied);
  }
}

void KineticSettingsModule::bindIpc(IpcService& ipcService) {
  ipcService.bind(noctalia::cli::msg::kineticSettingsOpen, [this](const std::string& /*args*/) -> std::string {
    if (m_window != nullptr) {
      m_window->open();
    }
    return "OK\n";
  });
  ipcService.bind(noctalia::cli::msg::kineticSettingsClose, [this](const std::string& /*args*/) -> std::string {
    if (m_window != nullptr) {
      m_window->close();
    }
    return "OK\n";
  });
  ipcService.bind(noctalia::cli::msg::kineticSettingsToggle, [this](const std::string& /*args*/) -> std::string {
    if (m_window != nullptr) {
      m_window->toggle();
    }
    return "OK\n";
  });
  ipcService.bind(noctalia::cli::msg::keybindsImport, [this](const std::string& /*args*/) -> std::string {
    importKeybinds();
    return "OK\n";
  });
}

bool KineticSettingsModule::ownsKeyboardSurface(wl_surface* surface) const noexcept {
  return m_window != nullptr && m_window->ownsKeyboardSurface(surface);
}

bool KineticSettingsModule::onPointerEvent(const PointerEvent& event) {
  return m_window != nullptr && m_window->onPointerEvent(event);
}

void KineticSettingsModule::onKeyboardEvent(const KeyboardEvent& event) {
  if (m_window != nullptr) {
    m_window->onKeyboardEvent(event);
  }
}