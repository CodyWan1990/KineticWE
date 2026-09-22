#pragma once

#include "ui/builders.h"
#include "ui/controls/button.h"
#include "ui/controls/collapsible.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"
#include "ui/palette.h"
#include "ui/scroll_into_view.h"
#include "ui/style.h"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

// Module-local re-implementation of Noctalia's collapsible settings group card
// (settings_content_common.cpp addSettingsGroupCard), so the Kinetic Settings
// pages match the Noctalia Settings look without depending on its internals.
namespace kinetic_settings {

  // Rounded card with a bold title header and a chevron; expanding/collapsing
  // folds its body. `expandedGroups` is the caller's persistent set of expanded
  // group keys, owned by the page object so the state survives the per-change
  // scene rebuild. When `jumpRow` is given, a section pill is added to it and
  // wired to toggle the card and scroll it to the top of the content view
  // (mirrors Noctalia's fixed group-jump row). Returns the body column to add
  // rows to.
  inline Flex* addCollapsibleSection(
      Flex& parent, std::string_view title, std::string_view key, std::unordered_set<std::string>& expandedGroups,
      float scale, Flex* jumpRow = nullptr
  ) {
    const std::string groupKey(key);
    auto* expandedGroupsPtr = &expandedGroups;

    auto card = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .configure =
            [scale](Flex& container) {
              container.setPadding(Style::spaceSm * scale, Style::spaceMd * scale);
              container.setCardStyle(scale, 0.4F);
            },
    });

    auto body = ui::column({
        .align = FlexAlign::Stretch,
        .gap = Style::spaceSm * scale,
        .configure = [scale](Flex& container) { container.setPadding(Style::spaceSm * scale, 0.0F, 0.0F, 0.0F); },
    });
    Flex* bodyRaw = body.get();

    auto collapsible = std::make_unique<Collapsible>();
    collapsible->setScale(scale);
    collapsible->setHeaderPadding(Style::spaceXs, 0.0F);
    collapsible->setHeader(ui::label({
        .text = std::string(title),
        .fontSize = Style::fontSizeTitle * scale,
        .fontWeight = FontWeight::Bold,
        .color = colorSpecFromRole(ColorRole::OnSurface),
    }));
    collapsible->setBody(std::move(body));
    collapsible->setExpandedImmediate(expandedGroupsPtr->contains(groupKey));

    // Optional section pill in the window's fixed jump row.
    Button* pill = nullptr;
    if (jumpRow != nullptr) {
      jumpRow->addChild(ui::button({
          .out = &pill,
          .text = std::string(title),
          .fontSize = Style::fontSizeCaption * scale,
          .variant = expandedGroupsPtr->contains(groupKey) ? ButtonVariant::Primary : ButtonVariant::Default,
          .radius = Style::scaledRadiusMd(scale),
      }));
    }

    Collapsible* collapsibleRaw = collapsible.get();
    if (pill != nullptr) {
      pill->setOnClick([expandedGroupsPtr, groupKey, pill, collapsibleRaw, scale]() {
        if (expandedGroupsPtr->contains(groupKey)) {
          expandedGroupsPtr->erase(groupKey);
          pill->setVariant(ButtonVariant::Default);
          collapsibleRaw->setExpanded(false);
          return;
        }
        expandedGroupsPtr->insert(groupKey);
        pill->setVariant(ButtonVariant::Primary);
        collapsibleRaw->setExpandedImmediate(true);
        if (ScrollView* scrollView = findEnclosingScrollView(collapsibleRaw)) {
          scrollNodeToScrollViewTop(*scrollView, *collapsibleRaw, Style::spaceMd * scale);
        }
      });
    }
    // The page object outlives the scene, so borrowing its set is safe for the
    // lifetime of this node.
    collapsible->setOnToggle([expandedGroupsPtr, groupKey, pill](bool expanded) {
      if (expanded) {
        expandedGroupsPtr->insert(groupKey);
      } else {
        expandedGroupsPtr->erase(groupKey);
      }
      if (pill != nullptr) {
        pill->setVariant(expanded ? ButtonVariant::Primary : ButtonVariant::Default);
      }
    });

    card->addChild(std::move(collapsible));
    parent.addChild(std::move(card));
    return bodyRaw;
  }

} // namespace kinetic_settings
