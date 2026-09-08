#pragma once

#include <set>
#include <string>
#include <string_view>
#include <vector>

// Shared discovery/catalog for the compositor's desktop effects, used by both
// the Desktop Effects page and the Animations page. The discovery logic lives
// here so the two pages never drift apart: Desktop Effects shows every
// user-facing effect except the animation ones, and Animations shows only the
// animation ones (plus the global animation-speed setting).
//
// Effects are discovered from kwin-wayland/builtin-effects/*.json plus the
// bundled scripted effects under kwin-wayland/effects. Third-party "Get New
// Effects" (KNewStuff) packages installed under kwin/effects are intentionally
// not listed. Each effect maps to a [Plugins] <name>Enabled key in
// kineticwe.kwe; the compiled-in default and display metadata come from the
// effect's JSON metadata file.
namespace effects_catalog {

struct Effect {
  std::string name;        // plugin id, e.g. "blur" — also the [Plugins] key stem
  std::string title;       // display name from KPlugin JSON ("Blur")
  std::string description;
  std::string category;
  bool enabledByDefault = false;
  bool internal = false;
  std::string exclusiveGroup; // e.g. "toplevel-open-close-animation"; empty when not exclusive
};

struct GroupInfo {
  std::string_view title;
  std::string_view description;
};

// The discovered, sorted effect list (memoized).
[[nodiscard]] const std::vector<Effect>& effects();

// Effects a page may show (non-internal, not on the exclusion list).
[[nodiscard]] bool isVisible(const Effect& effect);
[[nodiscard]] std::vector<const Effect*> visibleEffects();

// Config key path for an effect (its plugin id).
[[nodiscard]] std::string keyPath(const Effect& effect);
[[nodiscard]] const Effect* findEffect(std::string_view keyPath);

// Human-readable label/description for a mutually-exclusive effect group
// (nullptr when unknown).
[[nodiscard]] const GroupInfo* exclusiveGroupInfo(std::string_view group);

// Exclusive groups that belong on the Animations page rather than the Desktop
// Effects page (the "animation type" selectors).
[[nodiscard]] const std::set<std::string_view>& animationGroups();

// True for effects that are animations (an animation-type exclusive group, or
// the "Animations" category, which currently holds the move/resize
// "Geometry Change" effect).
[[nodiscard]] bool isAnimationEffect(const Effect& effect);

} // namespace effects_catalog
