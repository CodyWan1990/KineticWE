#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class WaylandConnection;

// Wayland client for output enumeration and atomic output configuration. The
// KDE protocol name is historical: this talks directly to the compositor over
// wayland-client and does not require Qt, KDE Frameworks, Plasma, or a KDE
// session service.
class DisplayOutputBackend {
public:
  enum class Protocol : std::uint8_t {
    None,
    Wlr,
    Kinetic,
  };

  enum class Capability : std::uint32_t {
    Overscan = 1U << 0,
    Vrr = 1U << 1,
    RgbRange = 1U << 2,
    HighDynamicRange = 1U << 3,
    WideColorGamut = 1U << 4,
    AutoRotate = 1U << 5,
    IccProfile = 1U << 6,
    Brightness = 1U << 7,
    BuiltInColor = 1U << 8,
    DdcCi = 1U << 9,
    MaxBitsPerColor = 1U << 10,
    Edr = 1U << 11,
    Sharpness = 1U << 12,
    CustomModes = 1U << 13,
    AutoBrightness = 1U << 14,
    HdrIccProfile = 1U << 15,
    AbmLevel = 1U << 16,
  };

  struct Mode {
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::int32_t refresh = 0; // milli-Hz
    bool preferred = false;
    bool custom = false;
    bool reducedBlanking = false;
  };

  // This is intentionally a value type. DisplayPage can stage a complete
  // output state without retaining raw Wayland proxy pointers.
  struct OutputSettings {
    std::string id;
    bool enabled = true;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t transform = 0;
    double scale = 1.0;
    Mode mode;

    std::uint32_t overscan = 0;
    std::uint32_t vrrPolicy = 2; // automatic
    std::uint32_t rgbRange = 0;  // automatic
    std::uint32_t priority = 0;

    bool highDynamicRange = false;
    std::uint32_t sdrBrightness = 200;
    bool wideColorGamut = false;
    std::uint32_t autoRotatePolicy = 0;
    std::string iccProfilePath;
    std::int32_t maxPeakBrightnessOverride = -1;
    std::int32_t maxAverageBrightnessOverride = -1;
    std::int32_t minBrightnessOverride = -1;
    std::uint32_t sdrGamutWideness = 0;
    std::uint32_t colorProfileSource = 2; // EDID
    std::uint32_t brightness = 10000;
    std::uint32_t colorPowerTradeoff = 0;
    std::uint32_t dimming = 10000;
    std::string replicationSource;
    bool ddcCiAllowed = true;
    std::uint32_t maxBitsPerColor = 0;
    std::uint32_t edrPolicy = 0;
    std::uint32_t sharpness = 0;
    bool automaticBrightness = false;
    std::string hdrIccProfilePath;
    std::uint32_t hdrColorProfileSource = 2; // EDID
    std::uint32_t abmLevel = 0;
    std::vector<Mode> customModes;
  };

  struct OutputInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string make;
    std::string model;
    std::string serialNumber;
    std::string uuid;
    std::int32_t physicalWidth = 0;
    std::int32_t physicalHeight = 0;
    std::uint32_t capabilities = 0;
    std::uint32_t maxBitsPerColorMin = 0;
    std::uint32_t maxBitsPerColorMax = 0;
    std::uint32_t maxBitsPerColorAutomaticLimit = 0;
    std::uint32_t maxPeakBrightness = 0;
    std::uint32_t maxAverageBrightness = 0;
    std::uint32_t minBrightness = 0;
    OutputSettings settings;
    std::vector<Mode> modes;

    [[nodiscard]] bool supports(Capability capability) const noexcept {
      return (capabilities & static_cast<std::uint32_t>(capability)) != 0;
    }
  };

  using ChangeCallback = std::function<void()>;

  class Private;

  explicit DisplayOutputBackend(WaylandConnection& wayland);
  ~DisplayOutputBackend();

  DisplayOutputBackend(const DisplayOutputBackend&) = delete;
  DisplayOutputBackend& operator=(const DisplayOutputBackend&) = delete;

  // Binds the available output-management protocol and performs the initial
  // roundtrips needed to populate outputs and modes.
  [[nodiscard]] bool initialize();
  // Pumps current output events. Useful after closing and reopening settings.
  [[nodiscard]] bool refresh();

  [[nodiscard]] bool initialized() const noexcept { return m_initialized; }
  [[nodiscard]] Protocol protocol() const noexcept;
  [[nodiscard]] bool supportsKineticFeature(std::uint32_t version) const noexcept;
  [[nodiscard]] const std::vector<OutputInfo>& outputs() const noexcept { return m_outputs; }

  // Applies a complete output configuration atomically. `settings` must have
  // one entry for each currently known output. The error is populated on
  // protocol rejection or compositor failure.
  [[nodiscard]] bool apply(const std::vector<OutputSettings>& settings, std::string& error);

  void setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

private:
  std::unique_ptr<Private> m_private;
  std::vector<OutputInfo> m_outputs;
  ChangeCallback m_changeCallback;
  bool m_initialized = false;
};
