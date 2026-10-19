#include "layout_catalog.h"

#include "core/log.h"

#include <xkbcommon/xkbregistry.h>

namespace layout_catalog {

namespace {

  constexpr Logger kLog("kinetic-settings");

  std::vector<Entry> buildCatalog() {
    std::vector<Entry> catalog;

    rxkb_context* context = rxkb_context_new(RXKB_CONTEXT_NO_FLAGS);
    if (context == nullptr) {
      kLog.warn("layout catalog: could not create the XKB registry context");
      return catalog;
    }

    if (!rxkb_context_parse_default_ruleset(context)) {
      // Usually a broken/missing xkeyboard-config on the host. Leave the
      // catalog empty so the page can show a clear notice instead of an
      // incomplete list.
      kLog.warn("layout catalog: failed to parse the XKB ruleset");
      rxkb_context_unref(context);
      return catalog;
    }

    for (rxkb_layout* layout = rxkb_layout_first(context); layout != nullptr; layout = rxkb_layout_next(layout)) {
      const char* name = rxkb_layout_get_name(layout);
      if (name == nullptr || *name == '\0') {
        continue;
      }
      const char* variant = rxkb_layout_get_variant(layout);
      const char* description = rxkb_layout_get_description(layout);
      catalog.push_back(Entry{
          .layout = name,
          .variant = variant != nullptr ? variant : "",
          .description = description != nullptr ? description : "",
      });
    }

    rxkb_context_unref(context);
    return catalog;
  }

} // namespace

const std::vector<Entry>& entries() {
  static const std::vector<Entry> catalog = buildCatalog();
  return catalog;
}

std::string label(const Entry& entry) {
  if (!entry.description.empty()) {
    return entry.description;
  }
  if (entry.variant.empty()) {
    return entry.layout;
  }
  return entry.layout + " (" + entry.variant + ")";
}

std::string labelFor(std::string_view layout, std::string_view variant) {
  for (const auto& entry : entries()) {
    if (entry.layout == layout && entry.variant == variant) {
      return label(entry);
    }
  }
  if (variant.empty()) {
    return std::string(layout);
  }
  return std::string(layout) + " (" + std::string(variant) + ")";
}

} // namespace layout_catalog
