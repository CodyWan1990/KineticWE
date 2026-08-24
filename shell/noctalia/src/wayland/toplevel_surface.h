#pragma once

#include "wayland/surface.h"

#include <cstdint>
#include <functional>
#include <string>

struct wl_array;
struct wl_output;
struct xdg_surface;
struct xdg_toplevel;
struct zxdg_toplevel_decoration_v1;

struct ToplevelSurfaceConfig {
  std::uint32_t width = 720;
  std::uint32_t height = 480;
  std::uint32_t minWidth = 0;
  std::uint32_t minHeight = 0;
  std::string title = "Noctalia";
  const char* appId = "dev.noctalia.Noctalia";
  bool decorated = false;
};

class ToplevelSurface : public Surface {
public:
  explicit ToplevelSurface(WaylandConnection& connection);
  ~ToplevelSurface() override;

  using Surface::initialize;
  bool initialize() override { return false; }
  bool initialize(wl_output* output, ToplevelSurfaceConfig config);

  void setClosedCallback(std::function<void()> callback);
  void setMinSize(std::uint32_t minWidth, std::uint32_t minHeight);
  void beginMove(std::uint32_t serial);

  [[nodiscard]] xdg_surface* xdgSurface() const noexcept { return m_xdgSurface; }

  static void handleXdgSurfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial);
  static void handleToplevelConfigure(
      void* data, xdg_toplevel* toplevel, std::int32_t width, std::int32_t height, wl_array* states
  );
  static void handleToplevelClose(void* data, xdg_toplevel* toplevel);
  static void
  handleToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, std::int32_t width, std::int32_t height);
  static void handleToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities);
  static void handleToplevelDecorationConfigure(
      void* data, zxdg_toplevel_decoration_v1* decoration, std::uint32_t mode
  );

private:
  void destroyRoleObjects();

  xdg_surface* m_xdgSurface = nullptr;
  xdg_toplevel* m_toplevel = nullptr;
  zxdg_toplevel_decoration_v1* m_toplevelDecoration = nullptr;
  std::function<void()> m_closedCallback;
  std::uint32_t m_pendingWidth = 0;
  std::uint32_t m_pendingHeight = 0;
  std::int32_t m_lastToplevelWidth = 0;
  std::int32_t m_lastToplevelHeight = 0;
};
