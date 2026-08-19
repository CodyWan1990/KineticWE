#include "kinetic_settings_module.h"

#include "kinetic-settings/src/custom_keybinds.h"
#include "kinetic-settings/src/keybinds_kwe.h"
#include "kinetic-settings/src/kinetic_settings_window.h"

#include "cli/schema_msg.h"
#include "core/input/key_chord.h"
#include "core/log.h"
#include "ipc/ipc_service.h"

#include <string>

namespace {
  constexpr Logger kLog("kinetic-settings");
} // namespace

KineticSettingsModule::KineticSettingsModule() = default;

KineticSettingsModule::~KineticSettingsModule() = default;

void KineticSettingsModule::initialize(
    WaylandConnection& wayland, RenderContext& renderContext, SessionBus& bus, IpcService& ipcService,
    float uiScale
) {
  m_bus = &bus;
  // Custom keybind actions must exist in the daemon before the mirror replay
  // (importKeybinds) tries to re-apply their chords.
  m_customKeybinds = std::make_unique<CustomKeybinds>(bus);
  m_customKeybinds->initialize();
  m_window = std::make_unique<KineticSettingsWindow>();
  m_window->initialize(wayland, renderContext, bus, *m_customKeybinds, uiScale);
  bindIpc(ipcService);
  // The compositor is already up by the time the shell (and this module)
  // initializes, so the persisted keybinds mirror can be replayed now. A
  // failure is not fatal — the session keeps its live binds and the user can
  // re-run the import (`noctalia-kwe msg keybinds-import`).
  importKeybinds();
}

void KineticSettingsModule::importKeybinds() {
  if (m_bus == nullptr) {
    return;
  }
  const int applied = KeybindsKwe::applyToDaemon(*m_bus);
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