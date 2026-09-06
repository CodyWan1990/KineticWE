#pragma once

#include "kinetic-settings/src/compositor_dbus.h"
#include "kinetic-settings/src/kineticwe_kwe.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Flex;
class Node;

// The Desktop Effects settings page: lists the compositor's built-in desktop
// effects (discovered from kwin-wayland/builtin-effects/*.json) and lets the
// user toggle them on/off. Each effect maps to a [Plugins] <name>Enabled key
// in kineticwe.kwe; the compiled-in default and display metadata (title,
// description, category) come from the effect's JSON metadata file.
//
// Edits are STAGED like the other pages: toggles update an in-memory pending
// map and nothing touches disk or the compositor until Apply. applyAll()
// writes [Plugins] once and then tells the compositor to load/unload each
// changed effect via org.kde.kwin.Effects so the change applies live without
// a session restart.
class DesktopEffectsPage {
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

  explicit DesktopEffectsPage(class SessionBus& bus);

  // Rebuilds the page node. `armedResetKey` is the effect name whose Reset
  // button was clicked once and is waiting for the confirming second click.
  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  // Effect names of every resettable effect on the page (Reset Page scope).
  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;

  // Stages one effect back at its default (nothing is written until Apply).
  void resetKeyPath(std::string_view keyPath);
  // Stages every effect back at its default.
  void resetAll();

  // True while unapplied (staged) edits exist — drives the window's Apply button.
  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }

  // Writes [Plugins] to kineticwe.kwe, then load/unloads each changed effect.
  // Returns false when the (gating) file write failed — staged edits are kept
  // for retry and nothing reaches the compositor.
  bool applyAll();

  // Force the next build() to re-read the config file from disk, discarding any
  // staged edits.
  void reloadFromDisk() noexcept {
    m_loaded = false;
    m_pending.clear();
  }

private:
  struct Effect {
    std::string name;        // plugin id, e.g. "blur" — also the [Plugins] key stem
    std::string title;       // display name from JSON ("Blur")
    std::string description;
    std::string category;
    bool enabledByDefault = false;
    bool internal = false;
  };

  // The discovered, sorted effect list (memoized).
  [[nodiscard]] static const std::vector<Effect>& effects();
  [[nodiscard]] static std::vector<Effect> discoverEffects();
  // Effects the page actually shows (non-internal, not on the exclusion list).
  [[nodiscard]] static bool isVisible(const Effect& effect);
  [[nodiscard]] static std::vector<const Effect*> visibleEffects();
  [[nodiscard]] static std::string keyPath(const Effect& effect);
  [[nodiscard]] static const Effect* findEffect(std::string_view keyPath);

  void ensureLoaded();
  [[nodiscard]] bool fileBackedEnabled(const Effect& effect) const;
  [[nodiscard]] bool effectiveEnabled(const Effect& effect) const;
  [[nodiscard]] bool overridden(const Effect& effect) const;
  [[nodiscard]] bool matchesFilter(const Effect& effect, const Filter& filter) const;

  void stage(const Effect& effect, bool value);
  void stageAndRebuild(const Effect& effect, bool value, const Callbacks& callbacks);
  void addEffectRow(
      Flex& parent, const Effect& effect, float scale, std::string_view armedResetKey,
      const Callbacks& callbacks
  );

  CompositorDbus m_dbus;
  KineticweKwe m_config;
  bool m_loaded = false;
  // Edits staged by the controls, keyed by effect name; flushed by applyAll().
  std::map<std::string, bool> m_pending;
};
