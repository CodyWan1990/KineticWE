#include "kinetic-settings/src/display_page.h"

#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/flex.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/select.h"
#include "ui/controls/slider.h"
#include "ui/controls/toggle.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/string_utils.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <ranges>
#include <utility>

namespace {

  constexpr float kControlWidth = 220.0F;
  constexpr float kCanvasWidth = 560.0F;
  constexpr float kCanvasHeight = 240.0F;
  constexpr float kCanvasPadding = 28.0F;
  constexpr float kSnapDistance = 18.0F;

  [[nodiscard]] std::unique_ptr<Label>
  makeLabel(std::string_view text, float size, const ColorSpec& color, FontWeight weight = FontWeight::Normal) {
    return ui::label({
        .text = std::string(text),
        .fontSize = size,
        .fontWeight = weight,
        .color = color,
    });
  }

  [[nodiscard]] std::string formatNumber(double value) {
    return std::format("{:.0f}", value);
  }

  [[nodiscard]] bool closeEnough(double left, double right) {
    return std::abs(left - right) < 0.001;
  }

  [[nodiscard]] bool sameMode(
      const DisplayOutputBackend::Mode& left, const DisplayOutputBackend::Mode& right
  ) {
    return left.width == right.width && left.height == right.height && left.refresh == right.refresh
           && left.custom == right.custom && left.reducedBlanking == right.reducedBlanking;
  }

  [[nodiscard]] std::string outputTitle(const DisplayOutputBackend::OutputInfo& output) {
    if (!output.description.empty()) {
      return output.description;
    }
    if (!output.name.empty()) {
      return output.name;
    }
    return output.id.empty() ? "Display" : output.id;
  }

  [[nodiscard]] std::string formatMode(const DisplayOutputBackend::Mode& mode) {
    if (mode.width <= 0 || mode.height <= 0) {
      return "Unknown mode";
    }
    std::string result = std::format("{}x{}", mode.width, mode.height);
    if (mode.refresh > 0) {
      result += std::format(" @ {:.2f} Hz", static_cast<double>(mode.refresh) / 1000.0);
    }
    if (mode.custom) {
      result += " (custom)";
    }
    return result;
  }

  [[nodiscard]] std::string transformLabel(std::int32_t transform) {
    switch (transform) {
    case 1:
      return "90 degrees";
    case 2:
      return "180 degrees";
    case 3:
      return "270 degrees";
    case 4:
      return "Flipped";
    case 5:
      return "Flipped 90 degrees";
    case 6:
      return "Flipped 180 degrees";
    case 7:
      return "Flipped 270 degrees";
    case 0:
    default:
      return "Normal";
    }
  }

  [[nodiscard]] std::string protocolLabel(DisplayOutputBackend::Protocol protocol) {
    switch (protocol) {
    case DisplayOutputBackend::Protocol::Kinetic:
      return "Full compositor output management";
    case DisplayOutputBackend::Protocol::Wlr:
      return "Standard output management (HDR and color controls unavailable)";
    case DisplayOutputBackend::Protocol::None:
      return "No output management protocol available";
    }
    return {};
  }

  [[nodiscard]] bool parseInt(std::string_view text, std::int32_t& result) {
    const auto trimmed = StringUtils::trim(text);
    if (trimmed.empty()) {
      return false;
    }
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const auto parsed = std::from_chars(begin, end, result);
    return parsed.ec == std::errc{} && parsed.ptr == end;
  }

  [[nodiscard]] std::unique_ptr<Node> makeNotice(std::string_view text, float scale, bool error = false) {
    auto notice = ui::column({
        .align = FlexAlign::Start,
        .gap = Style::spaceXs * scale,
        .padding = Style::spaceMd * scale,
        .fill = colorSpecFromRole(error ? ColorRole::Error : ColorRole::SurfaceVariant, error ? 0.12F : 0.45F),
        .radius = Style::scaledRadiusMd(scale),
        .fillWidth = true,
    });
    notice->addChild(makeLabel(
        text, Style::fontSizeBody * scale, colorSpecFromRole(error ? ColorRole::Error : ColorRole::OnSurfaceVariant)
    ));
    return notice;
  }

} // namespace

DisplayPage::DisplayPage(WaylandConnection& wayland) : m_backend(wayland) {}

void DisplayPage::ensureLoaded() {
  if (m_loaded) {
    return;
  }
  if (!m_backend.initialize()) {
    m_errorNotice = "The compositor did not expose a display configuration protocol.";
  } else if (!m_backend.refresh()) {
    m_errorNotice = "The compositor's display information could not be refreshed.";
  }
  m_loaded = true;

  if (m_selectedOutput.empty() || findOutput(m_selectedOutput) == nullptr) {
    const auto& outputs = m_backend.outputs();
    const auto preferred = std::ranges::find_if(outputs, [](const OutputInfo& output) {
      return output.settings.enabled;
    });
    m_selectedOutput = preferred != outputs.end() ? preferred->id : (outputs.empty() ? std::string{} : outputs.front().id);
  }
}

const DisplayPage::OutputInfo* DisplayPage::findOutput(std::string_view id) const {
  const auto& outputs = m_backend.outputs();
  const auto it = std::ranges::find_if(outputs, [id](const OutputInfo& output) {
    return output.id == id || output.name == id || output.uuid == id;
  });
  return it == outputs.end() ? nullptr : &*it;
}

DisplayPage::Settings DisplayPage::effectiveSettings(const OutputInfo& output) const {
  const auto it = m_pending.find(output.id);
  return it == m_pending.end() ? output.settings : it->second;
}

void DisplayPage::stage(Settings settings) {
  const auto* output = findOutput(settings.id);
  if (output == nullptr || settingsEqual(settings, output->settings)) {
    m_pending.erase(settings.id);
    return;
  }
  m_pending[settings.id] = std::move(settings);
}

void DisplayPage::requestRebuild(const Callbacks& callbacks) const {
  if (callbacks.requestRebuild) {
    callbacks.requestRebuild();
  }
}

std::vector<std::string> DisplayPage::resettableKeyPaths() const {
  std::vector<std::string> paths;
  paths.reserve(m_pending.size());
  for (const auto& [id, settings] : m_pending) {
    (void)settings;
    paths.push_back("output:" + id);
  }
  return paths;
}

void DisplayPage::resetKeyPath(std::string_view keyPath) {
  constexpr std::string_view prefix = "output:";
  if (keyPath.starts_with(prefix)) {
    m_pending.erase(std::string(keyPath.substr(prefix.size())));
  } else {
    resetAll();
  }
}

void DisplayPage::resetAll() {
  m_pending.clear();
  m_errorNotice.clear();
}

bool DisplayPage::applyAll() {
  ensureLoaded();
  if (m_pending.empty()) {
    return true;
  }

  std::vector<Settings> settings;
  settings.reserve(m_backend.outputs().size());
  for (const auto& output : m_backend.outputs()) {
    settings.push_back(effectiveSettings(output));
  }

  std::string error;
  if (!m_backend.apply(settings, error)) {
    m_errorNotice = error.empty() ? "The compositor rejected the display configuration." : std::move(error);
    return false;
  }
  m_pending.clear();
  m_errorNotice.clear();
  return true;
}

bool DisplayPage::settingsEqual(const Settings& left, const Settings& right) {
  if (left.id != right.id || left.enabled != right.enabled || left.x != right.x || left.y != right.y
      || left.transform != right.transform || !closeEnough(left.scale, right.scale) || !sameMode(left.mode, right.mode)
      || left.overscan != right.overscan || left.vrrPolicy != right.vrrPolicy || left.rgbRange != right.rgbRange
      || left.priority != right.priority || left.highDynamicRange != right.highDynamicRange
      || left.sdrBrightness != right.sdrBrightness || left.wideColorGamut != right.wideColorGamut
      || left.autoRotatePolicy != right.autoRotatePolicy || left.iccProfilePath != right.iccProfilePath
      || left.maxPeakBrightnessOverride != right.maxPeakBrightnessOverride
      || left.maxAverageBrightnessOverride != right.maxAverageBrightnessOverride
      || left.minBrightnessOverride != right.minBrightnessOverride || left.sdrGamutWideness != right.sdrGamutWideness
      || left.colorProfileSource != right.colorProfileSource || left.brightness != right.brightness
      || left.colorPowerTradeoff != right.colorPowerTradeoff || left.dimming != right.dimming
      || left.replicationSource != right.replicationSource || left.ddcCiAllowed != right.ddcCiAllowed
      || left.maxBitsPerColor != right.maxBitsPerColor || left.edrPolicy != right.edrPolicy
      || left.sharpness != right.sharpness || left.automaticBrightness != right.automaticBrightness
      || left.hdrIccProfilePath != right.hdrIccProfilePath || left.hdrColorProfileSource != right.hdrColorProfileSource
      || left.abmLevel != right.abmLevel || left.customModes.size() != right.customModes.size()) {
    return false;
  }
  return std::ranges::equal(left.customModes, right.customModes, sameMode);
}

std::string DisplayPage::formatMode(const DisplayOutputBackend::Mode& mode) {
  return ::formatMode(mode);
}

std::string DisplayPage::transformLabel(std::int32_t transform) {
  return ::transformLabel(transform);
}

std::int32_t DisplayPage::outputWidth(const OutputInfo& output, const Settings& settings) {
  const std::int32_t width = settings.mode.width > 0 ? settings.mode.width : 640;
  const std::int32_t height = settings.mode.height > 0 ? settings.mode.height : 360;
  const double scale = std::max(0.01, settings.scale);
  if (settings.transform == 1 || settings.transform == 3 || settings.transform == 5 || settings.transform == 7) {
    return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<double>(height) / scale)));
  }
  (void)output;
  return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<double>(width) / scale)));
}

std::int32_t DisplayPage::outputHeight(const OutputInfo& output, const Settings& settings) {
  const std::int32_t width = settings.mode.width > 0 ? settings.mode.width : 640;
  const std::int32_t height = settings.mode.height > 0 ? settings.mode.height : 360;
  const double scale = std::max(0.01, settings.scale);
  if (settings.transform == 1 || settings.transform == 3 || settings.transform == 5 || settings.transform == 7) {
    return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<double>(width) / scale)));
  }
  (void)output;
  return std::max(1, static_cast<std::int32_t>(std::lround(static_cast<double>(height) / scale)));
}

bool DisplayPage::containsInsensitive(std::string_view haystack, std::string_view needle) {
  return needle.empty() || StringUtils::containsInsensitive(haystack, needle);
}

void DisplayPage::addSectionHeader(Flex& parent, std::string_view title, float scale, bool separator) {
  if (separator) {
    parent.addChild(ui::separator());
  }
  parent.addChild(makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::Secondary), FontWeight::Bold));
}

void DisplayPage::addSettingRow(
    Flex& parent, std::string_view title, std::string_view subtitle, std::unique_ptr<Node> control, float scale
) {
  auto copy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  copy->addChild(makeLabel(title, Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold));
  if (!subtitle.empty()) {
    copy->addChild(ui::label({
        .text = std::string(subtitle),
        .fontSize = Style::fontSizeCaption * scale,
        .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
        .maxLines = 4,
    }));
  }

  auto row = ui::row({
      .align = FlexAlign::Center,
      .justify = FlexJustify::SpaceBetween,
      .gap = Style::spaceSm * scale,
      .paddingV = 2.0F * scale,
      .fillWidth = true,
  });
  row->addChild(std::move(copy));
  if (control != nullptr) {
    auto actions = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
    actions->addChild(std::move(control));
    row->addChild(std::move(actions));
  }
  parent.addChild(std::move(row));
}

std::unique_ptr<Node> DisplayPage::makeSelect(
    std::vector<std::string> options, std::optional<std::size_t> selected, float scale,
    std::function<void(std::size_t)> onChanged
) {
  return ui::select({
      .options = std::move(options),
      .selectedIndex = selected,
      .clearSelection = !selected.has_value(),
      .placeholder = selected.has_value() ? std::nullopt : std::optional<std::string>{"Unavailable"},
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .glyphSize = Style::fontSizeBody * scale,
      .width = kControlWidth * scale,
      .height = Style::controlHeight * scale,
      .onSelectionChanged = [onChanged = std::move(onChanged)](std::size_t index, std::string_view) {
        if (onChanged) {
          onChanged(index);
        }
      },
  });
}

std::unique_ptr<Node> DisplayPage::makeToggle(bool checked, float scale, std::function<void(bool)> onChanged) {
  return ui::toggle({
      .checked = checked,
      .scale = scale,
      .onChange = std::move(onChanged),
  });
}

std::unique_ptr<Node> DisplayPage::makeSlider(
    double value, double min, double max, double step, float scale, std::function<void(double)> onCommit,
    std::string suffix
) {
  min = std::min(min, max);
  max = std::max(min + step, max);
  value = std::clamp(value, min, max);

  auto row = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  Slider* sliderPtr = nullptr;
  Label* valuePtr = nullptr;
  auto slider = ui::slider({
      .out = &sliderPtr,
      .minValue = min,
      .maxValue = max,
      .step = step,
      .value = value,
      .trackHeight = Style::sliderTrackHeight * scale,
      .thumbSize = Style::sliderThumbSize * scale,
      .controlHeight = Style::controlHeight * scale,
      .width = Style::sliderDefaultWidth * scale,
  });
  auto valueLabel = ui::label({
      .out = &valuePtr,
      .text = formatNumber(value) + suffix,
      .fontSize = Style::fontSizeCaption * scale,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .minWidth = 52.0F * scale,
      .textAlign = TextAlign::End,
  });
  slider->setOnValueChanged([valuePtr, suffix](double next) {
    if (valuePtr != nullptr) {
      valuePtr->setText(formatNumber(next) + suffix);
    }
  });
  slider->setOnDragEnd([sliderPtr, onCommit = std::move(onCommit)]() {
    if (sliderPtr != nullptr && onCommit) {
      onCommit(sliderPtr->value());
    }
  });
  row->addChild(std::move(slider));
  row->addChild(std::move(valueLabel));
  return row;
}

std::unique_ptr<Node> DisplayPage::makeText(
    std::string value, float scale, std::function<void(std::string)> onSubmit
) {
  Input* inputPtr = nullptr;
  auto input = ui::input({
      .out = &inputPtr,
      .value = std::move(value),
      .fontSize = Style::fontSizeBody * scale,
      .controlHeight = Style::controlHeight * scale,
      .horizontalPadding = Style::spaceSm * scale,
      .width = kControlWidth * scale,
      .height = Style::controlHeight * scale,
      .submitOnFocusLoss = true,
  });
  input->setOnSubmit([onSubmit = std::move(onSubmit)](const std::string& text) {
    if (onSubmit) {
      onSubmit(StringUtils::trim(text));
    }
  });
  return input;
}

std::unique_ptr<Node> DisplayPage::buildLayout(float scale, const Callbacks& callbacks) {
  struct CanvasItem {
    std::string id;
    Settings settings;
    float width = 0.0F;
    float height = 0.0F;
  };

  std::vector<CanvasItem> items;
  items.reserve(m_backend.outputs().size());
  for (const auto& output : m_backend.outputs()) {
    auto settings = effectiveSettings(output);
    items.push_back(CanvasItem{
        .id = output.id,
        .settings = settings,
        .width = static_cast<float>(outputWidth(output, settings)),
        .height = static_cast<float>(outputHeight(output, settings)),
    });
  }

  auto canvas = ui::box({
      .fill = colorSpecFromRole(ColorRole::SurfaceVariant, 0.34F),
      .border = colorSpecFromRole(ColorRole::Outline, 0.45F),
      .borderWidth = 1.0F * scale,
      .radius = Style::scaledRadiusMd(scale),
      .width = kCanvasWidth * scale,
      .height = kCanvasHeight * scale,
  });
  canvas->setClipChildren(true);

  if (items.empty()) {
    auto empty = ui::column({
        .align = FlexAlign::Center,
        .justify = FlexJustify::Center,
        .width = kCanvasWidth * scale,
        .height = kCanvasHeight * scale,
    });
    empty->addChild(makeLabel(
        "No displays detected", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
    ));
    canvas->addChild(std::move(empty));
    return canvas;
  }

  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float maxY = std::numeric_limits<float>::lowest();
  for (const auto& item : items) {
    minX = std::min(minX, static_cast<float>(item.settings.x));
    minY = std::min(minY, static_cast<float>(item.settings.y));
    maxX = std::max(maxX, static_cast<float>(item.settings.x) + item.width);
    maxY = std::max(maxY, static_cast<float>(item.settings.y) + item.height);
  }
  const float modelWidth = std::max(1.0F, maxX - minX);
  const float modelHeight = std::max(1.0F, maxY - minY);
  const float availableWidth = std::max(1.0F, kCanvasWidth * scale - 2.0F * kCanvasPadding * scale);
  const float availableHeight = std::max(1.0F, kCanvasHeight * scale - 2.0F * kCanvasPadding * scale);
  const float canvasScale = std::clamp(
      std::min(availableWidth / modelWidth, availableHeight / modelHeight), 0.045F * scale, 0.65F * scale
  );
  const float originX = kCanvasPadding * scale;
  const float originY = kCanvasPadding * scale;

  const auto snapPosition = [items, canvasScale](std::string_view id, std::int32_t x, std::int32_t y, float width,
                                                  float height) {
    const float threshold = kSnapDistance / std::max(0.01F, canvasScale);
    for (const auto& other : items) {
      if (other.id == id || !other.settings.enabled) {
        continue;
      }
      const float otherX = static_cast<float>(other.settings.x);
      const float otherY = static_cast<float>(other.settings.y);
      const float otherRight = otherX + other.width;
      const float otherBottom = otherY + other.height;
      const float right = static_cast<float>(x) + width;
      const float bottom = static_cast<float>(y) + height;
      const bool verticalOverlap = bottom > otherY && static_cast<float>(y) < otherBottom;
      const bool horizontalOverlap = right > otherX && static_cast<float>(x) < otherRight;
      if (verticalOverlap && std::abs(right - otherX) <= threshold) {
        x = static_cast<std::int32_t>(std::lround(otherX - width));
      } else if (verticalOverlap && std::abs(static_cast<float>(x) - otherRight) <= threshold) {
        x = static_cast<std::int32_t>(std::lround(otherRight));
      }
      if (horizontalOverlap && std::abs(bottom - otherY) <= threshold) {
        y = static_cast<std::int32_t>(std::lround(otherY - height));
      } else if (horizontalOverlap && std::abs(static_cast<float>(y) - otherBottom) <= threshold) {
        y = static_cast<std::int32_t>(std::lround(otherBottom));
      }
    }
    return std::pair{x, y};
  };

  for (const auto& item : items) {
    const auto* output = findOutput(item.id);
    if (output == nullptr) {
      continue;
    }
    const float width = std::max(48.0F * scale, item.width * canvasScale);
    const float height = std::max(36.0F * scale, item.height * canvasScale);
    const float x = originX + (static_cast<float>(item.settings.x) - minX) * canvasScale;
    const float y = originY + (static_cast<float>(item.settings.y) - minY) * canvasScale;
    const bool selected = m_selectedOutput == item.id;

    auto area = ui::inputArea({});
    area->setPosition(x, y);
    area->setSize(width, height);
    area->setZIndex(10);
    area->setAcceptedButtons(InputArea::buttonMask(BTN_LEFT));
    auto* areaPtr = area.get();

    auto fill = ui::box({
        .fill = colorSpecFromRole(selected ? ColorRole::Primary : ColorRole::Surface, selected ? 0.82F : 0.90F),
        .border = colorSpecFromRole(selected ? ColorRole::Primary : ColorRole::Outline, selected ? 0.95F : 0.75F),
        .borderWidth = (selected ? 2.0F : 1.0F) * scale,
        .radius = Style::scaledRadiusSm(scale),
        .width = width,
        .height = height,
    });
    area->addChild(std::move(fill));

    auto labels = ui::column({
        .align = FlexAlign::Center,
        .justify = FlexJustify::Center,
        .gap = 1.0F * scale,
        .width = width,
        .height = height,
    });
    labels->addChild(makeLabel(
        outputTitle(*output), Style::fontSizeCaption * scale,
        colorSpecFromRole(selected ? ColorRole::OnPrimary : ColorRole::OnSurface), FontWeight::Bold
    ));
    labels->addChild(makeLabel(
        formatMode(item.settings.mode), Style::fontSizeMini * scale,
        colorSpecFromRole(selected ? ColorRole::OnPrimary : ColorRole::OnSurfaceVariant)
    ));
    if (!item.settings.enabled) {
      labels->addChild(makeLabel(
          "Disabled", Style::fontSizeMini * scale, colorSpecFromRole(selected ? ColorRole::OnPrimary : ColorRole::Error)
      ));
    }
    area->addChild(std::move(labels));

    const std::string id = item.id;
    const auto state = std::make_shared<DragState>();
    area->setOnPress([this, id, state, areaPtr, item, minX, minY, canvasScale, originX, originY, callbacks](
                         const InputArea::PointerData& data) {
      if (data.button != BTN_LEFT) {
        return;
      }
      if (data.pressed) {
        state->active = true;
        state->moved = false;
        state->outputId = id;
        state->pressSceneX = data.sceneX;
        state->pressSceneY = data.sceneY;
        state->startX = item.settings.x;
        state->startY = item.settings.y;
        state->previewX = state->startX;
        state->previewY = state->startY;
        m_selectedOutput = id;
        return;
      }
      if (!state->active) {
        return;
      }
      state->active = false;
      if (state->moved) {
        if (auto* currentOutput = findOutput(id); currentOutput != nullptr) {
          auto settings = effectiveSettings(*currentOutput);
          settings.x = state->previewX;
          settings.y = state->previewY;
          stage(std::move(settings));
        }
      }
      m_selectedOutput = id;
      requestRebuild(callbacks);
    });
    area->setOnMotion([this, id, state, areaPtr, item, minX, minY, canvasScale, originX, originY, snapPosition](
                          const InputArea::PointerData& data) {
      if (!state->active || !areaPtr->pressed()) {
        return;
      }
      float absoluteX = 0.0F;
      float absoluteY = 0.0F;
      Node::absolutePosition(areaPtr, absoluteX, absoluteY);
      const float sceneX = absoluteX + data.localX;
      const float sceneY = absoluteY + data.localY;
      const float deltaX = sceneX - state->pressSceneX;
      const float deltaY = sceneY - state->pressSceneY;
      if (std::max(std::abs(deltaX), std::abs(deltaY)) >= 3.0F) {
        state->moved = true;
      }
      if (!state->moved) {
        return;
      }
      auto nextX = static_cast<std::int32_t>(std::lround(
          static_cast<float>(state->startX) + deltaX / std::max(0.01F, canvasScale)
      ));
      auto nextY = static_cast<std::int32_t>(std::lround(
          static_cast<float>(state->startY) + deltaY / std::max(0.01F, canvasScale)
      ));
      nextX = std::max(0, nextX);
      nextY = std::max(0, nextY);
      const auto snapped = snapPosition(id, nextX, nextY, item.width, item.height);
      state->previewX = std::max(0, snapped.first);
      state->previewY = std::max(0, snapped.second);
      areaPtr->setPosition(
          originX + (static_cast<float>(state->previewX) - minX) * canvasScale,
          originY + (static_cast<float>(state->previewY) - minY) * canvasScale
      );
    });
    area->setOnCancel([state]() { state->active = false; });
    canvas->addChild(std::move(area));
  }
  return canvas;
}

std::unique_ptr<Node> DisplayPage::buildOutputDetails(
    const OutputInfo& output, float scale, const Filter& filter, const Callbacks& callbacks
) {
  const Settings current = effectiveSettings(output);
  const std::string id = output.id;
  const auto stageUpdate = [this, id, callbacks](auto update) {
    if (const auto* currentOutput = findOutput(id); currentOutput != nullptr) {
      auto settings = effectiveSettings(*currentOutput);
      update(settings);
      stage(std::move(settings));
      requestRebuild(callbacks);
    }
  };

  auto card = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceMd * scale,
      .fillWidth = true,
      .configure = [scale](Flex& section) { section.setCardStyle(scale); },
  });

  auto header = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale, .fillWidth = true});
  header->addChild(ui::glyph({
      .glyph = "device-desktop",
      .glyphSize = Style::fontSizeHeader * scale,
      .color = colorSpecFromRole(ColorRole::Primary),
  }));
  auto headerCopy = ui::column({.align = FlexAlign::Start, .gap = Style::spaceXs * scale, .flexGrow = 1.0F});
  headerCopy->addChild(makeLabel(
      outputTitle(output), Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
  ));
  std::string identity = output.name;
  if (!output.make.empty() || !output.model.empty()) {
    if (!identity.empty()) {
      identity += " · ";
    }
    identity += output.make + " " + output.model;
  }
  if (identity.empty()) {
    identity = output.id;
  }
  headerCopy->addChild(makeLabel(identity, Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)));
  header->addChild(std::move(headerCopy));
  if (m_pending.contains(id)) {
    header->addChild(ui::label({
        .text = "Pending",
        .fontSize = Style::fontSizeCaption * scale,
        .fontWeight = FontWeight::Bold,
        .color = colorSpecFromRole(ColorRole::Primary),
    }));
  }
  card->addChild(std::move(header));

  addSectionHeader(*card, "Display", scale, true);
  addSettingRow(
      *card, "Enabled", "Whether this output participates in the desktop layout", makeToggle(
          current.enabled, scale, [stageUpdate](bool value) {
            stageUpdate([value](Settings& settings) { settings.enabled = value; });
          }
      ),
      scale
  );

  std::vector<std::string> modeOptions;
  std::vector<DisplayOutputBackend::Mode> modeValues;
  for (const auto& mode : output.modes) {
    if (std::ranges::any_of(modeValues, [&mode](const auto& existing) { return sameMode(existing, mode); })) {
      continue;
    }
    modeOptions.push_back(formatMode(mode));
    modeValues.push_back(mode);
  }
  std::optional<std::size_t> modeIndex;
  for (std::size_t index = 0; index < modeValues.size(); ++index) {
    if (sameMode(modeValues[index], current.mode)) {
      modeIndex = index;
      break;
    }
  }
  if (modeOptions.empty()) {
    modeOptions.push_back("No modes reported");
  }
  addSettingRow(
      *card, "Resolution and refresh rate", "Choose one of the modes advertised by the display", makeSelect(
          std::move(modeOptions), modeIndex, scale, [stageUpdate, modeValues](std::size_t index) {
            if (index < modeValues.size()) {
              stageUpdate([mode = modeValues[index]](Settings& settings) { settings.mode = mode; });
            }
          }
      ),
      scale
  );

  const std::vector<std::int32_t> transforms{0, 1, 2, 3, 4, 5, 6, 7};
  std::vector<std::string> transformOptions;
  transformOptions.reserve(transforms.size());
  for (const auto transform : transforms) {
    transformOptions.push_back(transformLabel(transform));
  }
  const auto transformIt = std::ranges::find(transforms, current.transform);
  addSettingRow(
      *card, "Orientation", "Rotate or flip the output", makeSelect(
          std::move(transformOptions),
          transformIt == transforms.end() ? std::nullopt
                                           : std::optional<std::size_t>{static_cast<std::size_t>(transformIt - transforms.begin())},
          scale, [stageUpdate, transforms](std::size_t index) {
            if (index < transforms.size()) {
              stageUpdate([transform = transforms[index]](Settings& settings) { settings.transform = transform; });
            }
          }
      ),
      scale
  );

  std::vector<double> scales{1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
  if (!std::ranges::any_of(scales, [current](double value) { return closeEnough(value, current.scale); })) {
    scales.push_back(current.scale);
    std::ranges::sort(scales);
  }
  std::vector<std::string> scaleOptions;
  for (const double value : scales) {
    scaleOptions.push_back(std::format("{}%", static_cast<int>(std::lround(value * 100.0))));
  }
  const auto scaleIt = std::ranges::find_if(scales, [current](double value) { return closeEnough(value, current.scale); });
  addSettingRow(
      *card, "Scale", "Logical scaling applied to this output", makeSelect(
          std::move(scaleOptions),
          scaleIt == scales.end() ? std::nullopt
                                   : std::optional<std::size_t>{static_cast<std::size_t>(scaleIt - scales.begin())},
          scale, [stageUpdate, scales](std::size_t index) {
            if (index < scales.size()) {
              stageUpdate([value = scales[index]](Settings& settings) { settings.scale = value; });
            }
          }
      ),
      scale
  );

  if (output.supports(DisplayOutputBackend::Capability::Vrr)) {
    const bool kinetic = m_backend.protocol() == DisplayOutputBackend::Protocol::Kinetic;
    const std::vector<std::uint32_t> values = kinetic ? std::vector<std::uint32_t>{0, 1, 2}
                                                       : std::vector<std::uint32_t>{0, 1};
    const std::vector<std::string> labels = kinetic ? std::vector<std::string>{"Never", "Always", "Automatic"}
                                                     : std::vector<std::string>{"Never", "Always"};
    const auto selected = std::ranges::find(values, current.vrrPolicy);
    addSettingRow(
        *card, "Variable refresh rate", "Choose when adaptive sync may be used", makeSelect(
            labels, selected == values.end() ? std::nullopt
                                              : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
            scale, [stageUpdate, values](std::size_t index) {
              if (index < values.size()) {
                stageUpdate([value = values[index]](Settings& settings) { settings.vrrPolicy = value; });
              }
            }
        ),
        scale
    );
  }

  if (output.supports(DisplayOutputBackend::Capability::RgbRange)) {
    const std::vector<std::uint32_t> values{0, 1, 2};
    const std::vector<std::string> labels{"Automatic", "Full", "Limited"};
    const auto selected = std::ranges::find(values, current.rgbRange);
    addSettingRow(
        *card, "RGB range", "Signal range sent to the display", makeSelect(
            labels, selected == values.end() ? std::nullopt
                                              : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
            scale, [stageUpdate, values](std::size_t index) {
              if (index < values.size()) {
                stageUpdate([value = values[index]](Settings& settings) { settings.rgbRange = value; });
              }
            }
        ),
        scale
    );
  }

  if (m_backend.protocol() == DisplayOutputBackend::Protocol::Kinetic) {
    std::vector<std::string> mirrorOptions{"No mirroring"};
    std::vector<std::string> mirrorValues{""};
    for (const auto& candidate : m_backend.outputs()) {
      if (candidate.id == output.id) {
        continue;
      }
      mirrorOptions.push_back(outputTitle(candidate));
      mirrorValues.push_back(candidate.uuid.empty() ? candidate.id : candidate.uuid);
    }
    const auto selected = std::ranges::find(mirrorValues, current.replicationSource);
    addSettingRow(
        *card, "Mirror this display", "Show the selected output's viewport on this display", makeSelect(
            std::move(mirrorOptions), selected == mirrorValues.end()
                ? std::nullopt
                : std::optional<std::size_t>{static_cast<std::size_t>(selected - mirrorValues.begin())},
            scale, [stageUpdate, mirrorValues](std::size_t index) {
              if (index < mirrorValues.size()) {
                stageUpdate([value = mirrorValues[index]](Settings& settings) { settings.replicationSource = value; });
              }
            }
        ),
        scale
    );
  }

  addSectionHeader(*card, "Color and HDR", scale, true);
  if (output.supports(DisplayOutputBackend::Capability::HighDynamicRange)) {
    addSettingRow(
        *card, "High dynamic range", "Enable HDR output and HDR-aware color processing", makeToggle(
            current.highDynamicRange, scale, [stageUpdate](bool value) {
              stageUpdate([value](Settings& settings) { settings.highDynamicRange = value; });
            }
        ),
        scale
    );
    addSettingRow(
        *card, "SDR brightness", "Reference brightness for SDR content while HDR is enabled", makeSlider(
            current.sdrBrightness, 80.0, std::max(1000.0, static_cast<double>(current.sdrBrightness)), 1.0, scale,
            [stageUpdate](double value) {
              stageUpdate([value](Settings& settings) { settings.sdrBrightness = static_cast<std::uint32_t>(std::lround(value)); });
            },
            " nits"
        ),
        scale
    );
  }
  if (output.supports(DisplayOutputBackend::Capability::WideColorGamut)) {
    addSettingRow(
        *card, "Wide color gamut", "Use the display's wider native gamut when available", makeToggle(
            current.wideColorGamut, scale, [stageUpdate](bool value) {
              stageUpdate([value](Settings& settings) { settings.wideColorGamut = value; });
            }
        ),
        scale
    );
  }
  if (output.supports(DisplayOutputBackend::Capability::Brightness)) {
    addSettingRow(
          *card, "Brightness", "Output brightness multiplier", makeSlider(
            static_cast<double>(current.brightness) / 100.0, 0.0, 100.0, 1.0, scale, [stageUpdate](double value) {
              stageUpdate([value](Settings& settings) {
                settings.brightness = static_cast<std::uint32_t>(std::lround(value * 100.0));
              });
            },
            "%"
        ),
        scale
    );
  }

  if (filter.showAdvanced) {
    addSectionHeader(*card, "Advanced color settings", scale, true);
    if (m_backend.supportsKineticFeature(8)) {
      const std::vector<std::uint32_t> values{0, 1, 2};
      const std::vector<std::string> labels{"sRGB", "ICC profile", "EDID"};
      const auto selected = std::ranges::find(values, current.colorProfileSource);
      addSettingRow(
          *card, "SDR color profile source", "Select the source used to describe this output's SDR colors", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.colorProfileSource = value; });
                }
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::IccProfile)) {
      addSettingRow(
          *card, "SDR ICC profile path", "Absolute path to an ICC profile; leave empty to use the compositor default", makeText(
              current.iccProfilePath, scale, [stageUpdate](std::string value) {
                stageUpdate([value = std::move(value)](Settings& settings) { settings.iccProfilePath = value; });
              }
          ),
          scale
      );
    }
    if (m_backend.supportsKineticFeature(7)) {
      addSettingRow(
          *card, "SDR gamut wideness", "How far sRGB content may use the display's native gamut", makeSlider(
              static_cast<double>(current.sdrGamutWideness) / 100.0, 0.0, 100.0, 1.0, scale, [stageUpdate](double value) {
                stageUpdate([value](Settings& settings) {
                  settings.sdrGamutWideness = static_cast<std::uint32_t>(std::lround(value * 100.0));
                });
              },
              "%"
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::HdrIccProfile)) {
      const std::vector<std::uint32_t> values{0, 1, 2};
      const std::vector<std::string> labels{"sRGB", "ICC profile", "EDID"};
      const auto selected = std::ranges::find(values, current.hdrColorProfileSource);
      addSettingRow(
          *card, "HDR color profile source", "Select the source used while HDR is enabled", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.hdrColorProfileSource = value; });
                }
              }
          ),
          scale
      );
      addSettingRow(
          *card, "HDR ICC profile path", "Absolute path to an ICC profile used in HDR mode", makeText(
              current.hdrIccProfilePath, scale, [stageUpdate](std::string value) {
                stageUpdate([value = std::move(value)](Settings& settings) { settings.hdrIccProfilePath = value; });
              }
          ),
          scale
      );
    }
    if (m_backend.supportsKineticFeature(10)) {
      const std::vector<std::uint32_t> values{0, 1};
      const std::vector<std::string> labels{"Efficiency", "Color accuracy"};
      const auto selected = std::ranges::find(values, current.colorPowerTradeoff);
      addSettingRow(
          *card, "Color and power preference", "Choose between efficiency and color accuracy", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.colorPowerTradeoff = value; });
                }
              }
          ),
          scale
      );
    }

    addSectionHeader(*card, "Hardware and policy", scale, true);
    if (output.supports(DisplayOutputBackend::Capability::Overscan)) {
      addSettingRow(
          *card, "Overscan", "Crop the image by this percentage", makeSlider(
              current.overscan, 0.0, 100.0, 1.0, scale, [stageUpdate](double value) {
                stageUpdate([value](Settings& settings) { settings.overscan = static_cast<std::uint32_t>(std::lround(value)); });
              },
              "%"
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::AutoRotate)) {
      const std::vector<std::uint32_t> values{0, 1, 2};
      const std::vector<std::string> labels{"Never", "In tablet mode", "Always"};
      const auto selected = std::ranges::find(values, current.autoRotatePolicy);
      addSettingRow(
          *card, "Automatic rotation", "When the compositor may rotate this output from sensor input", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.autoRotatePolicy = value; });
                }
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::DdcCi)) {
      addSettingRow(
          *card, "Allow DDC/CI", "Allow hardware brightness and display controls through DDC/CI", makeToggle(
              current.ddcCiAllowed, scale, [stageUpdate](bool value) {
                stageUpdate([value](Settings& settings) { settings.ddcCiAllowed = value; });
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::MaxBitsPerColor)) {
      std::vector<std::uint32_t> values{0};
      std::vector<std::string> labels{"Automatic"};
      const std::uint32_t minBpc = output.maxBitsPerColorMin == 0 ? 8 : output.maxBitsPerColorMin;
      const std::uint32_t maxBpc = output.maxBitsPerColorMax < minBpc ? 12 : output.maxBitsPerColorMax;
      for (std::uint32_t bpc = minBpc; bpc <= maxBpc && bpc < 32; ++bpc) {
        values.push_back(bpc);
        labels.push_back(std::format("{} bits", bpc));
      }
      const auto selected = std::ranges::find(values, current.maxBitsPerColor);
      addSettingRow(
          *card, "Maximum bits per color", "Limit the link color depth; automatic is recommended", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.maxBitsPerColor = value; });
                }
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::Edr)) {
      const std::vector<std::uint32_t> values{0, 1};
      const std::vector<std::string> labels{"Never", "Always"};
      const auto selected = std::ranges::find(values, current.edrPolicy);
      addSettingRow(
          *card, "Extended dynamic range", "Allow extra backlight for HDR content on displays without native HDR", makeSelect(
              labels, selected == values.end() ? std::nullopt
                                                : std::optional<std::size_t>{static_cast<std::size_t>(selected - values.begin())},
              scale, [stageUpdate, values](std::size_t index) {
                if (index < values.size()) {
                  stageUpdate([value = values[index]](Settings& settings) { settings.edrPolicy = value; });
                }
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::Sharpness)) {
      addSettingRow(
          *card, "Sharpness", "Post-processing sharpness strength", makeSlider(
              static_cast<double>(current.sharpness) / 100.0, 0.0, 100.0, 1.0, scale, [stageUpdate](double value) {
                stageUpdate([value](Settings& settings) {
                  settings.sharpness = static_cast<std::uint32_t>(std::lround(value * 100.0));
                });
              },
              "%"
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::AutoBrightness)) {
      addSettingRow(
          *card, "Automatic brightness", "Follow the ambient light sensor when supported", makeToggle(
              current.automaticBrightness, scale, [stageUpdate](bool value) {
                stageUpdate([value](Settings& settings) { settings.automaticBrightness = value; });
              }
          ),
          scale
      );
    }
    if (output.supports(DisplayOutputBackend::Capability::AbmLevel)) {
      std::vector<std::string> labels{"Off", "Low", "Medium", "High", "Maximum"};
      addSettingRow(
          *card, "Adaptive backlight modulation", "Trade contrast for lower power use", makeSelect(
              labels, current.abmLevel <= 4 ? std::optional<std::size_t>{current.abmLevel} : std::nullopt, scale,
              [stageUpdate](std::size_t index) {
                if (index <= 4) {
                  stageUpdate([index](Settings& settings) { settings.abmLevel = static_cast<std::uint32_t>(index); });
                }
              }
          ),
          scale
      );
    }
    if (m_backend.supportsKineticFeature(11)) {
      addSettingRow(
          *card, "Dimming", "Temporary output dimming multiplier", makeSlider(
              static_cast<double>(current.dimming) / 100.0, 0.0, 100.0, 1.0, scale, [stageUpdate](double value) {
                stageUpdate([value](Settings& settings) {
                  settings.dimming = static_cast<std::uint32_t>(std::lround(value * 100.0));
                });
              },
              "%"
          ),
          scale
      );
    }
    if (m_backend.supportsKineticFeature(3)) {
      const std::uint32_t maxPriority = std::max<std::uint32_t>(
          static_cast<std::uint32_t>(m_backend.outputs().size() + 1), current.priority + 1
      );
      addSettingRow(
          *card, "Output priority", "Lower values are placed earlier in the compositor's output order", makeSlider(
              current.priority, 0.0, static_cast<double>(maxPriority), 1.0, scale, [stageUpdate](double value) {
                stageUpdate([value](Settings& settings) { settings.priority = static_cast<std::uint32_t>(std::lround(value)); });
              }
          ),
          scale
      );
    }

    if (m_backend.supportsKineticFeature(7)) {
      addSettingRow(
          *card, "Maximum peak brightness override", "Override the panel metadata in nits; use -1 for automatic", makeText(
              std::to_string(current.maxPeakBrightnessOverride), scale, [stageUpdate](std::string value) {
                std::int32_t parsed = 0;
                if (parseInt(value, parsed)) {
                  stageUpdate([parsed](Settings& settings) { settings.maxPeakBrightnessOverride = parsed; });
                }
              }
          ),
          scale
      );
      addSettingRow(
          *card, "Maximum frame-average brightness", "Override the panel metadata in nits; use -1 for automatic", makeText(
              std::to_string(current.maxAverageBrightnessOverride), scale, [stageUpdate](std::string value) {
                std::int32_t parsed = 0;
                if (parseInt(value, parsed)) {
                  stageUpdate([parsed](Settings& settings) { settings.maxAverageBrightnessOverride = parsed; });
                }
              }
          ),
          scale
      );
      addSettingRow(
          *card, "Minimum brightness override", "Override the panel minimum in 0.0001 nits; use -1 for automatic", makeText(
              std::to_string(current.minBrightnessOverride), scale, [stageUpdate](std::string value) {
                std::int32_t parsed = 0;
                if (parseInt(value, parsed)) {
                  stageUpdate([parsed](Settings& settings) { settings.minBrightnessOverride = parsed; });
                }
              }
          ),
          scale
      );
    }

    if (output.supports(DisplayOutputBackend::Capability::CustomModes)) {
      addSectionHeader(*card, "Custom modes", scale, true);
      if (current.customModes.empty()) {
        card->addChild(makeLabel(
            "No custom modes configured.", Style::fontSizeCaption * scale,
            colorSpecFromRole(ColorRole::OnSurfaceVariant)
        ));
      } else {
        for (const auto& mode : current.customModes) {
          card->addChild(makeLabel(
              formatMode(mode), Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
          ));
        }
      }
      auto editor = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
      Input* widthInput = nullptr;
      Input* heightInput = nullptr;
      Input* refreshInput = nullptr;
      editor->addChild(ui::input({
          .out = &widthInput,
          .placeholder = "Width",
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeightSm * scale,
          .width = 78.0F * scale,
          .height = Style::controlHeightSm * scale,
      }));
      editor->addChild(ui::input({
          .out = &heightInput,
          .placeholder = "Height",
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeightSm * scale,
          .width = 78.0F * scale,
          .height = Style::controlHeightSm * scale,
      }));
      editor->addChild(ui::input({
          .out = &refreshInput,
          .placeholder = "mHz",
          .fontSize = Style::fontSizeCaption * scale,
          .controlHeight = Style::controlHeightSm * scale,
          .width = 78.0F * scale,
          .height = Style::controlHeightSm * scale,
      }));
      editor->addChild(ui::button({
          .text = "Add",
          .fontSize = Style::fontSizeCaption * scale,
          .variant = ButtonVariant::Secondary,
          .minHeight = Style::controlHeightSm * scale,
          .paddingV = Style::spaceXs * scale,
          .paddingH = Style::spaceSm * scale,
          .radius = Style::scaledRadiusMd(scale),
          .onClick = [stageUpdate, widthInput, heightInput, refreshInput]() {
            if (widthInput == nullptr || heightInput == nullptr || refreshInput == nullptr) {
              return;
            }
            DisplayOutputBackend::Mode mode;
            if (!parseInt(widthInput->value(), mode.width) || !parseInt(heightInput->value(), mode.height)
                || !parseInt(refreshInput->value(), mode.refresh) || mode.width <= 0 || mode.height <= 0
                || mode.refresh <= 0) {
              return;
            }
            mode.custom = true;
            stageUpdate([mode](Settings& settings) {
              if (!std::ranges::any_of(settings.customModes, [mode](const auto& existing) { return sameMode(existing, mode); })) {
                settings.customModes.push_back(mode);
              }
            });
          },
      }));
      card->addChild(std::move(editor));
    }
  }

  return card;
}

std::unique_ptr<Node>
DisplayPage::build(float scale, const Filter& filter, std::string_view /*armedResetKey*/, const Callbacks& callbacks) {
  ensureLoaded();
  auto root = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceLg * scale,
      .fill = clearColorSpec(),
      .fillWidth = true,
  });

  auto heading = ui::row({.align = FlexAlign::Center, .gap = Style::spaceSm * scale});
  heading->addChild(ui::glyph({
      .glyph = "device-desktop",
      .glyphSize = Style::fontSizeHeader * scale,
      .color = colorSpecFromRole(ColorRole::Primary),
  }));
  heading->addChild(makeLabel(
      "Displays", Style::fontSizeHeader * scale, colorSpecFromRole(ColorRole::Primary), FontWeight::Bold
  ));
  root->addChild(std::move(heading));
  root->addChild(makeLabel(
      protocolLabel(m_backend.protocol()), Style::fontSizeCaption * scale,
      colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));
  if (!m_errorNotice.empty()) {
    root->addChild(makeNotice(m_errorNotice, scale, true));
  }

  if (m_backend.outputs().empty()) {
    root->addChild(makeNotice(
        "No display outputs are available. Connect a display or use a compositor with output management support.",
        scale
    ));
    return root;
  }

  auto layoutCard = ui::column({
      .align = FlexAlign::Stretch,
      .gap = Style::spaceSm * scale,
      .padding = Style::spaceMd * scale,
      .fillWidth = true,
      .configure = [scale](Flex& section) { section.setCardStyle(scale); },
  });
  layoutCard->addChild(makeLabel(
      "Arrange displays", Style::fontSizeBody * scale, colorSpecFromRole(ColorRole::OnSurface), FontWeight::Bold
  ));
  layoutCard->addChild(makeLabel(
      "Drag a display to move it. Edges snap together; changes are sent only after Apply.",
      Style::fontSizeCaption * scale, colorSpecFromRole(ColorRole::OnSurfaceVariant)
  ));
  layoutCard->addChild(buildLayout(scale, callbacks));
  root->addChild(std::move(layoutCard));

  const auto* selected = findOutput(m_selectedOutput);
  if (selected == nullptr) {
    root->addChild(makeNotice("Select a display to edit its settings.", scale));
    return root;
  }
  if (filter.showOverriddenOnly && m_pending.empty()) {
    root->addChild(makeNotice("No display changes are pending.", scale));
    return root;
  }
  if (!filter.searchQuery.empty()
      && !containsInsensitive(outputTitle(*selected), filter.searchQuery)
      && !containsInsensitive("display resolution HDR color scale rotation", filter.searchQuery)) {
    root->addChild(makeNotice("No display settings match the current search.", scale));
    return root;
  }
  root->addChild(buildOutputDetails(*selected, scale, filter, callbacks));
  return root;
}
