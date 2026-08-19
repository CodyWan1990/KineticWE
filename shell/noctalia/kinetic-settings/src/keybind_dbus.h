#pragma once

#include <cstdint>
#include <string>
#include <vector>

class SessionBus;
struct KeyChord;

// Thin sdbus-cpp client for the global-shortcut daemon. The daemon runs
// in-process in the kinetic-we compositor (src/globalshortcuts.cpp) but is
// owned and managed by the kglobalaccel framework, so it exposes the standard
// KGlobalAccel D-Bus surface:
//   org.kde.KGlobalAccel                (/kglobalaccel)        — registry ops
//   org.kde.kglobalaccel.Component      (/component/<name>)    — one component
class KeybindDbus {
public:
  explicit KeybindDbus(SessionBus& bus);

  // One shortcut action as reported by Component::allShortcutInfos:
  // (actionUnique, friendlyName, componentUnique, componentFriendlyName,
  //  contextUnique, contextFriendlyName, activeShortcuts, defaultShortcuts).
  struct Action {
    std::string actionUnique;
    std::string friendlyName;
    std::vector<KeyChord> active;
    std::vector<KeyChord> defaults;
  };

  struct Component {
    std::string uniqueName;
    std::string friendlyName;
    std::vector<Action> actions;
  };

  // All registered components with their actions and current/default chords.
  // Empty on error or when the daemon is unreachable.
  [[nodiscard]] std::vector<Component> components();

// Replaces the action's chords (KGlobalAccel::NoAutoloading). Returns true
  // when the daemon accepted exactly the requested chords; false on error or
  // when a conflicting chord was dropped.
  [[nodiscard]] bool setActionShortcuts(
      const std::string& component, const std::string& action, const std::string& friendlyName,
      const std::string& componentFriendlyName, const std::vector<KeyChord>& chords
  );

private:
  SessionBus& m_bus;
};