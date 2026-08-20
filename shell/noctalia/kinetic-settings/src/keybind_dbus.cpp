#include "keybind_dbus.h"

#include "core/input/key_chord.h"
#include "core/log.h"
#include "dbus/session_bus.h"
#include "keybind_codec.h"

#include <sdbus-c++/sdbus-c++.h>

#include <algorithm>

namespace {

  const sdbus::ServiceName kGlobalAccelBusName{"org.kde.kglobalaccel"};
  const sdbus::ObjectPath kGlobalAccelPath{"/kglobalaccel"};
  constexpr auto kGlobalAccelInterface = "org.kde.KGlobalAccel";
  constexpr auto kComponentInterface = "org.kde.kglobalaccel.Component";

  // This fork's KGlobalAccelD (pinned commit 5cc88399) renumbered the flags:
  // SetPresent=2, NoAutoloading=4, IsDefault=8 — stock KDE uses NoAutoloading=1.
  // SetPresent must be set: without it the daemon never marks the shortcut
  // present, setActive() bails out and the key never reaches _active_keys, so
  // the shortcut silently never triggers.
  constexpr std::uint32_t kNoAutoloading = 0x6; // SetPresent | NoAutoloading

  const Logger kLog("kinetic-settings");

  // One QKeySequence on the wire. KGlobalShortcutInfo marshals each keyset as
  // a plain 'ai' array (verified against the live daemon: a(ssssssaiai)), while
  // the daemon's setShortcutKeys takes 'a(ai)' — one (ai) per chord.
  using IntArray = std::vector<std::int32_t>;
  using Keyset = sdbus::Struct<IntArray>;
  using Keysets = std::vector<Keyset>;
  // KGlobalShortcutInfo marshalling (kglobalshortcutinfo_dbus.cpp):
  // (actionUnique, friendlyName, componentUnique, componentFriendlyName,
  //  contextUnique, contextFriendlyName, activeShortcuts, defaultShortcuts).
  using ShortcutInfo = sdbus::Struct<
      std::string, std::string, std::string, std::string, std::string, std::string, IntArray, IntArray>;

  [[nodiscard]] Keysets encodeKeys(const std::vector<KeyChord>& chords) {
    Keysets result;
    result.reserve(chords.size());
    for (const auto& chord : chords) {
      const auto key = KeybindCodec::toQtKey(chord);
      if (!key.has_value()) {
        continue;
      }
      // The kglobalaccel QKeySequence D-Bus codec (kglobalshortcutinfo_dbus.cpp)
      // always emits 4 int slots per (ai) element: (key, 0, 0, 0). A shorter
      // array makes the daemon's reader hit end-of-array on the 2nd int and
      // abort the compositor.
      result.emplace_back(std::vector<std::int32_t>{static_cast<std::int32_t>(*key), 0, 0, 0});
    }
    return result;
  }

  [[nodiscard]] std::vector<KeyChord> decodeKeys(const IntArray& keys) {
    std::vector<KeyChord> result;
    result.reserve(keys.size());
    for (const auto value : keys) {
      const auto chord = KeybindCodec::fromQtKey(static_cast<std::uint32_t>(value));
      if (chord.has_value()) {
        result.push_back(*chord);
      }
    }
    return result;
  }

  [[nodiscard]] std::vector<KeyChord> decodeKeys(const Keysets& keys) {
    std::vector<KeyChord> result;
    for (const auto& key : keys) {
      const auto chords = decodeKeys(key.get<0>());
      result.insert(result.end(), chords.begin(), chords.end());
    }
    return result;
  }

  // Chords have no operator<; compare as sorted multisets.
  [[nodiscard]] bool sameChords(const std::vector<KeyChord>& a, const std::vector<KeyChord>& b) {
    if (a.size() != b.size()) {
      return false;
    }
    const auto bySymThenMods = [](const KeyChord& x, const KeyChord& y) {
      if (x.sym != y.sym) {
        return x.sym < y.sym;
      }
      return x.modifiers < y.modifiers;
    };
    auto sortedA = a;
    auto sortedB = b;
    std::sort(sortedA.begin(), sortedA.end(), bySymThenMods);
    std::sort(sortedB.begin(), sortedB.end(), bySymThenMods);
    return sortedA == sortedB;
  }

} // namespace

KeybindDbus::KeybindDbus(SessionBus& bus) : m_bus(bus) {}

std::vector<KeybindDbus::Component> KeybindDbus::components() {
  std::vector<KeybindDbus::Component> result;
  try {
    auto registry = sdbus::createProxy(m_bus.connection(), kGlobalAccelBusName, kGlobalAccelPath);
    std::vector<sdbus::ObjectPath> paths;
    registry->callMethod("allComponents").onInterface(kGlobalAccelInterface).storeResultsTo(paths);
    kLog.debug("kglobalaccel: {} component(s) registered", paths.size());
    for (const auto& path : paths) {
      try {
        auto proxy = sdbus::createProxy(m_bus.connection(), kGlobalAccelBusName, path);
        KeybindDbus::Component component;
        component.uniqueName = proxy->getProperty("uniqueName").onInterface(kComponentInterface).get<std::string>();
        component.friendlyName = proxy->getProperty("friendlyName").onInterface(kComponentInterface).get<std::string>();
        std::vector<ShortcutInfo> infos;
        proxy->callMethod("allShortcutInfos").onInterface(kComponentInterface).storeResultsTo(infos);
        kLog.debug("kglobalaccel: {} -> {} action(s)", path, infos.size());
        for (const auto& info : infos) {
          KeybindDbus::Action action;
          action.actionUnique = info.get<0>();
          action.friendlyName = info.get<1>();
          action.active = decodeKeys(info.get<6>());
          action.defaults = decodeKeys(info.get<7>());
          component.actions.push_back(std::move(action));
        }
        result.push_back(std::move(component));
      } catch (const sdbus::Error& e) {
        kLog.warn("kglobalaccel: component {} failed: {}", path, e.what());
      }
    }
  } catch (const sdbus::Error& e) {
    kLog.warn("kglobalaccel: allComponents failed: {}", e.what());
    return {};
  }
  return result;
}

bool KeybindDbus::setActionShortcuts(
    const std::string& component, const std::string& action, const std::string& friendlyName,
    const std::string& componentFriendlyName, const std::vector<KeyChord>& chords
) {
  try {
    auto proxy = sdbus::createProxy(m_bus.connection(), kGlobalAccelBusName, kGlobalAccelPath);
    // The daemon's findAction() rejects actionIds without all four entries:
    // [component, action, component friendlyName, action friendlyName].
    const std::vector<std::string> id{component, action, componentFriendlyName, friendlyName};
    Keysets result;
    proxy->callMethod("setShortcutKeys")
        .onInterface(kGlobalAccelInterface)
        .withArguments(id, encodeKeys(chords), kNoAutoloading)
        .storeResultsTo(result);
    // The daemon drops chords it cannot apply (conflicts with other actions);
    // compare the returned set with what we asked for.
    const auto accepted = decodeKeys(result);
    if (!sameChords(accepted, chords)) {
      std::string requested;
      for (const auto& c : chords) {
        if (!requested.empty()) {
          requested += ", ";
        }
        requested += KeybindCodec::toChordString(c);
      }
      std::string got;
      for (const auto& c : accepted) {
        if (!got.empty()) {
          got += ", ";
        }
        got += KeybindCodec::toChordString(c);
      }
      kLog.warn(
          "kglobalaccel accepted {}/{} chords for {}.{}: requested [{}], got [{}]", accepted.size(), chords.size(),
          component, action, requested, got
      );
      return false;
    }
    return true;
  } catch (const sdbus::Error& e) {
    kLog.warn("kglobalaccel setShortcutKeys failed for {}.{}: {}", component, action, e.what());
    return false;
  }
}