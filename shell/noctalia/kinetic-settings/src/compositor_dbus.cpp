#include "compositor_dbus.h"

#include "core/log.h"
#include "dbus/session_bus.h"

#include <sdbus-c++/sdbus-c++.h>

namespace {

  const sdbus::ServiceName kKwinBusName{"org.kde.KWin"};
  const sdbus::ObjectPath kKwinPath{"/KWin"};
  const sdbus::ObjectPath kTilingPath{"/Tiling"};
  const sdbus::ObjectPath kVirtualDesktopsPath{"/VirtualDesktopManager"};
  constexpr auto kKwinInterface = "org.kde.KWin";
  constexpr auto kTilingInterface = "org.kde.KWin.Tiling";
  constexpr auto kVirtualDesktopsInterface = "org.kde.KWin.VirtualDesktopManager";

  const Logger kLog("kinetic-settings");

} // namespace

CompositorDbus::CompositorDbus(SessionBus& bus) : m_bus(bus) {}

void CompositorDbus::requestReconfigure() {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kKwinPath);
    proxy->callMethod("reconfigure").onInterface("org.kde.KWin");
  } catch (const sdbus::Error& e) {
    kLog.warn("compositor reconfigure failed: {}", e.what());
  }
}

std::string CompositorDbus::activeOutputName() {
  try {
    std::string result;
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kKwinPath);
    proxy->callMethod("activeOutputName").onInterface(kKwinInterface).storeResultsTo(result);
    return result;
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin activeOutputName failed: {}", e.what());
    return {};
  }
}

std::string CompositorDbus::currentLayout() {
  try {
    std::string result;
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kTilingPath);
    proxy->callMethod("currentLayout").onInterface(kTilingInterface).storeResultsTo(result);
    return result;
  } catch (const sdbus::Error& e) {
    kLog.warn("tiling currentLayout failed: {}", e.what());
    return {};
  }
}

std::vector<std::string> CompositorDbus::enabledLayouts() {
  try {
    std::vector<std::string> result;
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kTilingPath);
    proxy->callMethod("enabledLayouts").onInterface(kTilingInterface).storeResultsTo(result);
    return result;
  } catch (const sdbus::Error& e) {
    kLog.warn("tiling enabledLayouts failed: {}", e.what());
    return {};
  }
}

void CompositorDbus::setLayout(const std::string& kind) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kTilingPath);
    proxy->callMethod("setLayout").onInterface(kTilingInterface).withArguments(kind);
  } catch (const sdbus::Error& e) {
    kLog.warn("tiling setLayout failed: {}", e.what());
  }
}

void CompositorDbus::cycleLayout() {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kTilingPath);
    proxy->callMethod("cycleLayout").onInterface(kTilingInterface);
  } catch (const sdbus::Error& e) {
    kLog.warn("tiling cycleLayout failed: {}", e.what());
  }
}

void CompositorDbus::toggleOuterGaps() {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kTilingPath);
    proxy->callMethod("toggleOuterGaps").onInterface(kTilingInterface);
  } catch (const sdbus::Error& e) {
    kLog.warn("tiling toggleOuterGaps failed: {}", e.what());
  }
}

std::vector<CompositorDbus::VirtualDesktopInfo> CompositorDbus::virtualDesktops() {
  try {
    using DesktopStruct = sdbus::Struct<std::uint32_t, std::string, std::string>;
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    const auto raw = proxy->getProperty("desktops")
                         .onInterface(kVirtualDesktopsInterface)
                         .template get<std::vector<DesktopStruct>>();
    std::vector<VirtualDesktopInfo> out;
    out.reserve(raw.size());
    for (const auto& entry : raw) {
      out.push_back(VirtualDesktopInfo{
          .position = entry.get<0>(),
          .id = entry.get<1>(),
          .name = entry.get<2>(),
      });
    }
    return out;
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin virtualDesktops failed: {}", e.what());
    return {};
  }
}

std::uint32_t CompositorDbus::virtualDesktopRows() {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    return proxy->getProperty("rows")
        .onInterface(kVirtualDesktopsInterface)
        .template get<std::uint32_t>();
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin virtualDesktopRows failed: {}", e.what());
    return 0;
  }
}

void CompositorDbus::createDesktop(std::uint32_t position, const std::string& name) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    proxy->callMethod("createDesktop")
        .onInterface(kVirtualDesktopsInterface)
        .withArguments(position, name);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin createDesktop failed: {}", e.what());
  }
}

void CompositorDbus::removeDesktop(const std::string& id) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    proxy->callMethod("removeDesktop")
        .onInterface(kVirtualDesktopsInterface)
        .withArguments(id);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin removeDesktop failed: {}", e.what());
  }
}

void CompositorDbus::setDesktopName(const std::string& id, const std::string& name) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    proxy->callMethod("setDesktopName")
        .onInterface(kVirtualDesktopsInterface)
        .withArguments(id, name);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin setDesktopName failed: {}", e.what());
  }
}

void CompositorDbus::setVirtualDesktopRows(std::uint32_t rows) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kVirtualDesktopsPath);
    proxy->setProperty("rows").onInterface(kVirtualDesktopsInterface).toValue(rows);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin set rows failed: {}", e.what());
  }
}