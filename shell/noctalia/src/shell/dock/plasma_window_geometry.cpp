#include "shell/dock/plasma_window_geometry.h"

#include "plasma-window-management-client-protocol.h"

namespace shell::dock {

  PlasmaWindowGeometryBridge::PlasmaWindowGeometryBridge() = default;

  PlasmaWindowGeometryBridge::~PlasmaWindowGeometryBridge() {
    reset();
  }

  void PlasmaWindowGeometryBridge::bind(org_kde_plasma_window_management* management) {
    reset();
    m_management = management;
  }

  void PlasmaWindowGeometryBridge::reset() {
    // The proxy objects are owned by the display and freed on disconnect, so we
    // only drop our references here (destroying them could double-free if the
    // display is torn down first).
    m_windows.clear();
    m_published.clear();
    // The management proxy is owned by the registry/display; we only borrow it.
    m_management = nullptr;
  }

  org_kde_plasma_window* PlasmaWindowGeometryBridge::acquire(const std::string& uuid) {
    if (uuid.empty() || m_management == nullptr) {
      return nullptr;
    }
    auto it = m_windows.find(uuid);
    if (it != m_windows.end()) {
      return it->second;
    }
    auto* window = org_kde_plasma_window_management_get_window_by_uuid(m_management, uuid.c_str());
    m_windows.emplace(uuid, window);
    return window;
  }

  void PlasmaWindowGeometryBridge::sync(
      wl_surface* panel, const std::unordered_map<std::string, DockWindowRect>& windowRects
  ) {
    if (panel == nullptr || m_management == nullptr) {
      return;
    }

    auto& published = m_published[panel];

    // Unset geometries that are no longer reported for this panel.
    for (auto it = published.begin(); it != published.end();) {
      if (windowRects.find(it->first) == windowRects.end()) {
        if (auto* window = acquire(it->first); window != nullptr) {
          org_kde_plasma_window_unset_minimized_geometry(window, panel);
        }
        it = published.erase(it);
      } else {
        ++it;
      }
    }

    // Set/update geometries; skip values that already match to avoid churn.
    for (const auto& [uuid, rect] : windowRects) {
      auto* window = acquire(uuid);
      if (window == nullptr) {
        continue;
      }
      auto prev = published.find(uuid);
      if (prev != published.end() && prev->second == rect) {
        continue;
      }
      org_kde_plasma_window_set_minimized_geometry(
          window, panel, static_cast<std::uint32_t>(rect.x), static_cast<std::uint32_t>(rect.y),
          static_cast<std::uint32_t>(rect.width), static_cast<std::uint32_t>(rect.height)
      );
      published[uuid] = rect;
    }

    if (published.empty()) {
      m_published.erase(panel);
    }
  }

  void PlasmaWindowGeometryBridge::clearPanel(wl_surface* panel) {
    if (panel == nullptr || m_management == nullptr) {
      return;
    }
    auto it = m_published.find(panel);
    if (it == m_published.end()) {
      return;
    }
    for (const auto& [uuid, rect] : it->second) {
      if (auto* window = acquire(uuid); window != nullptr) {
        org_kde_plasma_window_unset_minimized_geometry(window, panel);
      }
    }
    m_published.erase(it);
  }

} // namespace shell::dock
