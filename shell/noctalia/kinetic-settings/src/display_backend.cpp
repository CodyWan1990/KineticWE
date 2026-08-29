#include "kinetic-settings/src/display_backend.h"

#include "core/log.h"
#include "wayland/wayland_connection.h"

#include "kde-output-device-v2-client-protocol.h"
#include "kde-output-management-v2-client-protocol.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

  constexpr Logger kLog("display-settings");
  constexpr std::uint32_t kKdeDeviceVersion = 24;
  constexpr std::uint32_t kKdeManagementVersion = 22;
  constexpr std::uint32_t kKdeRegistryMinimumVersion = 21;
  constexpr std::uint32_t kWlrManagerVersion = 4;
  constexpr std::uint32_t kWlrManagerMinimumVersion = 3;

  constexpr std::uint32_t kKdeModeCustom = 0x1;
  constexpr std::uint32_t kKdeModeReducedBlanking = 0x2;

  [[nodiscard]] std::string waylandError() {
    const int value = errno;
    const char* text = std::strerror(value);
    return text != nullptr ? std::string(text) : std::string("unknown Wayland error");
  }

  [[nodiscard]] bool sameMode(const DisplayOutputBackend::Mode& left, const DisplayOutputBackend::Mode& right) {
    return left.width == right.width && left.height == right.height && left.refresh == right.refresh
           && left.custom == right.custom && left.reducedBlanking == right.reducedBlanking;
  }

} // namespace

class DisplayOutputBackend::Private {
public:
  explicit Private(DisplayOutputBackend& backend, WaylandConnection& wayland)
      : owner(backend), display(wayland.display()) {}

  ~Private() { cleanup(); }

  struct KdeMode {
    kde_output_device_mode_v2* proxy = nullptr;
    DisplayOutputBackend::Mode info;
  };

  struct KdeOutput {
    kde_output_device_v2* proxy = nullptr;
    kde_output_device_mode_v2* currentMode = nullptr;
    DisplayOutputBackend::OutputInfo info;
    std::vector<std::unique_ptr<KdeMode>> modes;
  };

  struct WlrMode {
    zwlr_output_mode_v1* proxy = nullptr;
    DisplayOutputBackend::Mode info;
  };

  struct WlrOutput {
    zwlr_output_head_v1* proxy = nullptr;
    zwlr_output_mode_v1* currentMode = nullptr;
    DisplayOutputBackend::OutputInfo info;
    std::vector<std::unique_ptr<WlrMode>> modes;
  };

  DisplayOutputBackend& owner;
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  kde_output_device_registry_v2* kdeRegistry = nullptr;
  kde_output_management_v2* kdeManagement = nullptr;
  zwlr_output_manager_v1* wlrManager = nullptr;
  std::uint32_t wlrSerial = 0;
  bool wlrSerialReady = false;
  bool applySucceeded = false;
  bool applyFinished = false;
  std::string applyFailure;

  std::vector<std::unique_ptr<KdeOutput>> kdeOutputs;
  std::unordered_map<kde_output_device_v2*, KdeOutput*> kdeByProxy;
  std::unordered_map<kde_output_device_mode_v2*, KdeOutput*> kdeModeOwners;

  std::vector<std::unique_ptr<WlrOutput>> wlrOutputs;
  std::unordered_map<zwlr_output_head_v1*, WlrOutput*> wlrByProxy;
  std::unordered_map<zwlr_output_mode_v1*, WlrOutput*> wlrModeOwners;

  static void global(
      void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version
  );
  static void globalRemove(void* data, wl_registry* registry, std::uint32_t name);

  static void kdeRegistryFinished(void*, kde_output_device_registry_v2*) {}
  static void kdeRegistryOutput(void* data, kde_output_device_registry_v2*, kde_output_device_v2* output);
  static void kdeGeometry(
      void* data, kde_output_device_v2* output, std::int32_t x, std::int32_t y, std::int32_t physicalWidth,
      std::int32_t physicalHeight, std::int32_t, const char* make, const char* model, std::int32_t transform
  );
  static void kdeCurrentMode(void* data, kde_output_device_v2* output, kde_output_device_mode_v2* mode);
  static void kdeMode(void* data, kde_output_device_v2* output, kde_output_device_mode_v2* mode);
  static void kdeDone(void* data, kde_output_device_v2* output);
  static void kdeScale(void* data, kde_output_device_v2* output, wl_fixed_t factor);
  static void kdeEnabled(void* data, kde_output_device_v2* output, std::int32_t enabled);
  static void kdeUuid(void* data, kde_output_device_v2* output, const char* uuid);
  static void kdeSerial(void* data, kde_output_device_v2* output, const char* serial);
  static void kdeCapabilities(void* data, kde_output_device_v2* output, std::uint32_t capabilities);
  static void kdeOverscan(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeVrrPolicy(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeRgbRange(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeName(void* data, kde_output_device_v2* output, const char* name);
  static void kdeHdr(void* data, kde_output_device_v2* output, std::uint32_t enabled);
  static void kdeSdrBrightness(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeWcg(void* data, kde_output_device_v2* output, std::uint32_t enabled);
  static void kdeAutoRotate(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeIccPath(void* data, kde_output_device_v2* output, const char* path);
  static void kdeBrightnessMetadata(
      void* data, kde_output_device_v2* output, std::uint32_t maxPeak, std::uint32_t maxAverage, std::uint32_t min
  );
  static void kdeBrightnessOverrides(
      void* data, kde_output_device_v2* output, std::int32_t maxPeak, std::int32_t maxAverage, std::int32_t min
  );
  static void kdeSdrGamut(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeColorProfileSource(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeBrightness(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeColorPowerTradeoff(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeDimming(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeReplicationSource(void* data, kde_output_device_v2* output, const char* source);
  static void kdeDdcCi(void* data, kde_output_device_v2* output, std::uint32_t allowed);
  static void kdeMaxBpc(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeMaxBpcRange(void* data, kde_output_device_v2* output, std::uint32_t min, std::uint32_t max);
  static void kdeMaxBpcAutomaticLimit(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeEdr(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeSharpness(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdePriority(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeAutoBrightness(void* data, kde_output_device_v2* output, std::uint32_t enabled);
  static void kdeRemoved(void* data, kde_output_device_v2* output);
  static void kdeHdrIccPath(void* data, kde_output_device_v2* output, const char* path);
  static void kdeHdrColorProfileSource(void* data, kde_output_device_v2* output, std::uint32_t value);
  static void kdeAbmLevel(void* data, kde_output_device_v2* output, std::uint32_t value);

  static void kdeModeSize(void* data, kde_output_device_mode_v2* mode, std::int32_t width, std::int32_t height);
  static void kdeModeRefresh(void* data, kde_output_device_mode_v2* mode, std::int32_t refresh);
  static void kdeModePreferred(void* data, kde_output_device_mode_v2* mode);
  static void kdeModeRemoved(void* data, kde_output_device_mode_v2* mode);
  static void kdeModeFlags(void* data, kde_output_device_mode_v2* mode, std::uint32_t flags);
  static void kdeModeCvt(
      void*, kde_output_device_mode_v2*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
      std::uint32_t
  ) {}

  static void kdeConfigApplied(void* data, kde_output_configuration_v2* configuration);
  static void kdeConfigFailed(void* data, kde_output_configuration_v2* configuration);
  static void kdeConfigFailureReason(void* data, kde_output_configuration_v2*, const char* reason);

  static void wlrManagerHead(void* data, zwlr_output_manager_v1*, zwlr_output_head_v1* head);
  static void wlrManagerDone(void* data, zwlr_output_manager_v1*, std::uint32_t serial);
  static void wlrManagerFinished(void* data, zwlr_output_manager_v1* manager);
  static void wlrHeadName(void* data, zwlr_output_head_v1* head, const char* name);
  static void wlrHeadDescription(void* data, zwlr_output_head_v1* head, const char* description);
  static void wlrHeadPhysicalSize(void*, zwlr_output_head_v1*, std::int32_t, std::int32_t) {}
  static void wlrHeadMode(void* data, zwlr_output_head_v1* head, zwlr_output_mode_v1* mode);
  static void wlrHeadEnabled(void* data, zwlr_output_head_v1* head, std::int32_t enabled);
  static void wlrHeadCurrentMode(void* data, zwlr_output_head_v1* head, zwlr_output_mode_v1* mode);
  static void wlrHeadPosition(void* data, zwlr_output_head_v1* head, std::int32_t x, std::int32_t y);
  static void wlrHeadTransform(void* data, zwlr_output_head_v1* head, std::int32_t transform);
  static void wlrHeadScale(void* data, zwlr_output_head_v1* head, wl_fixed_t scale);
  static void wlrHeadFinished(void* data, zwlr_output_head_v1* head);
  static void wlrHeadMake(void* data, zwlr_output_head_v1* head, const char* make);
  static void wlrHeadModel(void* data, zwlr_output_head_v1* head, const char* model);
  static void wlrHeadSerial(void* data, zwlr_output_head_v1* head, const char* serial);
  static void wlrHeadAdaptiveSync(void* data, zwlr_output_head_v1* head, std::uint32_t state);
  static void wlrModeSize(void* data, zwlr_output_mode_v1* mode, std::int32_t width, std::int32_t height);
  static void wlrModeRefresh(void* data, zwlr_output_mode_v1* mode, std::int32_t refresh);
  static void wlrModePreferred(void* data, zwlr_output_mode_v1* mode);
  static void wlrModeFinished(void* data, zwlr_output_mode_v1* mode);
  static void wlrConfigSucceeded(void* data, zwlr_output_configuration_v1* configuration);
  static void wlrConfigFailed(void* data, zwlr_output_configuration_v1* configuration);
  static void wlrConfigCancelled(void* data, zwlr_output_configuration_v1* configuration);

  void addKdeOutput(kde_output_device_v2* output);
  void addKdeMode(KdeOutput& output, kde_output_device_mode_v2* mode);
  void syncKdeOutput(KdeOutput& output);
  void addWlrOutput(zwlr_output_head_v1* head);
  void addWlrMode(WlrOutput& output, zwlr_output_mode_v1* mode);
  void syncWlrOutput(WlrOutput& output);
  void syncPublicOutputs();
  void notifyChanged();
  [[nodiscard]] KdeOutput* findKde(kde_output_device_v2* output) const;
  [[nodiscard]] KdeOutput* findKdeMode(kde_output_device_mode_v2* mode) const;
  [[nodiscard]] WlrOutput* findWlr(zwlr_output_head_v1* head) const;
  [[nodiscard]] WlrOutput* findWlrMode(zwlr_output_mode_v1* mode) const;
  [[nodiscard]] KdeOutput* kdeForId(std::string_view id) const;
  [[nodiscard]] WlrOutput* wlrForId(std::string_view id) const;
  [[nodiscard]] kde_output_device_mode_v2* findKdeModeProxy(
      KdeOutput& output, const DisplayOutputBackend::Mode& mode
  ) const;
  [[nodiscard]] zwlr_output_mode_v1* findWlrModeProxy(
      WlrOutput& output, const DisplayOutputBackend::Mode& mode
  ) const;
  [[nodiscard]] bool supportsKde(std::uint32_t version) const noexcept;
  [[nodiscard]] bool applyKde(const std::vector<DisplayOutputBackend::OutputSettings>& settings, std::string& error);
  [[nodiscard]] bool applyWlr(const std::vector<DisplayOutputBackend::OutputSettings>& settings, std::string& error);
  void cleanup();
};

namespace {

  const wl_registry_listener kRegistryListener = {
      .global = &DisplayOutputBackend::Private::global,
      .global_remove = &DisplayOutputBackend::Private::globalRemove,
  };

  const kde_output_device_registry_v2_listener kKdeRegistryListener = {
      .finished = &DisplayOutputBackend::Private::kdeRegistryFinished,
      .output = &DisplayOutputBackend::Private::kdeRegistryOutput,
  };

  const kde_output_device_v2_listener kKdeDeviceListener = {
      .geometry = &DisplayOutputBackend::Private::kdeGeometry,
      .current_mode = &DisplayOutputBackend::Private::kdeCurrentMode,
      .mode = &DisplayOutputBackend::Private::kdeMode,
      .done = &DisplayOutputBackend::Private::kdeDone,
      .scale = &DisplayOutputBackend::Private::kdeScale,
      .edid = [](void*, kde_output_device_v2*, const char*) {},
      .enabled = &DisplayOutputBackend::Private::kdeEnabled,
      .uuid = &DisplayOutputBackend::Private::kdeUuid,
      .serial_number = &DisplayOutputBackend::Private::kdeSerial,
      .eisa_id = [](void*, kde_output_device_v2*, const char*) {},
      .capabilities = &DisplayOutputBackend::Private::kdeCapabilities,
      .overscan = &DisplayOutputBackend::Private::kdeOverscan,
      .vrr_policy = &DisplayOutputBackend::Private::kdeVrrPolicy,
      .rgb_range = &DisplayOutputBackend::Private::kdeRgbRange,
      .name = &DisplayOutputBackend::Private::kdeName,
      .high_dynamic_range = &DisplayOutputBackend::Private::kdeHdr,
      .sdr_brightness = &DisplayOutputBackend::Private::kdeSdrBrightness,
      .wide_color_gamut = &DisplayOutputBackend::Private::kdeWcg,
      .auto_rotate_policy = &DisplayOutputBackend::Private::kdeAutoRotate,
      .icc_profile_path = &DisplayOutputBackend::Private::kdeIccPath,
      .brightness_metadata = &DisplayOutputBackend::Private::kdeBrightnessMetadata,
      .brightness_overrides = &DisplayOutputBackend::Private::kdeBrightnessOverrides,
      .sdr_gamut_wideness = &DisplayOutputBackend::Private::kdeSdrGamut,
      .color_profile_source = &DisplayOutputBackend::Private::kdeColorProfileSource,
      .brightness = &DisplayOutputBackend::Private::kdeBrightness,
      .color_power_tradeoff = &DisplayOutputBackend::Private::kdeColorPowerTradeoff,
      .dimming = &DisplayOutputBackend::Private::kdeDimming,
      .replication_source = &DisplayOutputBackend::Private::kdeReplicationSource,
      .ddc_ci_allowed = &DisplayOutputBackend::Private::kdeDdcCi,
      .max_bits_per_color = &DisplayOutputBackend::Private::kdeMaxBpc,
      .max_bits_per_color_range = &DisplayOutputBackend::Private::kdeMaxBpcRange,
      .automatic_max_bits_per_color_limit = &DisplayOutputBackend::Private::kdeMaxBpcAutomaticLimit,
      .edr_policy = &DisplayOutputBackend::Private::kdeEdr,
      .sharpness = &DisplayOutputBackend::Private::kdeSharpness,
      .priority = &DisplayOutputBackend::Private::kdePriority,
      .auto_brightness = &DisplayOutputBackend::Private::kdeAutoBrightness,
      .removed = &DisplayOutputBackend::Private::kdeRemoved,
      .hdr_icc_profile_path = &DisplayOutputBackend::Private::kdeHdrIccPath,
      .hdr_color_profile_source = &DisplayOutputBackend::Private::kdeHdrColorProfileSource,
      .abm_level = &DisplayOutputBackend::Private::kdeAbmLevel,
  };

  const kde_output_device_mode_v2_listener kKdeModeListener = {
      .size = &DisplayOutputBackend::Private::kdeModeSize,
      .refresh = &DisplayOutputBackend::Private::kdeModeRefresh,
      .preferred = &DisplayOutputBackend::Private::kdeModePreferred,
      .removed = &DisplayOutputBackend::Private::kdeModeRemoved,
      .flags = &DisplayOutputBackend::Private::kdeModeFlags,
      .cvt = &DisplayOutputBackend::Private::kdeModeCvt,
  };

  const kde_output_configuration_v2_listener kKdeConfigListener = {
      .applied = &DisplayOutputBackend::Private::kdeConfigApplied,
      .failed = &DisplayOutputBackend::Private::kdeConfigFailed,
      .failure_reason = &DisplayOutputBackend::Private::kdeConfigFailureReason,
  };

  const zwlr_output_manager_v1_listener kWlrManagerListener = {
      .head = &DisplayOutputBackend::Private::wlrManagerHead,
      .done = &DisplayOutputBackend::Private::wlrManagerDone,
      .finished = &DisplayOutputBackend::Private::wlrManagerFinished,
  };

  const zwlr_output_head_v1_listener kWlrHeadListener = {
      .name = &DisplayOutputBackend::Private::wlrHeadName,
      .description = &DisplayOutputBackend::Private::wlrHeadDescription,
      .physical_size = &DisplayOutputBackend::Private::wlrHeadPhysicalSize,
      .mode = &DisplayOutputBackend::Private::wlrHeadMode,
      .enabled = &DisplayOutputBackend::Private::wlrHeadEnabled,
      .current_mode = &DisplayOutputBackend::Private::wlrHeadCurrentMode,
      .position = &DisplayOutputBackend::Private::wlrHeadPosition,
      .transform = &DisplayOutputBackend::Private::wlrHeadTransform,
      .scale = &DisplayOutputBackend::Private::wlrHeadScale,
      .finished = &DisplayOutputBackend::Private::wlrHeadFinished,
      .make = &DisplayOutputBackend::Private::wlrHeadMake,
      .model = &DisplayOutputBackend::Private::wlrHeadModel,
      .serial_number = &DisplayOutputBackend::Private::wlrHeadSerial,
      .adaptive_sync = &DisplayOutputBackend::Private::wlrHeadAdaptiveSync,
  };

  const zwlr_output_mode_v1_listener kWlrModeListener = {
      .size = &DisplayOutputBackend::Private::wlrModeSize,
      .refresh = &DisplayOutputBackend::Private::wlrModeRefresh,
      .preferred = &DisplayOutputBackend::Private::wlrModePreferred,
      .finished = &DisplayOutputBackend::Private::wlrModeFinished,
  };

} // namespace

DisplayOutputBackend::DisplayOutputBackend(WaylandConnection& wayland)
    : m_private(std::make_unique<Private>(*this, wayland)) {}

DisplayOutputBackend::~DisplayOutputBackend() = default;

DisplayOutputBackend::Protocol DisplayOutputBackend::protocol() const noexcept {
  if (m_private->kdeRegistry != nullptr && m_private->kdeManagement != nullptr) {
    return Protocol::Kinetic;
  }
  if (m_private->wlrManager != nullptr) {
    return Protocol::Wlr;
  }
  return Protocol::None;
}

bool DisplayOutputBackend::supportsKineticFeature(std::uint32_t version) const noexcept {
  return protocol() == Protocol::Kinetic && m_private->supportsKde(version);
}

bool DisplayOutputBackend::initialize() {
  if (m_initialized) {
    return true;
  }
  if (m_private->display == nullptr) {
    return false;
  }

  m_private->registry = wl_display_get_registry(m_private->display);
  if (m_private->registry == nullptr) {
    kLog.warn("failed to create display settings Wayland registry");
    return false;
  }
  if (wl_registry_add_listener(m_private->registry, &kRegistryListener, m_private.get()) != 0) {
    wl_registry_destroy(m_private->registry);
    m_private->registry = nullptr;
    kLog.warn("failed to add display settings Wayland registry listener");
    return false;
  }

  if (wl_display_roundtrip(m_private->display) < 0 || wl_display_roundtrip(m_private->display) < 0) {
    kLog.warn("display settings output discovery failed: {}", waylandError());
    m_private->cleanup();
    return false;
  }

  m_private->syncPublicOutputs();
  m_initialized = true;
  return true;
}

bool DisplayOutputBackend::refresh() {
  if (!m_initialized || m_private->display == nullptr) {
    return initialize();
  }
  if (wl_display_roundtrip(m_private->display) < 0) {
    kLog.warn("display settings refresh failed: {}", waylandError());
    return false;
  }
  m_private->syncPublicOutputs();
  return true;
}

bool DisplayOutputBackend::apply(const std::vector<OutputSettings>& settings, std::string& error) {
  if (!initialize()) {
    error = "The compositor does not expose an output configuration protocol.";
    return false;
  }
  switch (protocol()) {
  case Protocol::Kinetic:
    return m_private->applyKde(settings, error);
  case Protocol::Wlr:
    return m_private->applyWlr(settings, error);
  case Protocol::None:
    error = "The compositor does not expose an output configuration protocol.";
    return false;
  }
  return false;
}

void DisplayOutputBackend::Private::global(
    void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version
) {
  auto* self = static_cast<Private*>(data);
  if (std::strcmp(interface, kde_output_device_registry_v2_interface.name) == 0 && self->kdeRegistry == nullptr) {
    if (version < kKdeRegistryMinimumVersion) {
      return;
    }
    self->kdeRegistry = static_cast<kde_output_device_registry_v2*>(
        wl_registry_bind(registry, name, &kde_output_device_registry_v2_interface, std::min(version, kKdeDeviceVersion))
    );
    kde_output_device_registry_v2_add_listener(self->kdeRegistry, &kKdeRegistryListener, self);
    return;
  }
  if (std::strcmp(interface, kde_output_management_v2_interface.name) == 0 && self->kdeManagement == nullptr) {
    self->kdeManagement = static_cast<kde_output_management_v2*>(
        wl_registry_bind(registry, name, &kde_output_management_v2_interface, std::min(version, kKdeManagementVersion))
    );
    return;
  }
  if (std::strcmp(interface, zwlr_output_manager_v1_interface.name) == 0 && self->wlrManager == nullptr) {
    if (version < kWlrManagerMinimumVersion) {
      return;
    }
    self->wlrManager = static_cast<zwlr_output_manager_v1*>(
        wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, std::min(version, kWlrManagerVersion))
    );
    zwlr_output_manager_v1_add_listener(self->wlrManager, &kWlrManagerListener, self);
  }
}

void DisplayOutputBackend::Private::globalRemove(void*, wl_registry*, std::uint32_t) {}

void DisplayOutputBackend::Private::kdeRegistryOutput(void* data, kde_output_device_registry_v2*, kde_output_device_v2* output) {
  static_cast<Private*>(data)->addKdeOutput(output);
}

void DisplayOutputBackend::Private::addKdeOutput(kde_output_device_v2* output) {
  if (output == nullptr || kdeByProxy.contains(output)) {
    return;
  }
  auto record = std::make_unique<KdeOutput>();
  record->proxy = output;
  record->info.settings.scale = 1.0;
  record->info.settings.dimming = 10000;
  auto* ptr = record.get();
  kdeByProxy.emplace(output, ptr);
  kdeOutputs.push_back(std::move(record));
  kde_output_device_v2_add_listener(output, &kKdeDeviceListener, this);
}

DisplayOutputBackend::Private::KdeOutput* DisplayOutputBackend::Private::findKde(kde_output_device_v2* output) const {
  const auto it = kdeByProxy.find(output);
  return it == kdeByProxy.end() ? nullptr : it->second;
}

DisplayOutputBackend::Private::KdeOutput* DisplayOutputBackend::Private::findKdeMode(
    kde_output_device_mode_v2* mode
) const {
  const auto it = kdeModeOwners.find(mode);
  return it == kdeModeOwners.end() ? nullptr : it->second;
}

void DisplayOutputBackend::Private::addKdeMode(KdeOutput& output, kde_output_device_mode_v2* mode) {
  if (mode == nullptr || kdeModeOwners.contains(mode)) {
    return;
  }
  auto record = std::make_unique<KdeMode>();
  record->proxy = mode;
  kdeModeOwners.emplace(mode, &output);
  output.modes.push_back(std::move(record));
  kde_output_device_mode_v2_add_listener(mode, &kKdeModeListener, this);
}

void DisplayOutputBackend::Private::syncKdeOutput(KdeOutput& output) {
  auto& info = output.info;
  info.id = !info.uuid.empty() ? info.uuid : info.name;
  info.name = info.name.empty() ? info.id : info.name;
  info.description = info.name;
  if (info.description.empty() && (!info.make.empty() || !info.model.empty())) {
    info.description = info.make + " " + info.model;
    info.description.erase(info.description.find_last_not_of(' ') + 1);
  }
  if (info.description.empty()) {
    info.description = info.id;
  }
  info.settings.id = info.id;
  info.settings.mode = {};
  info.modes.clear();
  info.settings.customModes.clear();
  const KdeMode* fallbackMode = nullptr;
  for (const auto& mode : output.modes) {
    info.modes.push_back(mode->info);
    if (mode->info.custom) {
      info.settings.customModes.push_back(mode->info);
    }
    if (fallbackMode == nullptr && mode->info.preferred) {
      fallbackMode = mode.get();
    }
    if (mode->proxy == output.currentMode) {
      info.settings.mode = mode->info;
    }
  }
  if (info.settings.mode.width <= 0 && !output.modes.empty()) {
    const auto& mode = fallbackMode != nullptr ? fallbackMode->info : output.modes.front()->info;
    info.settings.mode = mode;
  }
}

void DisplayOutputBackend::Private::kdeGeometry(
    void* data, kde_output_device_v2* output, std::int32_t x, std::int32_t y, std::int32_t physicalWidth,
    std::int32_t physicalHeight, std::int32_t, const char* make, const char* model, std::int32_t transform
) {
  auto* record = static_cast<Private*>(data)->findKde(output);
  if (record == nullptr) {
    return;
  }
  record->info.settings.x = x;
  record->info.settings.y = y;
  record->info.physicalWidth = physicalWidth;
  record->info.physicalHeight = physicalHeight;
  record->info.make = make != nullptr ? make : "";
  record->info.model = model != nullptr ? model : "";
  record->info.settings.transform = transform;
}

void DisplayOutputBackend::Private::kdeCurrentMode(void* data, kde_output_device_v2* output, kde_output_device_mode_v2* mode) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->currentMode = mode;
  }
}

void DisplayOutputBackend::Private::kdeMode(void* data, kde_output_device_v2* output, kde_output_device_mode_v2* mode) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    static_cast<Private*>(data)->addKdeMode(*record, mode);
  }
}

void DisplayOutputBackend::Private::kdeDone(void* data, kde_output_device_v2* output) {
  auto* self = static_cast<Private*>(data);
  if (auto* record = self->findKde(output); record != nullptr) {
    self->syncKdeOutput(*record);
    self->notifyChanged();
  }
}

void DisplayOutputBackend::Private::kdeScale(void* data, kde_output_device_v2* output, wl_fixed_t factor) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.scale = std::max(0.01, wl_fixed_to_double(factor));
  }
}

void DisplayOutputBackend::Private::kdeEnabled(void* data, kde_output_device_v2* output, std::int32_t enabled) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.enabled = enabled != 0;
  }
}

void DisplayOutputBackend::Private::kdeUuid(void* data, kde_output_device_v2* output, const char* uuid) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.uuid = uuid != nullptr ? uuid : "";
  }
}

void DisplayOutputBackend::Private::kdeSerial(void* data, kde_output_device_v2* output, const char* serial) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.serialNumber = serial != nullptr ? serial : "";
  }
}

void DisplayOutputBackend::Private::kdeCapabilities(void* data, kde_output_device_v2* output, std::uint32_t capabilities) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.capabilities = capabilities;
  }
}

void DisplayOutputBackend::Private::kdeOverscan(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.overscan = value;
  }
}

void DisplayOutputBackend::Private::kdeVrrPolicy(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.vrrPolicy = value;
  }
}

void DisplayOutputBackend::Private::kdeRgbRange(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.rgbRange = value;
  }
}

void DisplayOutputBackend::Private::kdeName(void* data, kde_output_device_v2* output, const char* name) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.name = name != nullptr ? name : "";
  }
}

void DisplayOutputBackend::Private::kdeHdr(void* data, kde_output_device_v2* output, std::uint32_t enabled) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.highDynamicRange = enabled != 0;
  }
}

void DisplayOutputBackend::Private::kdeSdrBrightness(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.sdrBrightness = value;
  }
}

void DisplayOutputBackend::Private::kdeWcg(void* data, kde_output_device_v2* output, std::uint32_t enabled) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.wideColorGamut = enabled != 0;
  }
}

void DisplayOutputBackend::Private::kdeAutoRotate(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.autoRotatePolicy = value;
  }
}

void DisplayOutputBackend::Private::kdeIccPath(void* data, kde_output_device_v2* output, const char* path) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.iccProfilePath = path != nullptr ? path : "";
  }
}

void DisplayOutputBackend::Private::kdeBrightnessMetadata(
    void* data, kde_output_device_v2* output, std::uint32_t maxPeak, std::uint32_t maxAverage, std::uint32_t min
) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.maxPeakBrightness = maxPeak;
    record->info.maxAverageBrightness = maxAverage;
    record->info.minBrightness = min;
  }
}

void DisplayOutputBackend::Private::kdeBrightnessOverrides(
    void* data, kde_output_device_v2* output, std::int32_t maxPeak, std::int32_t maxAverage, std::int32_t min
) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.maxPeakBrightnessOverride = maxPeak;
    record->info.settings.maxAverageBrightnessOverride = maxAverage;
    record->info.settings.minBrightnessOverride = min;
  }
}

void DisplayOutputBackend::Private::kdeSdrGamut(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.sdrGamutWideness = value;
  }
}

void DisplayOutputBackend::Private::kdeColorProfileSource(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.colorProfileSource = value;
  }
}

void DisplayOutputBackend::Private::kdeBrightness(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.brightness = value;
  }
}

void DisplayOutputBackend::Private::kdeColorPowerTradeoff(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.colorPowerTradeoff = value;
  }
}

void DisplayOutputBackend::Private::kdeDimming(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.dimming = value;
  }
}

void DisplayOutputBackend::Private::kdeReplicationSource(void* data, kde_output_device_v2* output, const char* source) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.replicationSource = source != nullptr ? source : "";
  }
}

void DisplayOutputBackend::Private::kdeDdcCi(void* data, kde_output_device_v2* output, std::uint32_t allowed) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.ddcCiAllowed = allowed != 0;
  }
}

void DisplayOutputBackend::Private::kdeMaxBpc(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.maxBitsPerColor = value;
  }
}

void DisplayOutputBackend::Private::kdeMaxBpcRange(
    void* data, kde_output_device_v2* output, std::uint32_t min, std::uint32_t max
) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.maxBitsPerColorMin = min;
    record->info.maxBitsPerColorMax = max;
  }
}

void DisplayOutputBackend::Private::kdeMaxBpcAutomaticLimit(
    void* data, kde_output_device_v2* output, std::uint32_t value
) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.maxBitsPerColorAutomaticLimit = value;
  }
}

void DisplayOutputBackend::Private::kdeEdr(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.edrPolicy = value;
  }
}

void DisplayOutputBackend::Private::kdeSharpness(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.sharpness = value;
  }
}

void DisplayOutputBackend::Private::kdePriority(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.priority = value;
  }
}

void DisplayOutputBackend::Private::kdeAutoBrightness(void* data, kde_output_device_v2* output, std::uint32_t enabled) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.automaticBrightness = enabled != 0;
  }
}

void DisplayOutputBackend::Private::kdeRemoved(void* data, kde_output_device_v2* output) {
  auto* self = static_cast<Private*>(data);
  auto it = self->kdeByProxy.find(output);
  if (it == self->kdeByProxy.end()) {
    return;
  }
  for (const auto& mode : it->second->modes) {
    if (mode->proxy != nullptr) {
      self->kdeModeOwners.erase(mode->proxy);
      kde_output_device_mode_v2_destroy(mode->proxy);
    }
  }
  kde_output_device_v2_release(output);
  self->kdeByProxy.erase(it);
  std::erase_if(self->kdeOutputs, [output](const auto& record) { return record->proxy == output; });
  self->syncPublicOutputs();
  self->notifyChanged();
}

void DisplayOutputBackend::Private::kdeHdrIccPath(void* data, kde_output_device_v2* output, const char* path) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.hdrIccProfilePath = path != nullptr ? path : "";
  }
}

void DisplayOutputBackend::Private::kdeHdrColorProfileSource(
    void* data, kde_output_device_v2* output, std::uint32_t value
) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.hdrColorProfileSource = value;
  }
}

void DisplayOutputBackend::Private::kdeAbmLevel(void* data, kde_output_device_v2* output, std::uint32_t value) {
  if (auto* record = static_cast<Private*>(data)->findKde(output); record != nullptr) {
    record->info.settings.abmLevel = value;
  }
}

void DisplayOutputBackend::Private::kdeModeSize(
    void* data, kde_output_device_mode_v2* mode, std::int32_t width, std::int32_t height
) {
  auto* owner = static_cast<Private*>(data)->findKdeMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.width = width;
      item->info.height = height;
      return;
    }
  }
}

void DisplayOutputBackend::Private::kdeModeRefresh(void* data, kde_output_device_mode_v2* mode, std::int32_t refresh) {
  auto* owner = static_cast<Private*>(data)->findKdeMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.refresh = refresh;
      return;
    }
  }
}

void DisplayOutputBackend::Private::kdeModePreferred(void* data, kde_output_device_mode_v2* mode) {
  auto* owner = static_cast<Private*>(data)->findKdeMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.preferred = true;
      return;
    }
  }
}

void DisplayOutputBackend::Private::kdeModeRemoved(void* data, kde_output_device_mode_v2* mode) {
  auto* self = static_cast<Private*>(data);
  auto* owner = self->findKdeMode(mode);
  if (owner == nullptr) {
    return;
  }
  if (owner->currentMode == mode) {
    owner->currentMode = nullptr;
  }
  self->kdeModeOwners.erase(mode);
  std::erase_if(owner->modes, [mode](const auto& item) { return item->proxy == mode; });
  kde_output_device_mode_v2_destroy(mode);
}

void DisplayOutputBackend::Private::kdeModeFlags(
    void* data, kde_output_device_mode_v2* mode, std::uint32_t flags
) {
  auto* owner = static_cast<Private*>(data)->findKdeMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.custom = (flags & kKdeModeCustom) != 0;
      item->info.reducedBlanking = (flags & kKdeModeReducedBlanking) != 0;
      return;
    }
  }
}

void DisplayOutputBackend::Private::kdeConfigApplied(void* data, kde_output_configuration_v2*) {
  auto* self = static_cast<Private*>(data);
  self->applyFinished = true;
  self->applySucceeded = true;
}

void DisplayOutputBackend::Private::kdeConfigFailed(void* data, kde_output_configuration_v2*) {
  auto* self = static_cast<Private*>(data);
  self->applyFinished = true;
  self->applySucceeded = false;
  if (self->applyFailure.empty()) {
    self->applyFailure = "The compositor rejected the output configuration.";
  }
}

void DisplayOutputBackend::Private::kdeConfigFailureReason(
    void* data, kde_output_configuration_v2*, const char* reason
) {
  static_cast<Private*>(data)->applyFailure = reason != nullptr ? reason : "The compositor rejected the output configuration.";
}

void DisplayOutputBackend::Private::wlrManagerHead(void* data, zwlr_output_manager_v1*, zwlr_output_head_v1* head) {
  auto* self = static_cast<Private*>(data);
  self->addWlrOutput(head);
  zwlr_output_head_v1_add_listener(head, &kWlrHeadListener, self);
}

void DisplayOutputBackend::Private::wlrManagerDone(void* data, zwlr_output_manager_v1*, std::uint32_t serial) {
  auto* self = static_cast<Private*>(data);
  self->wlrSerial = serial;
  self->wlrSerialReady = true;
  for (auto& output : self->wlrOutputs) {
    self->syncWlrOutput(*output);
  }
  self->notifyChanged();
}

void DisplayOutputBackend::Private::wlrManagerFinished(void* data, zwlr_output_manager_v1* manager) {
  auto* self = static_cast<Private*>(data);
  if (self->wlrManager == manager) {
    self->wlrManager = nullptr;
  }
}

void DisplayOutputBackend::Private::addWlrOutput(zwlr_output_head_v1* head) {
  if (head == nullptr || wlrByProxy.contains(head)) {
    return;
  }
  auto record = std::make_unique<WlrOutput>();
  record->proxy = head;
  record->info.settings.scale = 1.0;
  auto* ptr = record.get();
  wlrByProxy.emplace(head, ptr);
  wlrOutputs.push_back(std::move(record));
}

DisplayOutputBackend::Private::WlrOutput* DisplayOutputBackend::Private::findWlr(zwlr_output_head_v1* head) const {
  const auto it = wlrByProxy.find(head);
  return it == wlrByProxy.end() ? nullptr : it->second;
}

DisplayOutputBackend::Private::WlrOutput* DisplayOutputBackend::Private::findWlrMode(
    zwlr_output_mode_v1* mode
) const {
  const auto it = wlrModeOwners.find(mode);
  return it == wlrModeOwners.end() ? nullptr : it->second;
}

void DisplayOutputBackend::Private::addWlrMode(WlrOutput& output, zwlr_output_mode_v1* mode) {
  if (mode == nullptr || wlrModeOwners.contains(mode)) {
    return;
  }
  auto record = std::make_unique<WlrMode>();
  record->proxy = mode;
  wlrModeOwners.emplace(mode, &output);
  output.modes.push_back(std::move(record));
  zwlr_output_mode_v1_add_listener(mode, &kWlrModeListener, this);
}

void DisplayOutputBackend::Private::syncWlrOutput(WlrOutput& output) {
  auto& info = output.info;
  info.id = info.name;
  info.description = info.description.empty() ? info.name : info.description;
  info.settings.id = info.id;
  info.settings.mode = {};
  info.modes.clear();
  const WlrMode* fallbackMode = nullptr;
  for (const auto& mode : output.modes) {
    info.modes.push_back(mode->info);
    if (fallbackMode == nullptr && mode->info.preferred) {
      fallbackMode = mode.get();
    }
    if (mode->proxy == output.currentMode) {
      info.settings.mode = mode->info;
    }
  }
  if (info.settings.mode.width <= 0 && !output.modes.empty()) {
    const auto& mode = fallbackMode != nullptr ? fallbackMode->info : output.modes.front()->info;
    info.settings.mode = mode;
  }
}

void DisplayOutputBackend::Private::wlrHeadName(void* data, zwlr_output_head_v1* head, const char* name) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.name = name != nullptr ? name : "";
  }
}

void DisplayOutputBackend::Private::wlrHeadDescription(
    void* data, zwlr_output_head_v1* head, const char* description
) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.description = description != nullptr ? description : "";
  }
}

void DisplayOutputBackend::Private::wlrHeadMode(void* data, zwlr_output_head_v1* head, zwlr_output_mode_v1* mode) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    static_cast<Private*>(data)->addWlrMode(*record, mode);
  }
}

void DisplayOutputBackend::Private::wlrHeadEnabled(void* data, zwlr_output_head_v1* head, std::int32_t enabled) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.settings.enabled = enabled != 0;
  }
}

void DisplayOutputBackend::Private::wlrHeadCurrentMode(
    void* data, zwlr_output_head_v1* head, zwlr_output_mode_v1* mode
) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->currentMode = mode;
  }
}

void DisplayOutputBackend::Private::wlrHeadPosition(
    void* data, zwlr_output_head_v1* head, std::int32_t x, std::int32_t y
) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.settings.x = x;
    record->info.settings.y = y;
  }
}

void DisplayOutputBackend::Private::wlrHeadTransform(void* data, zwlr_output_head_v1* head, std::int32_t transform) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.settings.transform = transform;
  }
}

void DisplayOutputBackend::Private::wlrHeadScale(void* data, zwlr_output_head_v1* head, wl_fixed_t scale) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.settings.scale = std::max(0.01, wl_fixed_to_double(scale));
  }
}

void DisplayOutputBackend::Private::wlrHeadFinished(void* data, zwlr_output_head_v1* head) {
  auto* self = static_cast<Private*>(data);
  auto it = self->wlrByProxy.find(head);
  if (it == self->wlrByProxy.end()) {
    return;
  }
  for (const auto& mode : it->second->modes) {
    if (mode->proxy != nullptr) {
      self->wlrModeOwners.erase(mode->proxy);
      zwlr_output_mode_v1_release(mode->proxy);
    }
  }
  self->wlrByProxy.erase(it);
  std::erase_if(self->wlrOutputs, [head](const auto& record) { return record->proxy == head; });
  zwlr_output_head_v1_release(head);
  self->syncPublicOutputs();
  self->notifyChanged();
}

void DisplayOutputBackend::Private::wlrHeadMake(void* data, zwlr_output_head_v1* head, const char* make) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.make = make != nullptr ? make : "";
  }
}

void DisplayOutputBackend::Private::wlrHeadModel(void* data, zwlr_output_head_v1* head, const char* model) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.model = model != nullptr ? model : "";
  }
}

void DisplayOutputBackend::Private::wlrHeadSerial(void* data, zwlr_output_head_v1* head, const char* serial) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.serialNumber = serial != nullptr ? serial : "";
  }
}

void DisplayOutputBackend::Private::wlrHeadAdaptiveSync(
    void* data, zwlr_output_head_v1* head, std::uint32_t state
) {
  if (auto* record = static_cast<Private*>(data)->findWlr(head); record != nullptr) {
    record->info.capabilities |= static_cast<std::uint32_t>(Capability::Vrr);
    record->info.settings.vrrPolicy = state != 0 ? 1U : 0U;
  }
}

void DisplayOutputBackend::Private::wlrModeSize(
    void* data, zwlr_output_mode_v1* mode, std::int32_t width, std::int32_t height
) {
  auto* owner = static_cast<Private*>(data)->findWlrMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.width = width;
      item->info.height = height;
      return;
    }
  }
}

void DisplayOutputBackend::Private::wlrModeRefresh(void* data, zwlr_output_mode_v1* mode, std::int32_t refresh) {
  auto* owner = static_cast<Private*>(data)->findWlrMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.refresh = refresh;
      return;
    }
  }
}

void DisplayOutputBackend::Private::wlrModePreferred(void* data, zwlr_output_mode_v1* mode) {
  auto* owner = static_cast<Private*>(data)->findWlrMode(mode);
  if (owner == nullptr) {
    return;
  }
  for (const auto& item : owner->modes) {
    if (item->proxy == mode) {
      item->info.preferred = true;
      return;
    }
  }
}

void DisplayOutputBackend::Private::wlrModeFinished(void* data, zwlr_output_mode_v1* mode) {
  auto* self = static_cast<Private*>(data);
  auto* owner = self->findWlrMode(mode);
  if (owner == nullptr) {
    return;
  }
  if (owner->currentMode == mode) {
    owner->currentMode = nullptr;
  }
  self->wlrModeOwners.erase(mode);
  std::erase_if(owner->modes, [mode](const auto& item) { return item->proxy == mode; });
  zwlr_output_mode_v1_release(mode);
}

void DisplayOutputBackend::Private::wlrConfigSucceeded(void* data, zwlr_output_configuration_v1*) {
  auto* self = static_cast<Private*>(data);
  self->applyFinished = true;
  self->applySucceeded = true;
}

void DisplayOutputBackend::Private::wlrConfigFailed(void* data, zwlr_output_configuration_v1*) {
  auto* self = static_cast<Private*>(data);
  self->applyFinished = true;
  self->applySucceeded = false;
  self->applyFailure = "The compositor rejected the output configuration.";
}

void DisplayOutputBackend::Private::wlrConfigCancelled(void* data, zwlr_output_configuration_v1*) {
  auto* self = static_cast<Private*>(data);
  self->applyFinished = true;
  self->applySucceeded = false;
  self->applyFailure = "The output configuration became stale; please try again.";
}

void DisplayOutputBackend::Private::syncPublicOutputs() {
  const bool useKde = kdeRegistry != nullptr && kdeManagement != nullptr;
  owner.m_outputs.clear();
  if (useKde) {
    owner.m_outputs.reserve(kdeOutputs.size());
    for (const auto& record : kdeOutputs) {
      syncKdeOutput(*record);
      owner.m_outputs.push_back(record->info);
    }
  } else {
    owner.m_outputs.reserve(wlrOutputs.size());
    for (const auto& record : wlrOutputs) {
      syncWlrOutput(*record);
      owner.m_outputs.push_back(record->info);
    }
  }
  std::ranges::sort(owner.m_outputs, [](const auto& left, const auto& right) { return left.id < right.id; });
}

void DisplayOutputBackend::Private::notifyChanged() {
  syncPublicOutputs();
  if (owner.m_initialized && owner.m_changeCallback != nullptr) {
    owner.m_changeCallback();
  }
}

DisplayOutputBackend::Private::KdeOutput* DisplayOutputBackend::Private::kdeForId(std::string_view id) const {
  for (const auto& output : kdeOutputs) {
    if (output->info.id == id || output->info.uuid == id || output->info.name == id) {
      return output.get();
    }
  }
  return nullptr;
}

DisplayOutputBackend::Private::WlrOutput* DisplayOutputBackend::Private::wlrForId(std::string_view id) const {
  for (const auto& output : wlrOutputs) {
    if (output->info.id == id || output->info.name == id) {
      return output.get();
    }
  }
  return nullptr;
}

kde_output_device_mode_v2* DisplayOutputBackend::Private::findKdeModeProxy(
    KdeOutput& output, const DisplayOutputBackend::Mode& mode
) const {
  for (const auto& item : output.modes) {
    if (sameMode(item->info, mode)) {
      return item->proxy;
    }
  }
  for (const auto& item : output.modes) {
    if (item->info.width == mode.width && item->info.height == mode.height && item->info.refresh == mode.refresh) {
      return item->proxy;
    }
  }
  return nullptr;
}

zwlr_output_mode_v1* DisplayOutputBackend::Private::findWlrModeProxy(
    WlrOutput& output, const DisplayOutputBackend::Mode& mode
) const {
  for (const auto& item : output.modes) {
    if (sameMode(item->info, mode)) {
      return item->proxy;
    }
  }
  for (const auto& item : output.modes) {
    if (item->info.width == mode.width && item->info.height == mode.height && item->info.refresh == mode.refresh) {
      return item->proxy;
    }
  }
  return nullptr;
}

bool DisplayOutputBackend::Private::supportsKde(std::uint32_t version) const noexcept {
  return kdeManagement != nullptr && kde_output_management_v2_get_version(kdeManagement) >= version;
}

bool DisplayOutputBackend::Private::applyKde(
    const std::vector<DisplayOutputBackend::OutputSettings>& settings, std::string& error
) {
  if (!supportsKde(1)) {
    error = "The compositor's output configuration protocol is unavailable.";
    return false;
  }
  auto* configuration = kde_output_management_v2_create_configuration(kdeManagement);
  if (configuration == nullptr) {
    error = "Failed to create an output configuration.";
    return false;
  }
  kde_output_configuration_v2_add_listener(configuration, &kKdeConfigListener, this);
  applyFinished = false;
  applySucceeded = false;
  applyFailure.clear();

  for (const auto& requested : settings) {
    auto* output = kdeForId(requested.id);
    if (output == nullptr) {
      continue;
    }
    auto* device = output->proxy;
    kde_output_configuration_v2_enable(configuration, device, requested.enabled ? 1 : 0);
    if (auto* mode = findKdeModeProxy(*output, requested.mode); mode != nullptr) {
      kde_output_configuration_v2_mode(configuration, device, mode);
    }
    kde_output_configuration_v2_transform(configuration, device, requested.transform);
    kde_output_configuration_v2_position(configuration, device, requested.x, requested.y);
    kde_output_configuration_v2_scale(configuration, device, wl_fixed_from_double(std::max(0.01, requested.scale)));

    if (supportsKde(1) && output->info.supports(Capability::Overscan)) {
      kde_output_configuration_v2_overscan(configuration, device, requested.overscan);
    }
    if (output->info.supports(Capability::Vrr)) {
      kde_output_configuration_v2_set_vrr_policy(configuration, device, requested.vrrPolicy);
    }
    if (output->info.supports(Capability::RgbRange)) {
      kde_output_configuration_v2_set_rgb_range(configuration, device, requested.rgbRange);
    }
    if (supportsKde(4)) {
      if (output->info.supports(Capability::HighDynamicRange)) {
        kde_output_configuration_v2_set_high_dynamic_range(configuration, device, requested.highDynamicRange ? 1 : 0);
        kde_output_configuration_v2_set_sdr_brightness(configuration, device, requested.sdrBrightness);
      }
      if (output->info.supports(Capability::WideColorGamut)) {
        kde_output_configuration_v2_set_wide_color_gamut(configuration, device, requested.wideColorGamut ? 1 : 0);
      }
    }
    if (supportsKde(5) && output->info.supports(Capability::AutoRotate)) {
      kde_output_configuration_v2_set_auto_rotate_policy(configuration, device, requested.autoRotatePolicy);
    }
    if (supportsKde(6) && output->info.supports(Capability::IccProfile) && !requested.iccProfilePath.empty()) {
      kde_output_configuration_v2_set_icc_profile_path(configuration, device, requested.iccProfilePath.c_str());
    }
    if (supportsKde(7)) {
      kde_output_configuration_v2_set_brightness_overrides(
          configuration, device, requested.maxPeakBrightnessOverride, requested.maxAverageBrightnessOverride,
          requested.minBrightnessOverride
      );
      kde_output_configuration_v2_set_sdr_gamut_wideness(configuration, device, requested.sdrGamutWideness);
    }
    if (supportsKde(8)) {
      kde_output_configuration_v2_set_color_profile_source(configuration, device, requested.colorProfileSource);
    }
    if (supportsKde(9) && output->info.supports(Capability::Brightness)) {
      kde_output_configuration_v2_set_brightness(configuration, device, requested.brightness);
    }
    if (supportsKde(10)) {
      kde_output_configuration_v2_set_color_power_tradeoff(configuration, device, requested.colorPowerTradeoff);
    }
    if (supportsKde(11) && output->info.supports(Capability::Brightness)) {
      kde_output_configuration_v2_set_dimming(configuration, device, requested.dimming);
    }
    if (supportsKde(13)) {
      kde_output_configuration_v2_set_replication_source(configuration, device, requested.replicationSource.c_str());
    }
    if (supportsKde(14) && output->info.supports(Capability::DdcCi)) {
      kde_output_configuration_v2_set_ddc_ci_allowed(configuration, device, requested.ddcCiAllowed ? 1 : 0);
    }
    if (supportsKde(15) && output->info.supports(Capability::MaxBitsPerColor)) {
      kde_output_configuration_v2_set_max_bits_per_color(configuration, device, requested.maxBitsPerColor);
    }
    if (supportsKde(16) && output->info.supports(Capability::Edr)) {
      kde_output_configuration_v2_set_edr_policy(configuration, device, requested.edrPolicy);
    }
    if (supportsKde(17) && output->info.supports(Capability::Sharpness)) {
      kde_output_configuration_v2_set_sharpness(configuration, device, requested.sharpness);
    }
    if (supportsKde(18) && output->info.supports(Capability::CustomModes)) {
      auto* modeList = kde_output_management_v2_create_mode_list(kdeManagement);
      if (modeList != nullptr) {
        for (const auto& custom : requested.customModes) {
          if (custom.width <= 0 || custom.height <= 0 || custom.refresh <= 0) {
            continue;
          }
          kde_mode_list_v2_set_resolution(modeList, custom.width, custom.height);
          kde_mode_list_v2_set_refresh_rate(modeList, custom.refresh);
          kde_mode_list_v2_set_reduced_blanking(modeList, custom.reducedBlanking ? 1 : 0);
          kde_mode_list_v2_add_mode(modeList);
        }
        kde_output_configuration_v2_set_custom_modes(configuration, device, modeList);
        kde_mode_list_v2_destroy(modeList);
      }
    }
    if (supportsKde(19) && output->info.supports(Capability::AutoBrightness)) {
      kde_output_configuration_v2_set_auto_brightness(configuration, device, requested.automaticBrightness ? 1 : 0);
    }
    if (supportsKde(20) && output->info.supports(Capability::HdrIccProfile)) {
      if (!requested.hdrIccProfilePath.empty()) {
        kde_output_configuration_v2_set_hdr_icc_profile_path(configuration, device, requested.hdrIccProfilePath.c_str());
      }
      kde_output_configuration_v2_set_hdr_color_profile_source(configuration, device, requested.hdrColorProfileSource);
    }
    if (supportsKde(21) && output->info.supports(Capability::AbmLevel)) {
      kde_output_configuration_v2_set_abm_level(configuration, device, requested.abmLevel);
    }
    if (supportsKde(3)) {
      kde_output_configuration_v2_set_priority(configuration, device, requested.priority);
    }
  }

  kde_output_configuration_v2_apply(configuration);
  if (wl_display_roundtrip(display) < 0) {
    error = std::format("Output configuration failed: {}", waylandError());
    kde_output_configuration_v2_destroy(configuration);
    return false;
  }
  kde_output_configuration_v2_destroy(configuration);
  if (!applyFinished || !applySucceeded) {
    error = applyFailure.empty() ? "The compositor rejected the output configuration." : applyFailure;
    return false;
  }
  syncPublicOutputs();
  return true;
}

bool DisplayOutputBackend::Private::applyWlr(
    const std::vector<DisplayOutputBackend::OutputSettings>& settings, std::string& error
) {
  if (wlrManager == nullptr || !wlrSerialReady) {
    error = "The compositor has not provided a current output configuration serial.";
    return false;
  }
  auto* configuration = zwlr_output_manager_v1_create_configuration(wlrManager, wlrSerial);
  if (configuration == nullptr) {
    error = "Failed to create an output configuration.";
    return false;
  }
  const zwlr_output_configuration_v1_listener listener = {
      .succeeded = &Private::wlrConfigSucceeded,
      .failed = &Private::wlrConfigFailed,
      .cancelled = &Private::wlrConfigCancelled,
  };
  zwlr_output_configuration_v1_add_listener(configuration, &listener, this);
  applyFinished = false;
  applySucceeded = false;
  applyFailure.clear();

  for (const auto& requested : settings) {
    auto* output = wlrForId(requested.id);
    if (output == nullptr) {
      continue;
    }
    if (!requested.enabled) {
      zwlr_output_configuration_v1_disable_head(configuration, output->proxy);
      continue;
    }
    auto* head = zwlr_output_configuration_v1_enable_head(configuration, output->proxy);
    if (head == nullptr) {
      continue;
    }
    if (auto* mode = findWlrModeProxy(*output, requested.mode); mode != nullptr) {
      zwlr_output_configuration_head_v1_set_mode(head, mode);
    }
    zwlr_output_configuration_head_v1_set_position(head, requested.x, requested.y);
    zwlr_output_configuration_head_v1_set_transform(head, requested.transform);
    zwlr_output_configuration_head_v1_set_scale(head, wl_fixed_from_double(std::max(0.01, requested.scale)));
    if (output->info.supports(Capability::Vrr)) {
      zwlr_output_configuration_head_v1_set_adaptive_sync(head, requested.vrrPolicy == 1 ? 1 : 0);
    }
  }

  zwlr_output_configuration_v1_apply(configuration);
  if (wl_display_roundtrip(display) < 0) {
    error = std::format("Output configuration failed: {}", waylandError());
    zwlr_output_configuration_v1_destroy(configuration);
    return false;
  }
  zwlr_output_configuration_v1_destroy(configuration);
  if (!applyFinished || !applySucceeded) {
    error = applyFailure.empty() ? "The compositor rejected the output configuration." : applyFailure;
    return false;
  }
  syncPublicOutputs();
  return true;
}

void DisplayOutputBackend::Private::cleanup() {
  for (auto& output : kdeOutputs) {
    for (const auto& mode : output->modes) {
      if (mode->proxy != nullptr) {
        kde_output_device_mode_v2_destroy(mode->proxy);
      }
    }
    if (output->proxy != nullptr) {
      kde_output_device_v2_release(output->proxy);
    }
  }
  kdeModeOwners.clear();
  kdeByProxy.clear();
  kdeOutputs.clear();

  for (auto& output : wlrOutputs) {
    for (const auto& mode : output->modes) {
      if (mode->proxy != nullptr) {
        zwlr_output_mode_v1_release(mode->proxy);
      }
    }
    if (output->proxy != nullptr) {
      zwlr_output_head_v1_release(output->proxy);
    }
  }
  wlrModeOwners.clear();
  wlrByProxy.clear();
  wlrOutputs.clear();

  if (kdeRegistry != nullptr) {
    kde_output_device_registry_v2_destroy(kdeRegistry);
    kdeRegistry = nullptr;
  }
  if (kdeManagement != nullptr) {
    kde_output_management_v2_destroy(kdeManagement);
    kdeManagement = nullptr;
  }
  if (wlrManager != nullptr) {
    zwlr_output_manager_v1_destroy(wlrManager);
    wlrManager = nullptr;
  }
  if (registry != nullptr) {
    wl_registry_destroy(registry);
    registry = nullptr;
  }
}
