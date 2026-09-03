#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

struct wl_surface;
struct org_kde_plasma_window;
struct org_kde_plasma_window_management;

namespace shell::dock {

  // A dock item's on-surface rect (panel-local coordinates).
  struct DockWindowRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const DockWindowRect&) const = default;
  };

  // Bridges dock-item geometry to a KWin/KineticWE compositor's
  // org_kde_plasma_window_management protocol so minimize effects (Magic Lamp)
  // can animate a window down into its dock icon, instead of falling back to
  // cursor-minimize.
  //
  // Every method is a no-op while no management global is bound (e.g. on other
  // compositors), so callers never need to gate on the backend.
  class PlasmaWindowGeometryBridge {
  public:
    PlasmaWindowGeometryBridge();
    ~PlasmaWindowGeometryBridge();

    PlasmaWindowGeometryBridge(const PlasmaWindowGeometryBridge&) = delete;
    PlasmaWindowGeometryBridge& operator=(const PlasmaWindowGeometryBridge&) = delete;

    // Adopt the management global. Replaces any previously-bound manager.
    void bind(org_kde_plasma_window_management* management);

    // Make `panel` report exactly the given uuid→rect geometry for the windows
    // it displays. Windows previously reported for `panel` but absent from
    // `windowRects` are unset. Coordinates are relative to the dock surface.
    void sync(wl_surface* panel, const std::unordered_map<std::string, DockWindowRect>& windowRects);

    // Forget everything published for `panel` (e.g. the dock surface is gone).
    void clearPanel(wl_surface* panel);

    // Release all cached window proxies and detach the manager.
    void reset();

  private:
    org_kde_plasma_window* acquire(const std::string& uuid);

    org_kde_plasma_window_management* m_management = nullptr;
    std::unordered_map<std::string, org_kde_plasma_window*> m_windows;       // uuid -> window proxy
    std::unordered_map<wl_surface*, std::unordered_map<std::string, DockWindowRect>> m_published;
  };

} // namespace shell::dock
