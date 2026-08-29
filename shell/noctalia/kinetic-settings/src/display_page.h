#pragma once

#include "kinetic-settings/src/display_backend.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Flex;
class Node;
class WaylandConnection;

// Display settings backed by the compositor's Wayland output-management
// protocol. Changes stay in memory until the window's Apply button is pressed;
// the compositor receives one atomic configuration rather than one request per
// control.
class DisplayPage {
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

  explicit DisplayPage(WaylandConnection& wayland);

  std::unique_ptr<Node>
  build(float scale, const Filter& filter, std::string_view armedResetKey, const Callbacks& callbacks);

  [[nodiscard]] std::vector<std::string> resettableKeyPaths() const;
  void resetKeyPath(std::string_view keyPath);
  void resetAll();

  [[nodiscard]] bool hasPending() const noexcept { return !m_pending.empty(); }
  [[nodiscard]] bool applyAll();

  void reloadFromDisk() noexcept {
    m_pending.clear();
    m_errorNotice.clear();
    m_loaded = false;
  }

  [[nodiscard]] std::string_view lastError() const noexcept { return m_errorNotice; }
  void setOutputChangeCallback(DisplayOutputBackend::ChangeCallback callback) {
    m_backend.setChangeCallback(std::move(callback));
  }

private:
  using Settings = DisplayOutputBackend::OutputSettings;
  using OutputInfo = DisplayOutputBackend::OutputInfo;

  struct DragState {
    bool active = false;
    bool moved = false;
    std::string outputId;
    float pressSceneX = 0.0F;
    float pressSceneY = 0.0F;
    std::int32_t startX = 0;
    std::int32_t startY = 0;
    std::int32_t previewX = 0;
    std::int32_t previewY = 0;
  };

  void ensureLoaded();
  [[nodiscard]] const OutputInfo* findOutput(std::string_view id) const;
  [[nodiscard]] Settings effectiveSettings(const OutputInfo& output) const;
  void stage(Settings settings);
  void requestRebuild(const Callbacks& callbacks) const;

  [[nodiscard]] std::unique_ptr<Node> buildLayout(float scale, const Callbacks& callbacks);
  [[nodiscard]] std::unique_ptr<Node>
  buildOutputDetails(const OutputInfo& output, float scale, const Filter& filter, const Callbacks& callbacks);

  static void addSectionHeader(Flex& parent, std::string_view title, float scale, bool separator);
  static void addSettingRow(Flex& parent, std::string_view title, std::string_view subtitle,
                            std::unique_ptr<Node> control, float scale);
  static std::unique_ptr<Node> makeSelect(
      std::vector<std::string> options, std::optional<std::size_t> selected, float scale,
      std::function<void(std::size_t)> onChanged);
  static std::unique_ptr<Node> makeToggle(bool checked, float scale, std::function<void(bool)> onChanged);
  static std::unique_ptr<Node> makeSlider(
      double value, double min, double max, double step, float scale, std::function<void(double)> onCommit,
      std::string suffix = {});
  static std::unique_ptr<Node> makeText(std::string value, float scale, std::function<void(std::string)> onSubmit);

  [[nodiscard]] static bool settingsEqual(const Settings& left, const Settings& right);
  [[nodiscard]] static std::string formatMode(const DisplayOutputBackend::Mode& mode);
  [[nodiscard]] static std::string transformLabel(std::int32_t transform);
  [[nodiscard]] static std::int32_t outputWidth(const OutputInfo& output, const Settings& settings);
  [[nodiscard]] static std::int32_t outputHeight(const OutputInfo& output, const Settings& settings);
  [[nodiscard]] static bool containsInsensitive(std::string_view haystack, std::string_view needle);

  DisplayOutputBackend m_backend;
  std::map<std::string, Settings> m_pending;
  std::string m_selectedOutput;
  std::string m_errorNotice;
  bool m_loaded = false;
};
