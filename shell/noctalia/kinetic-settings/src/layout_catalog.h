#pragma once

#include <string>
#include <string_view>
#include <vector>

// Catalog of the keyboard layouts and variants available on the system, used by
// the Kweboard page's layout picker so layouts are chosen by their
// language/description instead of by typing XKB codes. Backed by
// libxkbregistry — the same data source Plasma's keyboard KCM uses — so the
// list matches what the compositor can actually load.
namespace layout_catalog {

struct Entry {
  std::string layout;      // XKB layout code, e.g. "us", "gb"
  std::string variant;     // XKB variant code; empty for the base layout
  std::string description; // localized description, e.g. "English (US)"
};

// The memoized catalog, parsed from the default XKB ruleset on first use.
// Empty when the registry data is unavailable.
[[nodiscard]] const std::vector<Entry>& entries();

// Display label for a layout/variant pair, falling back to the raw codes when
// the pair is not in the catalog.
[[nodiscard]] std::string labelFor(std::string_view layout, std::string_view variant);

// Display label for a catalog entry.
[[nodiscard]] std::string label(const Entry& entry);

} // namespace layout_catalog
