#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct DesktopEntry;

// The user's XDG autostart directory (~/.config/autostart). Kinetic Settings'
// System -> Autostart page lists, creates and removes .desktop files here;
// the shell calls launchAll() once per session to start them. Entries written
// by the freedesktop Background portal (org.freedesktop.portal.Background
// EnableAutostart, e.g. an app's own "start on login" toggle) land in the same
// directory and therefore show up on the page too.
namespace autostart {

  struct Entry {
    std::string filePath;   // full path of the .desktop file
    std::string fileName;   // basename, the id passed to remove()
    std::string name;       // [Desktop Entry] Name, falling back to the file stem
    std::string command;    // [Desktop Entry] Exec (raw, as stored)
    bool isCommand = false; // written by "Add Custom Command" (run through a shell)
    bool hidden = false;    // Hidden=true or X-GNOME-Autostart-enabled=false
  };

  // ~/.config/autostart in the real XDG config home (never KWE_CONFIG_HOME).
  [[nodiscard]] std::filesystem::path configDir();

  // Every *.desktop file in configDir(), ordered by file name.
  [[nodiscard]] std::vector<Entry> entries();

  // Copies the app's own desktop file into configDir() as "<app id>.desktop",
  // so Exec / Icon / DBusActivatable / flatpak wrappers are preserved.
  [[nodiscard]] bool addApplication(const DesktopEntry& app);

  // Writes a command entry. The command is run through /bin/sh at login, so it
  // may use shell syntax exactly like a custom keybind command.
  [[nodiscard]] bool addCommand(const std::string& command);

  // Removes the entry's .desktop file by basename.
  [[nodiscard]] bool remove(std::string_view fileName);

  // Launches every enabled entry. Runs once per session: a marker in
  // $XDG_RUNTIME_DIR makes later calls (e.g. a supervised shell restart within
  // the same login) no-ops so applications are not started twice.
  void launchAll();

} // namespace autostart
