#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class SessionBus;

// Thin sdbus-cpp client for the compositor-owned D-Bus surface:
//   org.kde.KWin                (/KWin)                   — reconfigure()
//   org.kde.KWin.Tiling         (/Tiling)                 — live layout read/switch
//   org.kde.KWin.VirtualDesktopManager (/VirtualDesktopManager) — workspaces
// No KDE service is involved; both are owned by the kinetic-we process.
class CompositorDbus {
public:
  explicit CompositorDbus(SessionBus& bus);

  // Trigger a full compositor config reparse (KConfig reload + reconfigure).
  void requestReconfigure();

  // Re-reads and re-applies a single effect's config
  // (org.kde.kwin.Effects reconfigureEffect), used after editing an effect's
  // [Effect-*] group in kineticwe.kwe.
  void reconfigureEffect(const std::string& name);

  // Loads/unloads an effect at runtime (org.kde.kwin.Effects loadEffect /
  // unloadEffect). Used after toggling an effect's [Plugins] <name>Enabled key
  // so the change applies live instead of waiting for the compositor's config
  // watcher to notice the file write.
  void loadEffect(const std::string& name);
  void unloadEffect(const std::string& name);

  // Name of the compositor's active output (org.kde.KWin.activeOutputName),
  // e.g. "DP-1"; empty when the compositor can't be reached.
  [[nodiscard]] std::string activeOutputName();

  // Window properties returned by org.kde.KWin queryWindowInfo (the
  // interactive "detect window properties" pick). Empty strings / 0 mean the
  // property was absent; `valid` is false when the pick failed or was
  // cancelled.
  struct DetectedWindow {
    std::string resourceClass;
    std::string resourceName;
    std::string role;
    std::string caption;
    std::string desktopFile;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool fullscreen = false;
    bool keepAbove = false;
    bool keepBelow = false;
    bool minimized = false;
    bool noBorder = false;
    bool maximizeHorizontal = false;
    bool maximizeVertical = false;
    int decorationPolicy = 1;
    bool valid = false;
  };

  // Starts an interactive window pick (the compositor grabs the next pointer
  // click) and BLOCKS until the user selects a window, then returns its
  // properties. `valid` is false when the pick was cancelled or failed.
  [[nodiscard]] DetectedWindow queryWindowInfo();

  // Live tiling state (org.kde.KWin.Tiling).
  [[nodiscard]] std::string currentLayout();
  [[nodiscard]] std::vector<std::string> enabledLayouts();
  void setLayout(const std::string& kind);
  void cycleLayout();
  // Flips the active monitor's outer (left/right) gaps between the
  // [Tiling] GapToggleWide / GapToggleCentered presets and applies them live
  // (same action as the "Tiling Toggle Outer Gaps" shortcut).
  void toggleOuterGaps();

  // One workspace as reported by the compositor's
  // org.kde.KWin.VirtualDesktopManager `desktops` property. A newly created
  // workspace only gets an id after the compositor has created it.
  struct VirtualDesktopInfo {
    std::uint32_t position = 0;
    std::string id;
    std::string name;
  };

  // Live virtual-desktop (workspace) state: desktops(), rows(). Reading a
  // failed/unreachable compositor yields an empty list / 0.
  [[nodiscard]] std::vector<VirtualDesktopInfo> virtualDesktops();
  [[nodiscard]] std::uint32_t virtualDesktopRows();  // Creates a workspace at `position` (0-based, appends when it equals the
  // current count) with the given name. The compositor generates the id and
  // persists the change to the config file itself.
  void createDesktop(std::uint32_t position, const std::string& name);
  void removeDesktop(const std::string& id);
  void setDesktopName(const std::string& id, const std::string& name);
  // Sets the live row count (1..workspace count) AND writes [Desktops] Rows to
  // the compositor's config: unlike create/remove/rename, the compositor's
  // setRows does not call save(), so without this the layout would not survive
  // a restart.
  void setVirtualDesktopRows(std::uint32_t rows);

  // Input devices. The compositor exposes its libinput devices over D-Bus
  // (org.kde.KWin.InputDevice at /org/kde/KWin/InputDevice/<sysName>); the
  // manager object lists them. Only the pointer-capable devices (which include
  // touchpads) are enumerated here — that is what the Mouse settings page needs.
  struct InputDeviceInfo {
    std::string sysName;
    std::string name;
    bool pointer = false;
    bool touchpad = false;
    bool touch = false;
    // A device that also exposes a keyboard capability is not a plain mouse
    // (Valve Steam Controller pucks, a keyboard's companion "Mouse" interface).
    bool keyboard = false;
    // uinput/virtual devices (e.g. ydotoold) have no physical scroll wheel.
    bool isVirtual = false;
    // Number of exported mouse buttons; used to prefer a full mouse over a
    // keyboard/game-controller interface when picking the default device.
    int supportedButtons = 0;
    std::uint32_t vendor = 0;
    std::uint32_t product = 0;
  };

  [[nodiscard]] std::vector<InputDeviceInfo> inputDevices();

  // Typed read of one exported device property (org.kde.KWin.InputDevice).
  // Returns std::nullopt when the compositor / device is unreachable or the
  // property is absent.
  [[nodiscard]] std::optional<bool> inputDeviceBool(std::string_view sysName, std::string_view property);
  [[nodiscard]] std::optional<double> inputDeviceDouble(std::string_view sysName, std::string_view property);
  [[nodiscard]] std::optional<std::uint32_t> inputDeviceUint(std::string_view sysName, std::string_view property);
  // Same, for properties exported with the D-Bus `i` signature (supportedButtons).
  [[nodiscard]] std::optional<int> inputDeviceInt(std::string_view sysName, std::string_view property);
  [[nodiscard]] std::optional<std::string> inputDeviceString(std::string_view sysName, std::string_view property);

  // Writes one device property. The compositor's setter persists the value to
  // its input config and applies it to libinput live, so no reconfigure is
  // needed afterwards.
  void setInputDeviceBool(std::string_view sysName, std::string_view property, bool value);
  void setInputDeviceDouble(std::string_view sysName, std::string_view property, double value);

private:
  SessionBus& m_bus;
};