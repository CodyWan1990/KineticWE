#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

class SessionBus;

// User-created global-shortcut actions, shown by the keybinds page as two
// extra components: "applications" (apps from the launcher's desktop-entry
// list) and "custom-commands" (shell command lines). The daemon only knows the
// actions and their chords; what to run on a trigger lives here.
//
// Definitions are persisted in custom-keybinds.kwe (next to the keybinds.kwe
// mirror); chords flow through the same paths as every other bind (the
// daemon's config + the mirror replay at session start). Presses are received
// via the daemon's globalShortcutPressed D-Bus signal on each component's
// object path; the stored command / app exec is then launched.
class CustomKeybinds {
public:
  inline static constexpr std::string_view kAppsComponent = "applications";
  inline static constexpr std::string_view kCommandsComponent = "custom-commands";
  inline static constexpr std::string_view kAppsFriendly = "Applications";
  inline static constexpr std::string_view kCommandsFriendly = "Custom Commands";

  struct Definition {
    std::string component;
    std::string action;
    std::string friendlyName;
    std::string command;
  };

  explicit CustomKeybinds(SessionBus& bus);
  ~CustomKeybinds();

  CustomKeybinds(const CustomKeybinds&) = delete;
  CustomKeybinds& operator=(const CustomKeybinds&) = delete;

  // Registers every persisted definition in the daemon and subscribes to the
  // trigger signals. Call before the keybinds mirror is replayed at session
  // start so mirror chords find their actions.
  void initialize();

  [[nodiscard]] const std::vector<Definition>& definitions() const noexcept { return m_definitions; }

  // Registers a new action (no chords yet) and persists its definition.
  [[nodiscard]] bool addApp(const std::string& appId, const std::string& friendlyName, const std::string& exec);
  [[nodiscard]] bool addCommand(const std::string& command);

  // Unregisters the action from the daemon and drops its definition (and its
  // mirror entry, if any).
  [[nodiscard]] bool remove(std::string_view component, std::string_view action);

private:
  void load();
  [[nodiscard]] bool save() const;
  [[nodiscard]] bool registerAction(const Definition& definition);
  void subscribeSignals();
  void onTriggered(const std::string& component, const std::string& action);

  SessionBus& m_bus;
  std::vector<Definition> m_definitions;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};