#pragma once

#include "kinetic-settings/src/animations_page.h"
#include "kinetic-settings/src/appearance_page.h"
#include "kinetic-settings/src/cursors_page.h"
#include "kinetic-settings/src/default_apps_page.h"
#include "kinetic-settings/src/desktop_effects_page.h"
#include "kinetic-settings/src/display_page.h"
#include "kinetic-settings/src/icons_page.h"
#include "kinetic-settings/src/keyboard_page.h"
#include "kinetic-settings/src/keybinds_page.h"
#include "kinetic-settings/src/pointing_devices_page.h"
#include "kinetic-settings/src/tiling_page.h"
#include "kinetic-settings/src/window_behavior_page.h"
#include "kinetic-settings/src/window_rules_page.h"
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
class CustomKeybinds;
class Flex;
class RenderContext;
class Renderer;
class SelectDropdownPopup;
class SessionBus;
class WaylandConnection;
struct KeyboardEvent;
struct PointerEvent;
struct wl_output;
struct wl_surface;
struct zwp_keyboard_shortcuts_inhibitor_v1;

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

  void initialize(
      WaylandConnection& wayland, RenderContext& renderContext, SessionBus& bus, CustomKeybinds& customKeybinds,
      float uiScale
  );

  void open();
  void close();
  void toggle();
  [[nodiscard]] bool isOpen() const noexcept { return m_surface != nullptr; }

  [[nodiscard]] bool ownsKeyboardSurface(wl_surface* surface) const noexcept;
  [[nodiscard]] bool onPointerEvent(const PointerEvent& event);
  void onKeyboardEvent(const KeyboardEvent& event);
  // Keeps a compositor keyboard-shortcuts inhibitor alive only while a keybind
  // recorder is capturing. This makes the compositor stop consuming global
  // shortcuts for the window so a chord that is already bound elsewhere can be
  // captured (and the conflict-overwrite prompt shown) instead of being eaten.
  void updateKeyboardShortcutInhibit();

private:
  // Sidebar/secondary nav pages, grouped under the collapsible categories.
  enum class ActivePage {
    // Appearance category (alphabetically first).
    AppearanceGlobalTheme,
    AppearanceIcons,
    AppearanceCursors,
    Animations,
    DesktopEffects,
    WindowBehavior,
    Workspaces,
    Tiling,
    WindowRules,
    Keyboard,
    Mouse,
    Touchpad,
    Keybinds,
    Displays,
    DefaultApps,
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
  // Rebuilds the content area only. The renderer is required by the Icons and
  // Cursors pages to load preview images.
  void rebuildContent(Renderer& renderer);
  void clearArmedResets() noexcept;

  [[nodiscard]] std::unique_ptr<Flex> buildFilterRow(float scale);
  [[nodiscard]] std::unique_ptr<Node> buildSidebar(float scale);
  // Filters/callbacks handed to whichever page is currently active. All pages
  // expose the same Filter/Callbacks shape, so these centralize the wiring.
  [[nodiscard]] TilingPage::Filter pageFilter() const;
  [[nodiscard]] TilingPage::Callbacks tilingPageCallbacks();
  [[nodiscard]] WorkspacesPage::Filter workspacesFilter() const;
  [[nodiscard]] WorkspacesPage::Callbacks workspacesPageCallbacks();
  [[nodiscard]] KeybindsPage::Filter keybindsFilter() const;
  [[nodiscard]] KeybindsPage::Callbacks keybindsPageCallbacks();
  [[nodiscard]] KeyboardPage::Filter keyboardFilter() const;
  [[nodiscard]] KeyboardPage::Callbacks keyboardPageCallbacks();
  [[nodiscard]] PointingDevicesPage::Filter mouseFilter() const;
  [[nodiscard]] PointingDevicesPage::Callbacks mousePageCallbacks();
  [[nodiscard]] PointingDevicesPage::Filter touchpadFilter() const;
  [[nodiscard]] PointingDevicesPage::Callbacks touchpadPageCallbacks();
  [[nodiscard]] WindowRulesPage::Filter windowRulesFilter() const;
  [[nodiscard]] WindowRulesPage::Callbacks windowRulesPageCallbacks();
  [[nodiscard]] WindowBehaviorPage::Filter windowBehaviorFilter() const;
  [[nodiscard]] WindowBehaviorPage::Callbacks windowBehaviorPageCallbacks();
  [[nodiscard]] AppearancePage::Filter appearanceFilter() const;
  [[nodiscard]] AppearancePage::Callbacks appearancePageCallbacks();
  [[nodiscard]] IconsPage::Filter iconsFilter() const;
  [[nodiscard]] IconsPage::Callbacks iconsPageCallbacks();
  [[nodiscard]] CursorsPage::Filter cursorsFilter() const;
  [[nodiscard]] CursorsPage::Callbacks cursorsPageCallbacks();
  [[nodiscard]] DesktopEffectsPage::Filter desktopEffectsFilter() const;
  [[nodiscard]] DesktopEffectsPage::Callbacks desktopEffectsPageCallbacks();
  [[nodiscard]] AnimationsPage::Filter animationsFilter() const;
  [[nodiscard]] AnimationsPage::Callbacks animationsPageCallbacks();
  [[nodiscard]] DefaultAppsPage::Filter defaultAppsFilter() const;
  [[nodiscard]] DefaultAppsPage::Callbacks defaultAppsPageCallbacks();
  [[nodiscard]] DisplayPage::Filter displayFilter() const;
  [[nodiscard]] DisplayPage::Callbacks displayPageCallbacks();
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
  CustomKeybinds* m_customKeybinds = nullptr;
  float m_uiScale = 1.0F;

  std::unique_ptr<ToplevelSurface> m_surface;
  std::unique_ptr<Node> m_sceneRoot;
  InputDispatcher m_inputDispatcher;
  std::unique_ptr<TilingPage> m_tilingPage;
  std::unique_ptr<WorkspacesPage> m_workspacesPage;
  std::unique_ptr<KeybindsPage> m_keybindsPage;
  std::unique_ptr<KeyboardPage> m_keyboardPage;
  std::unique_ptr<PointingDevicesPage> m_mousePage;
  std::unique_ptr<PointingDevicesPage> m_touchpadPage;
  std::unique_ptr<WindowRulesPage> m_windowRulesPage;
  std::unique_ptr<WindowBehaviorPage> m_windowBehaviorPage;
  std::unique_ptr<DisplayPage> m_displayPage;
  std::unique_ptr<AppearancePage> m_appearancePage;
  std::unique_ptr<IconsPage> m_iconsPage;
  std::unique_ptr<CursorsPage> m_cursorsPage;
  std::unique_ptr<DesktopEffectsPage> m_desktopEffectsPage;
  std::unique_ptr<AnimationsPage> m_animationsPage;
  std::unique_ptr<DefaultAppsPage> m_defaultAppsPage;
  std::unique_ptr<SelectDropdownPopup> m_selectPopup;
  // Non-null only while a keybind recorder is capturing (see updateKeyboardShortcutInhibit).
  zwp_keyboard_shortcuts_inhibitor_v1* m_shortcutInhibitor = nullptr;
  wl_output* m_output = nullptr;
  Box* m_background = nullptr;
  Flex* m_pageContainer = nullptr;
  Flex* m_mainContainer = nullptr;

  // Active secondary page and which sidebar categories are expanded.
  ActivePage m_activePage = ActivePage::AppearanceGlobalTheme;
  bool m_windowManagementExpanded = true;
  bool m_inputOutputExpanded = true;
  bool m_appearanceExpanded = true;
  bool m_systemExpanded = true;

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
