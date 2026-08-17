#pragma once

#include "kinetic-settings/src/tiling_page.h"
#include "kinetic-settings/src/workspaces_page.h"

#include "render/animation/animation_manager.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "ui/controls/scroll_view.h"
#include "wayland/toplevel_surface.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

class Box;
class Flex;
class RenderContext;
class SelectDropdownPopup;
class SessionBus;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct wl_surface;

// Self-contained xdg-toplevel settings window for KineticWE desktop settings
// (kinetic-settings module). Owns its own surface and scene; never touches
// Noctalia's own settings code so upstream merges stay clean — but mirrors the
// Noctalia settings window's chrome exactly: header + close button, a filter
// row (search, Advanced toggle, Overridden toggle, two-stage Reset Page), a
// sidebar and a scrolling content area.
class KineticSettingsWindow {
public:
  KineticSettingsWindow();
  ~KineticSettingsWindow();

  KineticSettingsWindow(const KineticSettingsWindow&) = delete;
  KineticSettingsWindow& operator=(const KineticSettingsWindow&) = delete;

  void initialize(WaylandConnection& wayland, RenderContext& renderContext, SessionBus& bus, float uiScale);

  void open();
  void close();
  void toggle();
  [[nodiscard]] bool isOpen() const noexcept { return m_surface != nullptr; }

  [[nodiscard]] bool ownsKeyboardSurface(wl_surface* surface) const noexcept;
  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);

private:
  // Sidebar/secondary nav pages under the "Window Management" category.
  enum class ActivePage {
    Workspaces,
    Tiling,
  };

  void destroyWindow();
  void prepareFrame(bool needsUpdate, bool needsLayout);
  void buildScene(float width, float height);
  // Sizes the explicit-size chrome (background + main container) to the window
  // before a relayout; the plain-Node scene root never resizes its children.
  void resizeSceneChrome(float width, float height);
  // Full rebuild (chrome + content). Content-only rebuilds keep the filter
  // row's search input alive (no focus loss while typing).
  void requestSceneRebuild();
  void requestContentRebuild();
  void rebuildContent();
  void clearArmedResets() noexcept;

  [[nodiscard]] std::unique_ptr<Flex> buildHeaderRow(float scale);
  [[nodiscard]] std::unique_ptr<Flex> buildFilterRow(float scale);
  [[nodiscard]] std::unique_ptr<Node> buildSidebar(float scale);
  // Filters/callbacks handed to whichever page is currently active. Both pages
  // expose the same Filter/Callbacks shape, so these centralize the wiring.
  [[nodiscard]] TilingPage::Filter pageFilter() const;
  [[nodiscard]] TilingPage::Callbacks tilingPageCallbacks();
  [[nodiscard]] WorkspacesPage::Filter workspacesFilter() const;
  [[nodiscard]] WorkspacesPage::Callbacks workspacesPageCallbacks();
  void confirmResetPage();
  // Routes the filter row's Apply/Reset Page to the active page.
  void applyActivePage();
  void resetActivePage();
  [[nodiscard]] bool activeHasPending() const;
  [[nodiscard]] bool activeHasResettableSettings() const;
  [[nodiscard]] float uiScale() const noexcept { return m_uiScale; }

  WaylandConnection* m_wayland = nullptr;
  RenderContext* m_renderContext = nullptr;
  SessionBus* m_bus = nullptr;
  float m_uiScale = 1.0F;

  std::unique_ptr<ToplevelSurface> m_surface;
  std::unique_ptr<Node> m_sceneRoot;
  InputDispatcher m_inputDispatcher;
  std::unique_ptr<TilingPage> m_tilingPage;
  std::unique_ptr<WorkspacesPage> m_workspacesPage;
  std::unique_ptr<SelectDropdownPopup> m_selectPopup;
  wl_output* m_output = nullptr;
  Box* m_background = nullptr;
  Flex* m_pageContainer = nullptr;
  Flex* m_mainContainer = nullptr;

  // Active secondary page and whether "Window Management" is expanded.
  ActivePage m_activePage = ActivePage::Workspaces;
  bool m_windowManagementExpanded = true;

  ScrollViewState m_sidebarScrollState;
  ScrollViewState m_contentScrollState;

  // Filter-row chrome state. Search/Overridden reset when the window closes
  // (mirroring Noctalia); Advanced persists for the session.
  std::string m_searchQuery;
  bool m_showAdvanced = false;
  bool m_showOverriddenOnly = false;
  // Two-stage reset confirmations: the per-setting armed row ("group.key")
  // and the armed Reset Page button. Any other interaction disarms both.
  std::string m_armedResetKeyPath;
  bool m_resetPageArmed = false;

  bool m_rebuildRequested = true;
  bool m_contentRebuildRequested = false;
  float m_lastSceneWidth = 0.0F;
  float m_lastSceneHeight = 0.0F;

  // m_sceneRoot must be destroyed before m_animations — ~Node() calls cancelForOwner().
  AnimationManager m_animations;
};
