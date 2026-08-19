#pragma once

#include <memory>

class CustomKeybinds;
class IpcService;
class KineticSettingsWindow;
class RenderContext;
class SessionBus;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_surface;

// The kinetic-settings module: owns the Kinetic Settings window and exposes
// it through the `msg` IPC surface (kinetic-settings-open/close/toggle) so
// the launcher desktop entry and gestures can drive it. Also owns the custom
// keybind service (user-added Applications / Custom Commands global
// shortcuts), whose registration must run before the keybinds mirror replay.
class KineticSettingsModule {
public:
  KineticSettingsModule();
  ~KineticSettingsModule();

  KineticSettingsModule(const KineticSettingsModule&) = delete;
  KineticSettingsModule& operator=(const KineticSettingsModule&) = delete;

  void initialize(
      WaylandConnection& wayland, RenderContext& renderContext, SessionBus& bus, IpcService& ipcService,
      float uiScale
  );

  [[nodiscard]] bool ownsKeyboardSurface(wl_surface* surface) const noexcept;
  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);

private:
  void bindIpc(IpcService& ipcService);
  // Re-applies ~/.config/kineticwe/keybinds.kwe to the compositor's global
  // shortcut daemon. Called at startup (initialize) and on demand through the
  // `keybinds-import` IPC message.
  void importKeybinds();

  std::unique_ptr<CustomKeybinds> m_customKeybinds;
  std::unique_ptr<KineticSettingsWindow> m_window;
  SessionBus* m_bus = nullptr;
};