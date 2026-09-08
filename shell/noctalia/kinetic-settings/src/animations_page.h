#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/effects_catalog.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;

// The Animations settings page: the animation-type selectors (window
// open/close, maximize, minimize, full screen, show desktop, workspace
// switching) plus the window move/resize "Geometry Change" toggle and the
// global animation speed. Shares effect discovery with the Desktop Effects
// page via effects_catalog; this page shows ONLY the animation effects.
//
// The animation-type selectors are mutually-exclusive effect groups rendered
// as dropdowns (or a plain toggle when a group has a single member). They
// write the same [Plugins] <name>Enabled keys as the Desktop Effects page.
// Animation speed is the compositor's global [KDE] AnimationDurationFactor in
// kineticwe.kwe (0 = instant, 1 = normal, higher = slower).
//
// Edits are staged like the other pages; applyAll() writes [Plugins] and
// [KDE] AnimationDurationFactor once, then reconfigure()s the compositor so
// the changes apply live.
class AnimationsPage {
public:
  struct Filter {
    std::string searchQuery;
    bool showAdvanced = false;
    bool showOverriddenOnly = false;
  };

  struct Callbacks {
    std::function<void()> requestRebuild;
    std::function<void(std::string keyPath)> armReset;
    std::function<void(std::string keyPath)> confirmReset;
  };

  explicit AnimationsPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the key whose Reset button was
  // clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // Keys of every resettable setting on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one setting back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  // Stages every setting back at its default.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept;

  // Writes [Plugins] and [KDE] AnimationDurationFactor to kineticwe.kwe, then
  // triggers a compositor reconfigure. Returns false when the (gating) file
  // write failed — staged edits are kept for retry.
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
    m_pendingSpeed.reset();
  }

private:
  using Effect = effects_catalog::Effect;

  // Reset key for the global animation speed (not an effect).
  static constexpr std::string_view kSpeedKeyPath = "animation-speed";

  void ensureLoaded();
  [[nodiscard]] bool fileBackedEnabled(const Effect& effect) const;
  [[nodiscard]] bool effectiveEnabled(const Effect& effect) const;
  [[nodiscard]] bool overridden(const Effect& effect) const;
  [[nodiscard]] bool matchesFilter(const Effect& effect, const Filter& filter) const;

  // Animation speed helpers (global [KDE] AnimationDurationFactor).
  [[nodiscard]] double fileBackedSpeed() const;
  [[nodiscard]] double effectiveSpeed() const;
  [[nodiscard]] bool speedOverridden() const;
  void stageSpeed(double factor);
  [[nodiscard]] static std::size_t speedToIndex(double factor);
  [[nodiscard]] static double indexToSpeed(std::size_t index);
  [[nodiscard]] static std::string_view speedLabel(std::size_t index);

  void stage(const Effect& effect, bool value);
  void stageGroup(const std::vector<const Effect*>& members, const Effect* selected);

  void addSpeedRow(
      Flex& parent, float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );
  void addEffectRow(
      Flex& parent, std::string_view title, std::string_view description, const Effect& effect,
      float scale, std::string_view armedResetKey, const Callbacks& callbacks
  );
  void addExclusiveGroupRow(
      Flex& parent, std::string_view group, const std::vector<const Effect*>& members,
      float scale, const Callbacks& callbacks
  );

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by effect name; flushed by applyAll().
  std::map<std::string, bool> m_pending;
  // Staged global animation speed factor; flushed by applyAll().
  std::optional<double> m_pendingSpeed;
};
