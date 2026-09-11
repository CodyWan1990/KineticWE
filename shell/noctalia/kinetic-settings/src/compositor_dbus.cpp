#include "compositor_dbus.h"

#include "core/log.h"
#include "dbus/session_bus.h"

#include <sdbus-c++/sdbus-c++.h>

namespace {

  const sdbus::ServiceName kKwinBusName{"org.kde.KWin"};
  const sdbus::ObjectPath kKwinPath{"/KWin"};
  const sdbus::ObjectPath kTilingPath{"/Tiling"};
  const sdbus::ObjectPath kVirtualDesktopsPath{"/VirtualDesktopManager"};
  const sdbus::ObjectPath kEffectsPath{"/Effects"};
  const sdbus::ObjectPath kInputDevicesPath{"/org/kde/KWin/InputDevice"};
  constexpr auto kKwinInterface = "org.kde.KWin";
  constexpr auto kTilingInterface = "org.kde.KWin.Tiling";
  constexpr auto kVirtualDesktopsInterface = "org.kde.KWin.VirtualDesktopManager";
  constexpr auto kEffectsInterface = "org.kde.kwin.Effects";
  constexpr auto kInputDeviceManagerInterface = "org.kde.KWin.InputDeviceManager";
  constexpr auto kInputDeviceInterface = "org.kde.KWin.InputDevice";

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

void CompositorDbus::reconfigureEffect(const std::string& name) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kEffectsPath);
    proxy->callMethod("reconfigureEffect").onInterface(kEffectsInterface).withArguments(name);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin reconfigureEffect failed: {}", e.what());
  }
}

void CompositorDbus::loadEffect(const std::string& name) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kEffectsPath);
    proxy->callMethod("loadEffect").onInterface(kEffectsInterface).withArguments(name);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin loadEffect failed: {}", e.what());
  }
}

void CompositorDbus::unloadEffect(const std::string& name) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kEffectsPath);
    proxy->callMethod("unloadEffect").onInterface(kEffectsInterface).withArguments(name);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin unloadEffect failed: {}", e.what());
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

CompositorDbus::DetectedWindow CompositorDbus::queryWindowInfo() {
  DetectedWindow out;
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kKwinPath);
    std::map<std::string, sdbus::Variant> map;
    proxy->callMethod("queryWindowInfo").onInterface(kKwinInterface).storeResultsTo(map);

    const auto str = [&map](const char* key) -> std::string {
      const auto it = map.find(key);
      if (it == map.end()) {
        return {};
      }
      try {
        return it->second.get<std::string>();
      } catch (...) {
        return {};
      }
    };
    const auto integer = [&map](const char* key, int fallback = 0) -> int {
      const auto it = map.find(key);
      if (it == map.end()) {
        return fallback;
      }
      try {
        return it->second.get<int>();
      } catch (...) {
        return fallback;
      }
    };
    const auto boolean = [&map](const char* key, bool fallback = false) -> bool {
      const auto it = map.find(key);
      if (it == map.end()) {
        return fallback;
      }
      try {
        return it->second.get<bool>();
      } catch (...) {
        return fallback;
      }
    };
    // Window::x()/y()/width()/height() are qreal, so these arrive as doubles.
    const auto real = [&map](const char* key, double fallback = 0.0) -> double {
      const auto it = map.find(key);
      if (it == map.end()) {
        return fallback;
      }
      try {
        return it->second.get<double>();
      } catch (...) {
        return fallback;
      }
    };

    out.resourceClass = str("resourceClass");
    out.resourceName = str("resourceName");
    out.role = str("role");
    out.caption = str("caption");
    out.desktopFile = str("desktopFile");
    out.x = static_cast<int>(real("x"));
    out.y = static_cast<int>(real("y"));
    out.width = static_cast<int>(real("width"));
    out.height = static_cast<int>(real("height"));
    out.fullscreen = boolean("fullscreen");
    out.keepAbove = boolean("keepAbove");
    out.keepBelow = boolean("keepBelow");
    out.minimized = boolean("minimized");
    out.noBorder = boolean("noBorder");
    out.maximizeHorizontal = boolean("maximizeHorizontal");
    out.maximizeVertical = boolean("maximizeVertical");
    out.decorationPolicy = integer("decorationPolicy", 1);
    out.valid = true;
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin queryWindowInfo failed: {}", e.what());
  }
  return out;
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

// ---------------------------------------------------------------------------
// Input devices (org.kde.KWin.InputDeviceManager / org.kde.KWin.InputDevice).
// ---------------------------------------------------------------------------

std::vector<CompositorDbus::InputDeviceInfo> CompositorDbus::inputDevices() {
  std::vector<InputDeviceInfo> out;
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, kInputDevicesPath);
    std::vector<std::string> sysNames;
    proxy->callMethod("ListPointers").onInterface(kInputDeviceManagerInterface).storeResultsTo(sysNames);
    out.reserve(sysNames.size());
    for (const auto& sysName : sysNames) {
      InputDeviceInfo info;
      info.sysName = sysName;
      if (const auto name = inputDeviceString(sysName, "name"); name.has_value()) {
        info.name = *name;
      }
      info.pointer = inputDeviceBool(sysName, "pointer").value_or(false);
      info.touchpad = inputDeviceBool(sysName, "touchpad").value_or(false);
      info.touch = inputDeviceBool(sysName, "touch").value_or(false);
      info.keyboard = inputDeviceBool(sysName, "keyboard").value_or(false);
      info.isVirtual = inputDeviceBool(sysName, "isVirtual").value_or(false);
      info.supportedButtons = inputDeviceInt(sysName, "supportedButtons").value_or(0);
      info.vendor = inputDeviceUint(sysName, "vendor").value_or(0);
      info.product = inputDeviceUint(sysName, "product").value_or(0);
      if (info.name.empty()) {
        info.name = sysName;
      }
      out.push_back(std::move(info));
    }
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin ListPointers failed: {}", e.what());
  }
  return out;
}

std::optional<bool> CompositorDbus::inputDeviceBool(std::string_view sysName, std::string_view property) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    return proxy->getProperty(std::string(property)).onInterface(kInputDeviceInterface).get<bool>();
  } catch (const sdbus::Error& e) {
    return std::nullopt;
  }
}

std::optional<double> CompositorDbus::inputDeviceDouble(std::string_view sysName, std::string_view property) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    return proxy->getProperty(std::string(property)).onInterface(kInputDeviceInterface).get<double>();
  } catch (const sdbus::Error& e) {
    return std::nullopt;
  }
}

std::optional<std::uint32_t> CompositorDbus::inputDeviceUint(std::string_view sysName, std::string_view property) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    return proxy->getProperty(std::string(property)).onInterface(kInputDeviceInterface).get<std::uint32_t>();
  } catch (const sdbus::Error& e) {
    return std::nullopt;
  }
}

std::optional<int> CompositorDbus::inputDeviceInt(std::string_view sysName, std::string_view property) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    return proxy->getProperty(std::string(property)).onInterface(kInputDeviceInterface).get<int>();
  } catch (const sdbus::Error& e) {
    return std::nullopt;
  }
}

std::optional<std::string> CompositorDbus::inputDeviceString(std::string_view sysName, std::string_view property) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    return proxy->getProperty(std::string(property)).onInterface(kInputDeviceInterface).get<std::string>();
  } catch (const sdbus::Error& e) {
    return std::nullopt;
  }
}

void CompositorDbus::setInputDeviceBool(std::string_view sysName, std::string_view property, bool value) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    proxy->setProperty(std::string(property)).onInterface(kInputDeviceInterface).toValue(value);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin set device property {} failed: {}", std::string(property), e.what());
  }
}

void CompositorDbus::setInputDeviceDouble(std::string_view sysName, std::string_view property, double value) {
  try {
    const std::string devicePath = "/org/kde/KWin/InputDevice/" + std::string(sysName);
    auto proxy = sdbus::createProxy(m_bus.connection(), kKwinBusName, sdbus::ObjectPath{devicePath});
    proxy->setProperty(std::string(property)).onInterface(kInputDeviceInterface).toValue(value);
  } catch (const sdbus::Error& e) {
    kLog.warn("kwin set device property {} failed: {}", std::string(property), e.what());
  }
}