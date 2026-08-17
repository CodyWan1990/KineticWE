#include "kinetic_settings_module.h"

#include "kinetic-settings/src/kinetic_settings_window.h"

#include "cli/schema_msg.h"
#include "ipc/ipc_service.h"

#include <string>

KineticSettingsModule::KineticSettingsModule() = default;

KineticSettingsModule::~KineticSettingsModule() = default;

void KineticSettingsModule::initialize(
    WaylandConnection& wayland, RenderContext& renderContext, SessionBus& bus, IpcService& ipcService,
    float uiScale
) {
  m_window = std::make_unique<KineticSettingsWindow>();
  m_window->initialize(wayland, renderContext, bus, uiScale);
  bindIpc(ipcService);
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